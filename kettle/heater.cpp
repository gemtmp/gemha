#include <OneWire.h>
#include <DallasTemperature.h>

#include "heater.h"

namespace temp {

static int rawTemp;

struct Measure {
	const int t;
	const int v;
};
static const Measure points[] = { { 0, 2800 }, //
		{ 8, 2710 }, //
		{ 10, 2660 }, //
		{ 20, 2260 }, //
		{ 24, 2120 }, //
		{ 30, 1914 }, //
		{ 35, 1809 }, //
		{ 40, 1706 }, //
		{ 45, 1602 }, //
		{ 50, 1519 }, //
		{ 60, 1420 }, //
		{ 65, 1420 }, //
		{ 70, 730 }, //
		{ 75, 587 }, //
		{ 80, 527 }, //
		{ 90, 360 }, //
		{ 100, 235 }, //
//
		{ 125, 0 }, { 126, -1 } };

float readTemperature() {
	static int acc = 0;
	int v = analogRead(A0);
	rawTemp = v;

	if (acc == 0)
		acc = v;
	acc = (v + acc * 7) / 8;
	v = acc;

	int i = 0;
	for (auto const &p : points) {
		if (p.v < v) {
			break;
		}
		i++;
	}
	if (i == 0)
		i++;
	auto l = points[i - 1];
	auto h = points[i];

	return (h.t - l.t) * float(v) / (h.v - l.v) + h.t
			- (h.t - l.t) * h.v / (h.v - l.v);
}

} // namespace temp

Heater::Heater(uint8_t owPin, const DeviceAddress &sensorAddress,
		THeatFunction heat, TNoWaterFunction noWater) :
		oneWire(owPin), oneWireTemp(&oneWire), sensorAddress(sensorAddress), heat(
				heat), noWaterFunc(noWater) {
}

void Heater::begin() {
	run = true;
	oneWireTemp.begin();
	xTaskCreate(trackTemp, "trackTemp", 2048, this, 1, &trackTask);
}

void Heater::trackTemp(void *ptr) {
	static_cast<Heater*>(ptr)->trackTemp();
}

void Heater::stop() {
	run = false;
	vTaskDelete(trackTask);
}

void Heater::log() {
	Serial.printf("Target: %.2f Reboil: %d NoWater: %d Curr: %6.2f owCurr: %6.2f Raw: %d\n\r",
			targetTemperature, reboiling, noWater, currentTemperature,
			currentOWTemperature, temp::rawTemp);
}

float Heater::getTemperature() {
	volatile auto t = currentOWTemperature;
	return t != -127 ? t : currentTemperature;
}

void Heater::readTemperature() {
	oneWireTemp.requestTemperatures();
	currentTemperature = temp::readTemperature();

	{
		auto v = oneWireTemp.getTempC(sensorAddress);
		currentOWTemperature = v == 85.0 ? -127 : v;
	}
}

void Heater::trackTemp() {

	while (run) {
		readTemperature();

		auto t =
				currentOWTemperature != -127 ?
						currentOWTemperature : round(currentTemperature);

		if (t > 107 || t - currentTemperature > 40) {
			heat(false);
			reboiling = false;
			noWater = true;
			while (noWaterFunc()) {
				readTemperature();
			}
			noWater = false;
			continue;
		}
		int target = reboiling ? 100 : targetTemperature;

		if (t < target - 1) {
			heat(true);
		} else {
			heat(false);
			reboiling = false;
		}
		delay(100);
	}
}

