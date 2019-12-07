#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <TM1637Display.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "../config/gemconfig.h"

const char *otaHostname = "co2.gem";

#define TOPIC "house/sensor/1"
const char *topicCo2 = TOPIC"/co2";
const char *topicBrightness = TOPIC"/brightness";

const int CLK = D3;
const int DIO = D4;
const int RX = D5;
const int TX = D2;

const long PERIOD = 30000;

int readCO2();
void callbackMqtt(char *topic, byte *payload, unsigned int length);

TM1637Display display(CLK, DIO);
SoftwareSerial co2Serial(RX, TX);

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
	Serial.begin(115200);
	Serial.println("");

	display.setBrightness(5);
	display.showNumberDec(1);

	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.begin(ssid, passwd);
	display.showNumberDec(11);

	while (WiFi.status() != WL_CONNECTED) {
		display.showNumberDec(111);
		delay(250);
		display.showNumberDec(11);
		delay(250);
		Serial.print(".");
	}
	Serial.println("");
	display.showNumberDec(111);

	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	display.showNumberDec(1111);
	randomSeed(micros());

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	co2Serial.begin(9600);

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
	readCO2(); // ignore first read
}

bool publish(int co2) {
	if (!client.connected()) {
		String clientId = "Co2Client-";
		clientId += String(random(0xffff), HEX);
		if (client.connect(clientId.c_str())) {
			client.subscribe(topicBrightness);
		} else {
			Serial.print("mqtt connect failed, rc=");
			Serial.println(client.state());
			return false;
		}
	}
	if (co2 < 0)
		return true;
	char msg[16];
	snprintf(msg, sizeof(msg), "%d", co2);
	return client.publish(topicCo2, msg);
}

long lastRead;

void loop() {
	client.loop();
	ArduinoOTA.handle();

	publish(-1);

	long now = millis();
	if (now - lastRead > PERIOD) {
		lastRead = now;
		int CO2 = readCO2();
		Serial.print("CO2 (ppm): ");
		Serial.println(CO2);
		auto dots = publish(CO2) ? 0 : 0b01000000;
		display.showNumberDecEx(CO2, dots);
	}
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}
	int value = payload[0] - '0';
	Serial.print(", value: ");
	Serial.println(value);

	if (value >= 0 && value < 8) {
		display.setBrightness(value);
		display.showNumberDec(value);
	}
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
