#pragma once

#include "Arduino.h"

namespace gemha {

class Button {
public:
	Button(uint8_t pin, bool pullup = true, bool inverse = false) : pin(pin), inverse(inverse) {
		pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
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
		return value();
	}
	bool value() const {
		return inverse ? !val :val;
	}
private:
	const uint8_t pin;
	const bool inverse;
	bool val = false; // TODO volatile ?
	uint8_t counter = 0;
	const static uint8_t delay = 5;
};

} // namespace gemha
