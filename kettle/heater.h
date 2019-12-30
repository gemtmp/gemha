#include <OneWire.h>
#include <DallasTemperature.h>

#include <functional>

class Heater {
public:
	typedef std::function<void(bool)> THeatFunction;
	typedef std::function<bool()> TNoWaterFunction;

	Heater(uint8_t owPin, const DeviceAddress& sensorAddress, THeatFunction heat, TNoWaterFunction noWater);

	void begin();
	void stop();

	void reboil() {
		reboiling =true;
	}

	void setTargetTemperature(float t) {
		targetTemperature = t;
	}
	float getTargetTemprature() {
		return targetTemperature;
	}
	float getTemperature();

	void log();
private:
	void trackTemp();
	static void trackTemp(void* ptr);
	void readTemperature();
private:
	TaskHandle_t trackTask = nullptr;
	THeatFunction heat;
	TNoWaterFunction noWaterFunc;

	OneWire oneWire;
	DallasTemperature oneWireTemp;
	const DeviceAddress& sensorAddress;

	bool run = false;
	bool reboiling = false;
	bool noWater = false;
	float targetTemperature = 90;
	float currentTemperature = 0;
	float currentOWTemperature = 0;
};
