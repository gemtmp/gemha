#include "Arduino.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define DEBUG

#include "../common/button.h"
#include "../common/temperature.h"

#include "../config/gemconfig.h"

static const int INPUTS = 2;
static const int RELAYS = 4;

static const uint8_t oneWirePin = 26;

static const uint8_t relays[RELAYS] = { 22, 21, 17, 16 };
gemha::Button inputs[INPUTS] = {27, 25};

uint8_t mapping[INPUTS][1] = {{0}, {1}};

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
SemaphoreHandle_t  tempReadMutex;
SemaphoreHandle_t  tempBinaryMutex;

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
	for (;;) {
		if (xSemaphoreTake(tempBinaryMutex, portMAX_DELAY) == pdTRUE) {

			xSemaphoreTake(tempReadMutex, portMAX_DELAY);
			temperatures.read();
			xSemaphoreGive(tempReadMutex);
		}
	}
}

void setup() {
	for (auto i: relays) {
		pinMode(i, OUTPUT);
		digitalWrite(i, HIGH);
	}

#ifdef DEBUG
	Serial.begin(115200);

	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
#endif

	tempReadMutex = xSemaphoreCreateMutex();
	tempBinaryMutex = xSemaphoreCreateBinary();

	xTaskCreate(readInputs, "input", 4096, nullptr, 1, &inputTask);
	xTaskCreate(readTemperatures, "temp", 4096, nullptr, 1, &tempTask);



	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, passwd);
	WiFi.setHostname(otaHostname);
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef DEBUG
		Serial.println("Connection Failed! Rebooting...");
#endif
		delay(1000);
		ESP.restart();
	}
#ifdef DEBUG
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
#endif
	randomSeed(micros());

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);

	temperatures.start();
	temperatures.read();
}

bool connectMqtt() {
	if (client.connected())
		return true;
	String clientId = "LightClient-";
	clientId += String(random(0xffff), HEX);
	if (!client.connect(clientId.c_str())) {
#ifdef DEBUG
		Serial.print("mqtt connect failed, rc=");
		Serial.println(client.state());
#endif
		return false;
	}
	return client.subscribe( TOPIC_PREFIX TOPIC_RELAY "#");
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
	ArduinoOTA.handle();
	client.loop();
	isOnline = connectMqtt();

	if (isOnline) {
		static unsigned long last;
		unsigned long now = millis();
		bool pereodicForce = false;
		if (now - last > PERIOD) {
			last = now;
			pereodicForce = true;

			if (xSemaphoreTake(tempReadMutex, 50 * portTICK_PERIOD_MS) == pdTRUE) {
				temperatures.publish();
				xSemaphoreGive(tempReadMutex);
			}
			xSemaphoreGive(tempBinaryMutex);
		}
		publish(force || pereodicForce);
	}
	force = !isOnline;

	delay(1);
}

