#include "Arduino.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <esp_task_wdt.h>
#include <PZEM004Tv30.h>

#include "../common/button.h"
#include "../common/wifi.h"

#include "../config/gemconfig.h"

static const int INPUTS = 2;
static const int RELAYS = 4;

uint8_t relays[RELAYS] = { 19, 18, 5, 4 };
gemha::Button inputs[INPUTS] = {36, 39};

uint8_t mapping[INPUTS][2] = {{0, 1}, {2, 3}};

const char *otaHostname = "garage.gem";

#define TOPIC_PREFIX "house/garage/"
#define TOPIC_INPUT "input/"
#define TOPIC_RELAY "relay/"
#define TOPIC_PZEM "power/"

const unsigned long PERIOD = 30000;
const unsigned long PERIOD_PZEM = 5000;

volatile bool isOnline = false;

TaskHandle_t inputTask;
TaskHandle_t loggerTask;

WiFiClient espClient;
PubSubClient client(espClient);

PZEM004Tv30 pzems[] ={ {Serial2, 16, 17, 1}, {Serial2, 16, 17, 2}, {Serial2, 16, 17, 3}};

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

void logPzem(PZEM004Tv30& pzem) {
    Serial.print("PZEM:");
    Serial.print(pzem.readAddress(), HEX);
    Serial.print(" ");

    // Read the data from the sensor
    float voltage = pzem.voltage();
    float current = pzem.current();
    float power = pzem.power();
    float energy = pzem.energy();
    float frequency = pzem.frequency();
    float pf = pzem.pf();

    // Check if the data is valid
    if(isnan(voltage)){
        Serial.println("Error reading voltage");
    } else if (isnan(current)) {
        Serial.println("Error reading current");
    } else if (isnan(power)) {
        Serial.println("Error reading power");
    } else if (isnan(energy)) {
        Serial.println("Error reading energy");
    } else if (isnan(frequency)) {
        Serial.println("Error reading frequency");
    } else if (isnan(pf)) {
        Serial.println("Error reading power factor");
    } else {
        Serial.print("Voltage: ");      Serial.print(voltage);      Serial.print("V, ");
        Serial.print("Current: ");      Serial.print(current);      Serial.print("A, ");
        Serial.print("Power: ");        Serial.print(power);        Serial.print("W, ");
        Serial.print("Energy: ");       Serial.print(energy,3);     Serial.print("kWh, ");
        Serial.print("Frequency: ");    Serial.print(frequency, 1); Serial.print("Hz, ");
        Serial.print("PF: ");           Serial.println(pf);
    }
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
		Serial.println();

//		for (auto& pzem: pzems) {
//			logPzem(pzem);
//		}
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

void setup() {
	EEPROM.begin(RELAYS);
	int pos = 0;
	for (auto i: relays) {
		pinMode(i, OUTPUT);
		digitalWrite(i, EEPROM.read(pos++));
	}

	Serial.begin(115200);

	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
	xTaskCreate(readInputs, "input", 4096, nullptr, 1, &inputTask);

	gemha::initWiFi(otaHostname);

	espClient.setTimeout(1);
	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
	client.setSocketTimeout(3);
}

bool publishPzems() {
	bool ret = true;
	char topic[sizeof(TOPIC_PREFIX TOPIC_PZEM "123/frequency")];
	char data[16];
	for (int i = 0; i < sizeof(pzems) / sizeof(pzems[0]); i++) {
		auto& pzem = pzems[i];
		float val;
#define PZEM_PUBLISH(name) \
		val = pzem.name(); \
		if (!isnan(val)) { \
			sprintf(topic, TOPIC_PREFIX TOPIC_PZEM "%d/" #name, i); \
			sprintf(data, "%.3f", pzem.name()); \
			ret &= client.publish(topic, data); \
		}

		PZEM_PUBLISH(voltage)
		PZEM_PUBLISH(current)
		PZEM_PUBLISH(power)
		PZEM_PUBLISH(energy)
		PZEM_PUBLISH(frequency)
		PZEM_PUBLISH(pf)
	}

	return ret;
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
		}
		if (!publish(force || pereodicForce))
			client.disconnect();

		static unsigned long lastPzem;
		if (now - lastPzem > PERIOD_PZEM) {
			lastPzem = now;
			if (!publishPzems())
				client.disconnect();
		}
	}
	force = !isOnline;

	delay(1);
}
