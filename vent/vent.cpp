#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "../config/gemconfig.h"

const char *otaHostname = "vent1.gem";

#define TOPIC_PREFIX "house/vent/"
#define TOPIC_VALVE "valve/"
#define TOPIC_RELAY "relay/"
#define TOPIC_TEMP  "temp/"

static const uint8_t wireSDA = D3;
static const uint8_t wireSCL = D2;
static const uint8_t oneWirePin = D4;

const long PERIOD = 5000; // period for temperature query
const uint8_t startValue = 140; // set servo PWM from this point
const uint8_t ValveCount = 12; // number of used servos

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
WiFiClient espClient;
PubSubClient client(espClient);

OneWire oneWire(oneWirePin);
DallasTemperature sensors(&oneWire);

const uint8_t ADDRESS_MAX = 8;
uint8_t addressCount;
DeviceAddress owAddress[ADDRESS_MAX];

void callbackMqtt(char *topic, byte *payload, unsigned int length);
void setValue(uint8_t channel, uint8_t value);
void readTemperatures();
void processRelay(int channel, int value);

void setup() {
	Serial.begin(115200);
	Serial.println("");

	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.begin(ssid, passwd);

	while (WiFi.status() != WL_CONNECTED) {
		delay(250);
		delay(250);
		Serial.print(".");
	}
	Serial.println("");

	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(otaHostname);
	ArduinoOTA.begin();

	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	Wire.begin(wireSDA, wireSCL);
	pwm.begin();
	pwm.setPWMFreq(50);

	for (int i = 0; i < 12; i++) {
		setValue(i, 1);
	}
	for (int i = 0; i < 4; i++) {
		processRelay(i, 0);
	}
	randomSeed(micros());
	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);

	sensors.begin();
}

long lastRead;

void loop() {
	client.loop();
	ArduinoOTA.handle();

	if (client.connected()) {
		long now = millis();
		if (now - lastRead > PERIOD) {
			lastRead = now;
			readTemperatures();
		}
	}

	if (!client.connected()) {
		String clientId = "VentClient-";
		clientId += String(random(0xffff), HEX);
		if (client.connect(clientId.c_str())) {
			client.subscribe(TOPIC_PREFIX "#");
		} else {
			Serial.print("mqtt connect failed, rc=");
			Serial.println(client.state());
		}
	}
}

void processValve(int channel, int value) {
	if (channel < 0 || channel > 11)
		return;
	if (value < 0 || value > 100)
		return;
	Serial.print("Set valve ");
	Serial.print(channel);
	Serial.print(" to ");
	Serial.println(value);

	setValue(channel, 200 - value * 2);
}

void processRelay(int channel, int value) {
	if (channel < 0 || channel > 4)
		return;
	if (value != 0 && value != 1)
		return;
	Serial.print("Set relay ");
	Serial.print(channel);
	Serial.print(" to ");
	Serial.println(value);
	pwm.setPin(12 + channel, value ? 0 : 4095);
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
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}
	Serial.println();

	if (strncmp(topic, TOPIC_PREFIX, sizeof(TOPIC_PREFIX) - 1) != 0)
		return;
	topic += sizeof(TOPIC_PREFIX) - 1;

	enum TopicType {
		VALVE, RELAY, TEMP
	} type;
	if (strncmp(topic, TOPIC_VALVE, sizeof(TOPIC_VALVE) - 1) == 0) {
		topic += sizeof(TOPIC_VALVE) - 1;
		type = VALVE;
	} else if (strncmp(topic, TOPIC_RELAY, sizeof(TOPIC_RELAY) - 1) == 0) {
		topic += sizeof(TOPIC_RELAY) - 1;
		type = RELAY;
	} else {
		return;
	}

	char *dummy;
	int channel = strtoul(topic, &dummy, 10);
	if (dummy == topic)
		return;

	int value = getValue(payload, length);
	if (value == -1)
		return;

	if (type == VALVE) {
		processValve(channel, value);
	} else if (type == RELAY) {
		processRelay(channel, value);
	}
}

uint8_t values[16];

void setValue(uint8_t channel, uint8_t value) {
	if (value == values[channel]) {
		return;
	}
	auto v = values[channel];
	int inc = v > value ? -1 : 1;
	for (; abs(v - value) > 80; v += inc * 8) {
		pwm.setPWM(channel, 0, startValue + v);
		delay(20);
	}
	for (; abs(v - value) > 5; v += inc * 4) {
		pwm.setPWM(channel, 0, startValue + v);
		delay(20);
	}
	pwm.setPWM(channel, 0, startValue + value);
	values[channel] = value;
}

void printAddr(DeviceAddress addr) {
	const char *digits("0123456789ABCDEF");
	for (auto i = 0; i < 8; i++) {
		Serial.print(digits[addr[i] >> 4]);
		Serial.print(digits[addr[i] & 15]);
	}
}

void readTemperatures() {
	oneWire.reset_search();

	addressCount = 0;
	while (addressCount < ADDRESS_MAX && oneWire.search(owAddress[addressCount])) {
		DeviceAddress &addr = owAddress[addressCount];
		if (sensors.validAddress(addr)) {
			if (sensors.validFamily(addr)) {
				addressCount++;
			}
		}
	}
	sensors.requestTemperatures();

	for (auto i = 0; i < addressCount; i++) {
		auto val = sensors.getTempC(owAddress[i]);
		auto addr = owAddress[i];
		printAddr(addr);
		Serial.print("=");
		Serial.print(val);
		Serial.println("ºC");
		if (val == 85.0 || val == -127.0)
			continue;
		char buf[sizeof(TOPIC_PREFIX TOPIC_TEMP) + 32];
		sprintf(buf, TOPIC_PREFIX TOPIC_TEMP "%02x%02x%02x%02x%02x%02x%02x%02x",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
		char msg[16];
		snprintf(msg, sizeof(msg), "%.1f", val);

		client.publish(buf, msg);
	}
}
