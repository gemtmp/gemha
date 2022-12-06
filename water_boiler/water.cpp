#include "Arduino.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TM1637Display.h>
#include <esp_task_wdt.h>

#define DEBUG

#include "../common/temperature.h"
#include "../common/wifi.h"

#include "../config/gemconfig.h"

static const int RELAYS = 4;

static const uint8_t oneWirePin = 26;

static const uint8_t relays[RELAYS] = { 27, 25, 17, 16 };

static const uint8_t dipslayClk =18;
static const uint8_t dipslayDio =19;

const char *otaHostname = "waterboiler.gem";

#define TOPIC_PREFIX "house/water/boiler/"
#define TOPIC_RELAY "relay/"
#define TOPIC_BRIGHTNESS "brightness"

#ifdef DEBUG
const unsigned long PERIOD = 5000;
#else
const unsigned long PERIOD = 30000;
#endif

volatile bool isOnline = false;

TaskHandle_t displayTask;
TaskHandle_t loggerTask;
TaskHandle_t tempTask;

WiFiClient espClient;
PubSubClient client(espClient);

OneWire oneWire(oneWirePin);
gemha::Temperature temperatures(TOPIC_PREFIX "temp/", &oneWire, &client);
SemaphoreHandle_t  tempReadMutex;
SemaphoreHandle_t  tempBinaryMutex;

TM1637Display display(dipslayClk, dipslayDio);

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
	if (strncmp(topic, TOPIC_PREFIX TOPIC_BRIGHTNESS, sizeof(TOPIC_PREFIX TOPIC_BRIGHTNESS) - 1) == 0) {
		int value = getValue(payload, length);
		if (value < 0 || value > 7)
			return;
		display.setBrightness(value);
		return;
	}

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
		Serial.print(" Relays: ");
		for (auto i: relays) {
			Serial.print(digitalRead(i) ? " 1": " 0");
		}
		Serial.print(" Temps: ");
		Serial.println(temperatures.addressCount);

		delay(1000);
	}
}

void displayFunc(void *p) {
	static uint8_t i = 0;
	for (;;) {
		float val = 888;

		if (xSemaphoreTake(tempReadMutex, portMAX_DELAY) == pdTRUE) {
			if (i >= temperatures.addressCount) {
				i = 0;
			}
			val = temperatures.devices[i].val;
			xSemaphoreGive(tempReadMutex);
			i++;
		}
		uint8_t dots = 0;
		for (auto i: relays) {
			dots >>= 1;
			dots |= digitalRead(i) ? 0 : 0x80;
		}
		display.showNumberDecEx(i, dots, true, 1, 0);
		dots <<= 1;
		display.showNumberDecEx(int(val), dots, false, 3, 1);
		delay(1007);
	}
}

void readTemperatures(void *p) {
	for (;;) {
		if (xSemaphoreTake(tempBinaryMutex, portMAX_DELAY) == pdTRUE) {
			delay(PERIOD / 5);
			if (xSemaphoreTake(tempReadMutex, portMAX_DELAY) == pdTRUE) {
				temperatures.search();
				if (temperatures.addressCount == 0)
					temperatures.search();
				xSemaphoreGive(tempReadMutex);
			}
			temperatures.startMeasure();
			temperatures.read();
		}
		delay(PERIOD / 4);
	}
}

void setup() {
	for (auto i: relays) {
		pinMode(i, OUTPUT);
		digitalWrite(i, HIGH);
	}
	display.showNumberDec(0, true);

#ifdef DEBUG
	Serial.begin(115200);

	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
#endif

	tempReadMutex = xSemaphoreCreateMutex();
	tempBinaryMutex = xSemaphoreCreateBinary();

	xTaskCreate(readTemperatures, "temp", 4096, nullptr, 5, &tempTask);

	gemha::initWiFi(otaHostname);

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);

	temperatures.start();
	temperatures.readAll();

	display.setBrightness(3);
	xTaskCreate(displayFunc, "display", 4096, nullptr, 1, &displayTask);
}

bool publish(bool force) {
	bool ret = true;
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
	isOnline = gemha::connectMqtt(client, otaHostname, TOPIC_PREFIX "#");

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

