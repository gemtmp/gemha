#include "Arduino.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <esp_task_wdt.h>

#define DEBUG

#include "../common/button.h"
#include "../common/temperature.h"
#include "../common/wifi.h"

#include "../config/gemconfig.h"

static const int INPUTS = 4;
static const int RELAYS = 4;

static const uint8_t oneWirePin = 26;

static const uint8_t relays[RELAYS] = { 22, 21, 17, 16 };
gemha::Button inputs[INPUTS] = {27, 25, {39, false, true}, {35, false, true}};

uint8_t mapping[INPUTS][1] = {{0}, {1}, {0}, {1}};

const char *otaHostname = "light2.gem";

#define TOPIC_PREFIX "house/light2/"
#define TOPIC_INPUT "input/"
#define TOPIC_RELAY "relay/"

#ifdef DEBUG
const unsigned long PERIOD = 5000;
#else
const unsigned long PERIOD = 30000;
#endif

volatile bool isOnline = false;

TaskHandle_t inputTask;
TaskHandle_t loggerTask;
TaskHandle_t tempTask;

WiFiClient espClient;
PubSubClient client(espClient);

OneWire oneWire(oneWirePin);
gemha::Temperature temperatures(TOPIC_PREFIX "temp/", &oneWire, &client);

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

void processRelay(int channel, int value) {
	if (channel < 0 || channel > RELAYS - 1)
		return;
	if (value != 0 && value != 1)
		return;
#ifdef DEBUG
	Serial.print("Set relay ");
	Serial.print(channel);
	Serial.print(" to ");
	Serial.println(value);
#endif
	digitalWrite(relays[channel], !value);

	EEPROM.write(channel, !value);
	EEPROM.commit();
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

	if (strncmp(topic, TOPIC_PREFIX TOPIC_RELAY, sizeof(TOPIC_PREFIX TOPIC_RELAY) - 1) != 0)
		return;
	topic += sizeof(TOPIC_PREFIX TOPIC_RELAY) - 1;

	char *dummy;
	int channel = strtoul(topic, &dummy, 10);
	if (dummy == topic)
		return;

	int value = getValue(payload, length);
	if (value == -1)
		return;

	processRelay(channel, value);
}

void logger(void *p) {
	for (;;) {
		Serial.print(isOnline ? "Online  " : "Offline ");
		Serial.print("Inputs: ");
		for (auto i: inputs) {
			Serial.print(i ? " 1": " 0");
		}
		Serial.print(" Relays: ");
		for (auto i: relays) {
			Serial.print(digitalRead(i) ? " 1": " 0");
		}
		temperatures.log(Serial);
		Serial.println();
		delay(1000);
	}
}

void readInputs(void *p) {
	for (;;) {
		for (auto i = 0; i < INPUTS; i++) {
			inputs[i].check();
		}
		if (!isOnline && millis() > 15000) {
			for (auto i = 0; i < INPUTS; i++) {
				for (auto r: mapping[i]) {
					if (r < RELAYS)
						digitalWrite(relays[r], inputs[i].value());
				}
			}
		}

		delay(5);
	}
}

void readTemperatures(void *p) {
	for (uint8_t i = 0; ;i++) {
		if (i % 4 == 0) {
			temperatures.search();
		}
		temperatures.startMeasure();
		temperatures.read();
		delay(1000);
	}
}

void setup() {
	EEPROM.begin(RELAYS);
	int pos = 0;
	for (auto i: relays) {
		pinMode(i, OUTPUT);
		digitalWrite(i, EEPROM.read(pos++));
	}

#ifdef DEBUG
	Serial.begin(115200);

	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
#endif

	xTaskCreate(readInputs, "input", 4096, nullptr, 1, &inputTask);
	xTaskCreate(readTemperatures, "temp", 4096, nullptr, 1, &tempTask);

	gemha::initWiFi(otaHostname);

	espClient.setTimeout(1);
	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
	client.setSocketTimeout(3);

	temperatures.start();
	temperatures.readAll();
}

bool publish(bool force) {
	static bool published[INPUTS];

	bool ret = true;
	char topic[] = TOPIC_PREFIX TOPIC_INPUT "\0\0";
	int pos = sizeof(topic) - 3;
	for (int i = 0; i < INPUTS && ret; i++) {
		if (!force && published[i] == inputs[i])
			continue;
		if (i < 10)
			topic[pos] = '0' + i;
		else {
			topic[pos] = '0' + (i / 10);
			topic[pos + 1] = '0' + (i %10);
		}
		ret &= client.publish(topic, inputs[i] ? "0" : "1");
		if (ret)
			published[i] = inputs[i];
	}

	if (force) {
		char topic[] = TOPIC_PREFIX TOPIC_RELAY "\0\0";
		int pos = sizeof(topic) - 3;
		for (int i = 0; i < RELAYS && ret; i++) {
			if (i < 10)
				topic[pos] = '0' + i;
			else {
				topic[pos] = '0' + (i / 10);
				topic[pos + 1] = '0' + (i %10);
			}
			ret &= client.publish(topic, digitalRead(relays[i]) ? "0" : "1");
		}
	}
	return ret;
}

void loop() {
	static bool force = true;
	esp_task_wdt_reset();
	ArduinoOTA.handle();
	client.loop();
	isOnline = gemha::connectMqtt(client, otaHostname, TOPIC_PREFIX TOPIC_RELAY "#");

	if (isOnline) {
		static unsigned long last;
		unsigned long now = millis();
		bool pereodicForce = false;
		if (now - last > PERIOD) {
			last = now;
			pereodicForce = true;

			temperatures.publish();
		}
		if (!publish(force || pereodicForce))
			client.disconnect();
	}
	force = !isOnline;

	delay(1);
}

