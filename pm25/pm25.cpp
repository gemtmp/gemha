
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

WiFiClient espClient;
PubSubClient client(espClient);

Adafruit_PM25AQI aqi = Adafruit_PM25AQI();
PM25_AQI_Data data;
bool dataValid = false;

void logger(void *p) {
	for (;;) {
		delay(5000);

		if (!dataValid)
			continue;
		Serial.println();
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

	gemha::initWiFi(otaHostname);

	client.setServer(server, 1883);
}

void loop()
{
	ArduinoOTA.handle();
	client.loop();
	bool isOnline = gemha::connectMqtt(client, otaHostname);

	dataValid = aqi.read(&data);
	if (!dataValid) {
#ifdef DEBUG
		Serial.println("Could not read from AQI");
#endif
	} else {
		char msg[16];
		snprintf(msg, sizeof(msg), "%d", data.pm10_env);
		client.publish(TOPIC_PREFIX "pm10", msg);
		snprintf(msg, sizeof(msg), "%d", data.pm25_env);
		client.publish(TOPIC_PREFIX "pm25", msg);
		snprintf(msg, sizeof(msg), "%d", data.pm100_env);
		client.publish(TOPIC_PREFIX "pm100", msg);
	}

	delay(5000);
}
