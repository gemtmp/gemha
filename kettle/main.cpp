#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#include <DallasTemperature.h>

#include "../config/gemconfig.h"
#include "heater.h"

const char *otaHostname = "kettle.gem";

static const uint8_t LedBlue = 14;
static const uint8_t LedRed = 13;
static const uint8_t Reboil = 33;
static const uint8_t Relay = 16;
static const uint8_t OneWirePin = 4;

DeviceAddress tempAddr = { 0x28, 0xFF, 0x96, 0xD3, 0xB5, 0x16, 0x03, 0xC7 };

#define TOPIC "house/kettle"
const char *topicTarget = TOPIC"/target";
const char *topicCurrent = TOPIC"/current";
const long PERIOD = 5000;

TaskHandle_t blinkTask;
TaskHandle_t loggerTask;

int count = 0;

Heater heater(OneWirePin, tempAddr, [](bool on) {
	digitalWrite(LedRed, on ? LOW : HIGH);
	digitalWrite(Relay, on ? HIGH : LOW);
}, []() {
	digitalWrite(LedRed, LOW);
	delay(500);
	digitalWrite(LedRed, HIGH);
	delay(500);

	return count < 40;
});

WiFiClient espClient;
PubSubClient client(espClient);

struct Param {
	int delay;
	uint8_t pin;
	uint8_t channel;
};

Param pblue = { 25, LedBlue, 0 };

void blink(void *ptr) {
	const Param &p = *static_cast<Param*>(ptr);
	ledcSetup(p.channel, 4096, 8);
	ledcAttachPin(p.pin, p.channel);

	for (;;) {
		for (int dutyCycle = 0; dutyCycle <= 255; dutyCycle++) {
			ledcWrite(p.channel, dutyCycle);
			delay(p.delay);
		}
		for (int dutyCycle = 255; dutyCycle >= 0; dutyCycle--) {
			ledcWrite(p.channel, dutyCycle);
			delay(p.delay);
		}
	}
}

void logger(void *p) {
	for (;;) {
		heater.log();
		Serial.print("Count: ");
		Serial.println(count);
		delay(1000);
	}
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}

	char buf[16];
	memset(buf, 0, sizeof(buf));
	if (length >= sizeof(buf))
		return;
	memcpy(buf, payload, length);

	char *end;
	float value = strtof(buf, &end);
	if (end == buf)
		return;

	Serial.print(", value: ");
	Serial.println(value);
	auto newValue = value;
	if (value == 100.0) {
		heater.reboil();
	}
	if (value < 60) {
		value = 60;
	} else if (value > 90) {
		value = 90;
	}
	heater.setTargetTemperature(value);
	if (value != newValue) {
		char msg[16];
		snprintf(msg, sizeof(msg), "%.2f", value);
		client.publish(topicTarget, msg);
	}
}

void setup() {
	Serial.begin(115200);

	pinMode(LedRed, OUTPUT);
	pinMode(Relay, OUTPUT);
	pinMode(Reboil, INPUT_PULLUP);
	digitalWrite(Relay, LOW);

	heater.begin();

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, passwd);
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {
		Serial.println("Connection Failed! Rebooting...");
		delay(5000);
		ESP.restart();
	}
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
	randomSeed(micros());

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);

	xTaskCreate(blink, "blink", 2048, &pblue, 1, &blinkTask);
	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
}

bool publish(float value) {
	if (!client.connected()) {
		String clientId = "KettleClient-";
		clientId += String(random(0xffff), HEX);
		if (client.connect(clientId.c_str())) {
			client.subscribe(topicTarget);
		} else {
			Serial.print("mqtt connect failed, rc=");
			Serial.println(client.state());
			return false;
		}
	}

	char msg[16];
	snprintf(msg, sizeof(msg), "%.2f", value);
	return client.publish(topicCurrent, msg);
}

long lastRead = -PERIOD;
void loop() {
	client.loop();
	ArduinoOTA.handle();
	auto v = digitalRead(Reboil);
	count = v ? 0 : count + 1;
	if (count > 1) {
		heater.reboil();
	}
	if (count > 40)
		count = 40;
	long now = millis();
	if (now - lastRead > PERIOD) {
		lastRead = now;
		publish(heater.getTemperature());
	}

	delay(50);
}
