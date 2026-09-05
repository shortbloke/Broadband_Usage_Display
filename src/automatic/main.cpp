#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>

void getSNMP();
void resetDelayTimer();
void stopFastPolling();
bool isValidPoll();
float calculateBandwidth(unsigned int current, unsigned int last, unsigned int speed, int currentTime, int lastTime);
void updateDisplay();

#include "../../broadbandspeed.ino"