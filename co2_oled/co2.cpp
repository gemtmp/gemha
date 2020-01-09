#include <Arduino.h>
#include <ESP8266WiFi.h>

#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_HTU21DF.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "../config/gemconfig.h"

const char *otaHostname = "co2.gem";

//#define DEBUG

#define TOPIC "house/sensor/3"
const char *topicCo2 = TOPIC"/co2";
const char *topicTemperatue = TOPIC"/temperature";
const char *topicHumidity = TOPIC"/humidity";
const char *topicBrightness = TOPIC"/brightness";
const char *topicBroadcast = "house/broadcast";

const int RX = D5;
const int TX = D6;

#ifdef DEBUG
const long PERIOD = 10000;
#else
const long PERIOD = 30000;
#endif

struct State {
	int co2 = -1;
	float t = 0;
	float h = 0;
	char text[21];

	State() {
		text[0] = 0;
		text[sizeof(text) - 1] = 0;
		shift = 0;
		lastShow = 0;
	}

	void setText(const char *str, size_t length) {
		const int buflen = sizeof(text) - 1;
		int len = strnlen(str, buflen > length ? length : buflen);
		for (int i = buflen - len; i >= 0; i--)
			text[i] = ' ';
		memcpy(text + buflen - len, str, len);
	}

	template<class D>
	void show(D &display) {
		long now = millis();
		if (now - lastShow > PERIOD) {
			lastShow = now;
			shift = shift < 2 ? shift + 1 : 0;
		}
		display.clearDisplay();
		display.setCursor(0 + shift, 0 + shift);
		display.printf("CO2:       %4d ppm", co2);
		display.setCursor(0 + shift, 8 + shift);
		display.printf("Temperature %5.1f \xDF" "C", t);
		display.setCursor(0 + shift, 16 + shift);
		display.printf("Humidity    %5.1f %%", h);
		display.setCursor(0 + shift, 24 + shift);
		display.print(text);
		display.display();
	}

private:
	int16_t shift;
	long lastShow;
};
State state;

int readCO2();
void callbackMqtt(char *topic, byte *payload, unsigned int length);

SoftwareSerial co2Serial(RX, TX);

Adafruit_HTU21DF htu;

WiFiClient espClient;
PubSubClient client(espClient);

Adafruit_SSD1306 display(128, 32);

void setup() {
	Serial.begin(115200);
	Serial.println("");
	delay(2000);

	Wire.begin(SDA, SCL);

	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
		Serial.println(F("SSD1306 allocation failed"));
		for (;;)
			;
	}
	display.clearDisplay();
	display.display();
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);

	display.print("Connecting to ");
	display.print(ssid);
	display.setCursor(0, 8);
	display.display();

	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.begin(ssid, passwd);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		display.print(".");
		display.display();
		Serial.print(".");
	}
	Serial.println("");

	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	display.clearDisplay();
	display.setCursor(0, 0);
	display.print("Connected to ");
	display.print(ssid);
	display.setCursor(0, 8);
	display.print("IP ");
	display.print(WiFi.localIP());
	display.display();

	randomSeed(micros());

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	co2Serial.begin(9600);

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
	readCO2(); // ignore first read

	htu.begin();
}

bool publish(int co2, float t, float h) {
	if (!client.connected()) {
		String clientId = "Co2Client-";
		clientId += String(random(0xffff), HEX);
		if (client.connect(clientId.c_str())) {
			client.subscribe(topicBroadcast);
		} else {
			Serial.print("mqtt connect failed, rc=");
			Serial.println(client.state());
			return false;
		}
	}
	bool ret = true;
	char msg[16];
	if (co2 > 0) {
		snprintf(msg, sizeof(msg), "%d", co2);
		ret &= client.publish(topicCo2, msg);
	}
	if (t != 0 && h != 0) {
		snprintf(msg, sizeof(msg), "%.2f", t);
		ret &= client.publish(topicTemperatue, msg);
		snprintf(msg, sizeof(msg), "%.2f", h);
		ret &= client.publish(topicHumidity, msg);
	}

	return ret;
}

long lastRead;
void loop() {
	client.loop();
	ArduinoOTA.handle();

	publish(-1, 0, 0);

	long now = millis();
	if (now - lastRead > PERIOD) {
		lastRead = now;
		state.co2 = readCO2();
		state.t = htu.readTemperature();
		state.h = htu.readHumidity();

#ifdef DEBUG
		Serial.printf("CO2: %d, T: %.2f, H: %.2f\r\n", state.co2, state.t,
				state.h);
#endif
		bool connected = publish(state.co2, state.t, state.h);
		if (!connected) {
			state.setText("Offline             ", 20);
		}
		state.show(display);
	}
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
#ifdef DEBUG
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}
	Serial.println();
#endif
	state.setText((const char*) payload, length);
	state.show(display);
}

int readCO2() {
	byte cmd[9] = { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 };
// command to ask for data
	byte response[9]; // for answer

	co2Serial.write(cmd, 9); //request PPM CO2

// The serial stream can get out of sync. The response starts with 0xff, try to resync.
	while (co2Serial.available() > 0 && (unsigned char) co2Serial.peek() != 0xFF) {
		co2Serial.read();
	}

	memset(response, 0, 9);
	co2Serial.readBytes(response, 9);

	if (response[1] != 0x86) {
		Serial.println("Invalid response from co2 sensor!");
		return -1;
	}

	byte crc = 0;
	for (int i = 1; i < 8; i++) {
		crc += response[i];
	}
	crc = 255 - crc + 1;

	if (response[8] == crc) {
		int responseHigh = (int) response[2];
		int responseLow = (int) response[3];
		int ppm = (256 * responseHigh) + responseLow;
		return ppm;
	} else {
		Serial.println("CRC error!");
		return -1;
	}
}

