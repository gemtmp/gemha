#pragma once

#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>

#include "../config/gemconfig.h"

namespace gemha {

void initWiFi(const char* hostname) {
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, passwd);
	WiFi.setHostname(hostname);
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef DEBUG
		Serial.println("Connection Failed! Rebooting...");
#endif
		delay(1000);
		ESP.restart();
	}
#ifdef DEBUG
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
#endif
	randomSeed(micros());

	ArduinoOTA.onProgress([] (int, int) {esp_task_wdt_reset();});
	ArduinoOTA.setPassword(otaPassword);
	ArduinoOTA.setHostname(hostname);
	ArduinoOTA.begin();

	esp_task_wdt_init(5, true);
	esp_task_wdt_add(NULL);
}

bool connectMqtt(PubSubClient& client, const char* hostname, const char* topic = nullptr) {
	if (client.connected())
		return true;
	String clientId = hostname;
	clientId += String(random(0xffff), HEX);
	if (!client.connect(clientId.c_str())) {
#ifdef DEBUG
		Serial.print("mqtt connect failed, rc=");
		Serial.println(client.state());
#endif
		return false;
	}
	if (topic != nullptr)
		return client.subscribe(topic);

	return true;
}

} // namespace gemha
