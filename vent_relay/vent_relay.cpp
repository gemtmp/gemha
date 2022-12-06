#include "Arduino.h"

//#define DEBUG

#include "../common/wifi.h"
#include "../config/gemconfig.h"

static const int RELAYS = 2;
static const uint8_t relays[RELAYS] = { D1, D2 };

const char *otaHostname = "vent1r.gem";

#define TOPIC_PREFIX "house/vent1/"
#define TOPIC_RELAY "relay/"

WiFiClient espClient;
PubSubClient client(espClient);

#ifdef DEBUG
const unsigned long PERIOD = 5000;
#else
const unsigned long PERIOD = 30000;
#endif

volatile bool isOnline = false;

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
void setup()
{
	for (auto i: relays) {
		pinMode(i, OUTPUT);
		digitalWrite(i, HIGH);
	}

#ifdef DEBUG
	Serial.begin(115200);
#endif

	gemha::initWiFi(otaHostname);

	client.setServer(server, 1883);
	client.setCallback(callbackMqtt);
}

void loop()
{
	static bool force = true;
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
		publish(force || pereodicForce);
	}
	force = !isOnline;

	delay(1);
}
