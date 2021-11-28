#include <Arduino.h>
#include <ESP8266WiFi.h>

#include <PubSubClient.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


#include "../config/gemconfig.h"

const char *otaHostname = "bsw.gem";

//#define DEBUG

#define TOPIC "house/boiler/switch/"
#define TOPIC_VALUE TOPIC "value"
#define TOPIC_INPUT TOPIC "input/"


const int Relay = D1;
const int Input0 = D5;
const int Input1 = D6;

#ifdef DEBUG
const long PERIOD = 1000;
#else
const long PERIOD = 5000;
#endif

void callbackMqtt(char *topic, byte *payload, unsigned int length);


WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
	pinMode(Relay, OUTPUT_OPEN_DRAIN);
	digitalWrite(Relay, 1);
	pinMode(Input0, INPUT_PULLUP);
	pinMode(Input1, INPUT_PULLUP);

	Serial.begin(115200);
	Serial.println("");
	delay(2000);

	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.hostname(otaHostname);
	WiFi.begin(ssid, passwd);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("");

	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	randomSeed(micros());

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
}

bool publish() {
	if (!client.connected()) {
		String clientId = "Co2Client-";
		clientId += String(random(0xffff), HEX);
		if (client.connect(clientId.c_str())) {
			client.subscribe(TOPIC_VALUE);
		} else {
			Serial.print("mqtt connect failed, rc=");
			Serial.println(client.state());
			return false;
		}
	}
	bool ret = true;

	ret &= client.publish(TOPIC_VALUE, digitalRead(Relay) ? "0" : "1");
	ret &= client.publish(TOPIC_INPUT "0", digitalRead(Input0) ? "1" : "0");
	ret &= client.publish(TOPIC_INPUT "1", digitalRead(Input1) ? "1" : "0");

	return ret;
}

long lastRead;
void loop() {
	client.loop();
	ArduinoOTA.handle();

	long now = millis();
	if (now - lastRead > PERIOD) {
		lastRead = now;

#ifdef DEBUG
		Serial.printf("Relay: %d, Input0: %d, Input1: %d\r\n",
				digitalRead(Relay),
				digitalRead(Input0),
				digitalRead(Input1));
#endif
		publish();
	}
}

int getValue(const byte *payload, unsigned int length) {
	char buf[8];
	memset(buf, 0, sizeof(buf));
	if (length >= sizeof(buf))
		return -1;

	memcpy(buf, payload, length);
	char *dummy;
	int value = strtoul(buf, &dummy, 10);
	if (dummy == buf)
		return -1;
	return value;
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
#ifdef DEBUG
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (unsigned i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}
	Serial.println();
#endif

	if (strncmp(topic, TOPIC_VALUE, sizeof(TOPIC_VALUE) - 1) != 0)
		return;

	int value = getValue(payload, length);
	if (value != 0 && value != 1)
		return;
	value = !value;
	Serial.print("Set value to ");
	Serial.println(value);

	digitalWrite(Relay, value);
}

