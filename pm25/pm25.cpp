
#include <Arduino.h>
#include <PubSubClient.h>
#include <Adafruit_PM25AQI.h>

#define DEBUG

#include "../common/wifi.h"

#include "../config/gemconfig.h"

const char *otaHostname = "pm25.gem";

#define TOPIC_PREFIX "house/sensor/4/"

const int ResetPin = 4;
const int SetPin = 2;

TaskHandle_t loggerTask;
TaskHandle_t readerTask;

WiFiClient espClient;
PubSubClient client(espClient);

Adafruit_PM25AQI aqi = Adafruit_PM25AQI();
PM25_AQI_Data data;
volatile bool dataValid = false;
uint16_t pm10, pm25, pm100;
volatile uint32_t counts = 0;

void logger(void *p) {
	uint32_t prevCounts = counts;
	for (;;) {
		delay(5000);

		if (prevCounts == counts)
			continue;
		prevCounts = counts;
		Serial.println(counts);
		if (!dataValid)
			continue;
		Serial.println(F("---------------------------------------"));
		Serial.println(F("Concentration Units (standard)"));
		Serial.println(F("---------------------------------------"));
		Serial.print(F("PM 1.0: "));
		Serial.print(data.pm10_standard);
		Serial.print(F("\t\tPM 2.5: "));
		Serial.print(data.pm25_standard);
		Serial.print(F("\t\tPM 10: "));
		Serial.println(data.pm100_standard);
		Serial.println(F("Concentration Units (environmental)"));
		Serial.println(F("---------------------------------------"));
		Serial.print(F("PM 1.0: "));
		Serial.print(data.pm10_env);
		Serial.print(F("\t\tPM 2.5: "));
		Serial.print(data.pm25_env);
		Serial.print(F("\t\tPM 10: "));
		Serial.println(data.pm100_env);
		Serial.println(F("---------------------------------------"));
		Serial.print(F("Particles > 0.3um / 0.1L air:"));
		Serial.println(data.particles_03um);
		Serial.print(F("Particles > 0.5um / 0.1L air:"));
		Serial.println(data.particles_05um);
		Serial.print(F("Particles > 1.0um / 0.1L air:"));
		Serial.println(data.particles_10um);
		Serial.print(F("Particles > 2.5um / 0.1L air:"));
		Serial.println(data.particles_25um);
		Serial.print(F("Particles > 5.0um / 0.1L air:"));
		Serial.println(data.particles_50um);
		Serial.print(F("Particles > 10 um / 0.1L air:"));
		Serial.println(data.particles_100um);
	}
}

bool readPM() {
	dataValid = aqi.read(&data);
	if (!dataValid) {
		return false;
	}
	if (counts == 0) {
		pm10 = data.pm10_env;
		pm25 = data.pm25_env;
		pm100 = data.pm100_env;
	} else {
		pm10 = (pm10 * 3 + data.pm10_env) / 4;
		pm25 = (pm25 * 3 + data.pm25_env) / 4;
		pm100 = (pm100 * 3 + data.pm100_env) / 4;
	}
	counts++;
	return true;
}

void reader(void *p) {
	for (;;) {
		readPM();
		delay(200);
	}
}

void setup()
{

#ifdef DEBUG
	Serial.begin(115200);

	xTaskCreate(logger, "logger", 4096, nullptr, 1, &loggerTask);
#endif
	Serial2.begin(9600);
	aqi.begin_UART(&Serial2);

	pinMode(ResetPin, OUTPUT);
	pinMode(SetPin, OUTPUT);
	digitalWrite(ResetPin, 1);
	digitalWrite(SetPin, 1);

	xTaskCreate(reader, "reader", 4096, nullptr, 1, &readerTask);

	gemha::initWiFi(otaHostname);

	client.setServer(server, 1883);
}

void loop()
{
	ArduinoOTA.handle();
	client.loop();
	bool isOnline = gemha::connectMqtt(client, otaHostname);

	if (counts != 0) {
		char msg[16];
		snprintf(msg, sizeof(msg), "%d", pm10);
		client.publish(TOPIC_PREFIX "pm10", msg);
		snprintf(msg, sizeof(msg), "%d", pm25);
		client.publish(TOPIC_PREFIX "pm25", msg);
		snprintf(msg, sizeof(msg), "%d", pm100);
		client.publish(TOPIC_PREFIX "pm100", msg);
	}

	delay(5000);
}
