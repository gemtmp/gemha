#pragma once

#include "Arduino.h"

class Button {
public:
	Button(uint8_t pin) : pin(pin) {
		pinMode(pin, INPUT);
	}

	void check() {
		bool v = digitalRead(pin);
		if (v == val) {
			counter = 0;
			return;
		}
		counter++;
		if (counter >= delay) {
			val = v;
		}
	}
	operator bool() const {
		return val;
	}
	bool value() const {
		return val;
	}
private:
	const uint8_t pin;
	bool val = false; // TODO volatile ?
	uint8_t counter = 0;
	const static uint8_t delay = 5;
};
