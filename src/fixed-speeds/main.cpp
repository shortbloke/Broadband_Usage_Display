#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

void getSNMP();
void resetDelayTimer();
void stopFastPolling();
bool isValidPoll();
float calculateBandwidth(uint32_t current, uint32_t last, uint32_t speed, uint32_t currentTime, uint32_t lastTime);
void updateDisplay();

#include "../../broadbandspeed_FixedSpeeds.ino"