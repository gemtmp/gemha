#pragma once

#include "Arduino.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

namespace gemha {

class Temperature {
public:
	Temperature(const char *topic, OneWire *oneWire, PubSubClient *client) :
			topic(topic), oneWire(oneWire), sensors(oneWire), client(client) {
		topicLen = strlen(topic);
	}

	void start() {
		sensors.begin();
	}

	void readAll() {
		search();
		startMeasure();
		read();
	}

	void search() {
		oneWire->reset();
		oneWire->reset_search();

		uint8_t count = 0;
		while (addressCount < ADDRESS_MAX
				&& oneWire->search(devices[count].addr)) {
			DeviceAddress &addr = devices[count].addr;
			if (sensors.validAddress(addr)) {
				if (sensors.validFamily(addr)) {
					count++;
				}
			}
		}
		for (int i = count; i < addressCount; i++) {
			devices[i].val = 888;
		}
		addressCount = count;
	}

	void startMeasure() {
		sensors.requestTemperatures();
	}

	void read() {
		for (auto i = 0; i < addressCount; i++) {
			devices[i].val = sensors.getTempC(devices[i].addr);
		}
	}

	void printAddr(DeviceAddress addr) {
		const char *digits("0123456789ABCDEF");
		for (auto i = 0; i < 8; i++) {
			Serial.print(digits[addr[i] >> 4]);
			Serial.print(digits[addr[i] & 15]);
		}
	}

	void publish() {
		for (auto i = 0; i < addressCount; i++) {
			auto &addr = devices[i].addr;
			auto val = devices[i].val;
#ifdef DEBUG
			printAddr(addr);
			Serial.print("=");
			Serial.print(val);
			Serial.println("ºC");
#endif
			if (val == 85.0 || val == -127.0)
				continue;
			char buf[topicLen + 32];
			sprintf(buf, "%s%02x%02x%02x%02x%02x%02x%02x%02x", topic, addr[0],
					addr[1], addr[2], addr[3], addr[4], addr[5], addr[6],
					addr[7]);
			char msg[16];
			snprintf(msg, sizeof(msg), "%.1f", val);

			client->publish(buf, msg);
		}
	}

	const char *topic;
	uint8_t topicLen;

	OneWire *oneWire;
	DallasTemperature sensors;
	PubSubClient *client;

	static const uint8_t ADDRESS_MAX = 8;
	uint8_t addressCount = 0;
	struct Device {
		DeviceAddress addr;
		float val;
	};
	Device devices[ADDRESS_MAX];
};

} // namespace gemha
