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

	void log(Print& p) {
		p.print(" Temperatures: ");
		p.print(addressCount);
	}

	void search() {
		oneWire->reset();
		oneWire->reset_search();

		uint8_t prevCount = addressCount;
		uint8_t count = 0;
		DeviceAddress addr;
		while (count < ADDRESS_MAX
				&& oneWire->search(addr)) {
			if (sensors.validAddress(addr)) {
				if (sensors.validFamily(addr)) {
					if (!std::equal(addr, addr + 8, devices[count].addr)) {
						addressCount = 0;
						std::copy(addr, addr + 8, devices[count].addr);
						devices[count].val = -127.0;
					}

					count++;
				}
			}
		}
		for (int i = count; i < prevCount; i++) {
			devices[i].val = -127.0;
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
			if (val == 85.0 || val < -120.0)
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
	volatile uint8_t addressCount = 0;
	struct Device {
		DeviceAddress addr;
		float val;
	};
	Device devices[ADDRESS_MAX];
};

} // namespace gemha
