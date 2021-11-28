#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>

#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_HTU21DF.h>

#include <ESP8266mDNS.h>
//#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "AM2321.h"

#include "../config/gemconfig.h"

const char *otaHostname = "co2.gem";

#define TOPIC "house/sensor/1"
const char *topicCo2 = TOPIC"/co2";
const char *topicTemperatue = TOPIC"/temperature";
const char *topicHumidity = TOPIC"/humidity";
const char *topicBrightness = TOPIC"/brightness";
const char *topicBroadcast = "house/broadcast";

const int CLK = D3;
const int DIO = D4;
const int RX = D5;
const int TX = D6;

const long PERIOD = 30000;

char broadcast[21];

int readCO2();
void callbackMqtt(char *topic, byte *payload, unsigned int length);

SoftwareSerial co2Serial(RX, TX);

Adafruit_HTU21DF htu;
AM2321 am2321;

WiFiClient espClient;
PubSubClient client(espClient);

LiquidCrystal_I2C lcd(0x38 + 7, 20, 4);

void setup() {
	Serial.begin(115200);
	Serial.println("");
	delay(2000);

	Wire.begin(SDA, SCL);

	lcd.init();
	lcd.backlight();
	lcd.setCursor(0, 0);

	lcd.print("Connecting to ");
	lcd.print(ssid);
	lcd.blink();
	lcd.setCursor(0, 1);

	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.begin(ssid, passwd);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		lcd.print(".");
		Serial.print(".");
	}
	lcd.noBlink();
	Serial.println("");

	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Connected to ");
	lcd.print(ssid);
	lcd.setCursor(0, 1);
	lcd.print("IP ");
	lcd.print(WiFi.localIP());

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
bool clear = true;
void loop() {
	client.loop();
	ArduinoOTA.handle();

	publish(-1, 0, 0);

	long now = millis();
	if (now - lastRead > PERIOD) {
		lastRead = now;
		int CO2 = readCO2();

//		float t = htu.readTemperature();
//		float h = htu.readHumidity();

		am2321.read();
		float t = am2321.temperature/10;
		float h = am2321.humidity/10;

#ifdef DEBUG
		Serial.printf("CO2: %d, T: %.2f, H: %.2f\r\n", CO2, t, h);
#endif
		if (clear) {
			lcd.clear();
			clear = false;
		}
		lcd.setCursor(0, 0);
		lcd.printf("CO2:       %4d ppm", CO2);
		lcd.setCursor(0, 1);
		lcd.printf("Temperature %5.1f \xDF""C", t);
		lcd.setCursor(0, 2);
		lcd.printf("Humidity    %5.1f %%", h);
		bool connected = publish(CO2, t , h);
		lcd.setCursor(0, 3);
		lcd.print(connected ? broadcast : "Offline             ");
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
	const int buflen = sizeof(broadcast)-1;
	int len = strnlen((const char*)payload, buflen > length ? length : buflen);
	for(int i = buflen - len; i >= 0; i-- )
		broadcast[i] = ' ';
	memcpy(broadcast + buflen - len, payload, len);
	lcd.setCursor(0, 3);
	lcd.print(broadcast);
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
