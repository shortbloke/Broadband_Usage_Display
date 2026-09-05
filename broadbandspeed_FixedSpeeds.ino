#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <millisDelay.h>          // https://www.forward.com.au/pfod/ArduinoProgramming/TimingDelaysInArduino.html#using
#include <Arduino_SNMP_Manager.h> // https://github.com/shortbloke/Arduino_SNMP_Manager
#include <MAX7219_Digits.h>       // https://github.com/Mottramlabs/MAX7219-7-Segment-Driver
#if __has_include("project_secrets.h")
#include "project_secrets.h"
#else
#include "project_secrets.h.example"
#endif

//************************************
//* Your WiFi info                   *
//************************************
//************************************

//************************************
//* SNMP Device Info                 *
//************************************
const SNMPVersion snmpVersion = SNMPVersion::Version2c;
IPAddress router;
// These are standard IF-MIB names written as numeric OID bases. The code adds
// routerInterfaceIndex below; normally users only change that setting.
const uint32_t downSpeed = 1000000000;                 // 1,000,000,000 bits/s = 1 Gbps
const uint32_t upSpeed = 100000000;                    // 100,000,000 bits/s = 100 Mbps
const char *oidIfHcInOctetsBase = ".1.3.6.1.2.1.31.1.1.1.6.";    // IF-MIB::ifHCInOctets
const char *oidIfHcOutOctetsBase = ".1.3.6.1.2.1.31.1.1.1.10."; // IF-MIB::ifHCOutOctets
char oidInOctets[48];
char oidOutOctets[48];
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";                    // SNMPv2-MIB::sysUpTime
//************************************

//************************************
//* Settings                         *
//************************************
const int fastPollInterval = 1000; // Perform initial fast polling to populate data.
const int pollInterval = 2000;    // delay in milliseconds (15000 = 15 seconds)
const int deltaTimeError = 2;      // Permitted difference between poll interval and calculated uptime
//************************************

//************************************
//* Initialise                       *
//************************************
// Variables
uint64_t inOctets = 0;
uint64_t outOctets = 0;
uint32_t uptime = 0;
uint32_t lastUptime = 0;
uint32_t lastInOctetsUptime = 0;
uint32_t lastOutOctetsUptime = 0;

float bandwidthInUtilPct = 0;
float bandwidthOutUtilPct = 0;
uint64_t lastInOctets = 0;
uint64_t lastOutOctets = 0;
// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and receive packets
SNMPManager snmp = SNMPManager(community);             // Starts an SNMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SNMPGet instance to send requests

// Blank callback pointer for each OID
ValueCallback *callbackInOctets;
ValueCallback *callbackOutOctets;
ValueCallback *callbackUptime;

// millisDelay timer objects
millisDelay fastPollDelay;
millisDelay pollDelay;
bool isFastPolling = true;

// MottramLabs 4 Digit Display With Bar Graph - Wemos Version - https://www.mottramlabs.com/display_products.html
MAX7219_Digit Display(15); // Make an instance of MAX7219_Digit called My_Display and set CS pin
int Bar_1[17]{0, 128, 192, 224, 240, 248, 252, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255};
int Bar_2[17]{0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 192, 224, 240, 248, 252, 254, 255};
int indicators[5]{0, 128, 192, 224, 240};
//************************************

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  WiFi.softAPdisconnect(true); // Disable broadcast of local AP
  Serial.println("");
  // Wait for connection
  Display.Begin();
  Display.Brightness(8);
  int dp = 8;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    // Scroll the decimal point scrolls to left whilst connecting
    Display.Display_Text(1, 0x10, 0x10, 0x10, 0x10, dp);
    if (dp == 1)
    {
      dp = 8;
    }
    else
    {
      dp = dp >> 1;
    }
  }
  Display.Clear();
  Serial.println("");
  Serial.print("Connected to ");
  Serial.print(ssid);
  Serial.print(" with IP address: ");
  Serial.println(WiFi.localIP());

  if (!router.fromString(routerAddress))
  {
    Serial.print("Invalid router IP address: ");
    Serial.println(routerAddress);
    return;
  }

  snprintf(oidInOctets, sizeof(oidInOctets), "%s%u", oidIfHcInOctetsBase, routerInterfaceIndex);
  snprintf(oidOutOctets, sizeof(oidOutOctets), "%s%u", oidIfHcOutOctetsBase, routerInterfaceIndex);

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Get callbacks from creating a handler for each of the OID
  callbackInOctets = snmp.addCounter64Handler(router, oidInOctets, &inOctets);
  callbackOutOctets = snmp.addCounter64Handler(router, oidOutOctets, &outOctets);
  callbackUptime = snmp.addTimestampHandler(router, oidUptime, &uptime);

  fastPollDelay.start(fastPollInterval); // Start off fast polling to get data more quickly.
}

void loop()
{
  snmp.loop();
  if (fastPollDelay.justFinished() || (!isFastPolling && pollDelay.justFinished()))
  {
    getSNMP();
    resetDelayTimer();
  }

  if (isValidPoll())
  {
    if (isFastPolling && lastOutOctets != 0 && lastInOctets != 0)
    {
      stopFastPolling(); // Stop fast polling after good valid poll has occurred and data for current and last stored.
    }
    if (inOctets != lastInOctets)
    {
      if (lastInOctets != 0)
      {
        bandwidthInUtilPct = calculateBandwidth(inOctets, lastInOctets, downSpeed, uptime, lastInOctetsUptime);
      }
      lastInOctets = inOctets;
      lastInOctetsUptime = uptime;
    }
    if (outOctets != lastOutOctets)
    {
      if (lastOutOctets != 0)
      {
        bandwidthOutUtilPct = calculateBandwidth(outOctets, lastOutOctets, upSpeed, uptime, lastOutOctetsUptime);
      }
      lastOutOctets = outOctets;
      lastOutOctetsUptime = uptime;
    }
    updateDisplay();
  }
}

void updateDisplay()
{
  // Download Utilisation
  Display.Display_Value(1, bandwidthInUtilPct, 1, 0x00); // Display % util on 7 segment displays
  int barPercent = 0;
  if (bandwidthInUtilPct > 100)
  {
    barPercent = map(100, 0, 100, 0, 16); // As using estimated bandwidth, keep max to 100 to avoid unexpected bar led displays
  }
  else
  {
    barPercent = map(int(bandwidthInUtilPct), 0, 100, 0, 16); // Map % util on to the Green->Amber->Red LEDs
  }
  Display.MAX7219_Write(5, Bar_1[barPercent]);
  Display.MAX7219_Write(6, Bar_2[barPercent]);
  // Upload Utilisation
  int indicatorPercent = 0;
  if (bandwidthOutUtilPct > 100)
  {
    indicatorPercent = map(100, 0, 100, 0, 4); // As using estimated bandwidth, keep max to 100 to avoid unexpected bar led displays
  }
  else
  {
    indicatorPercent = map(int(bandwidthOutUtilPct), 0, 100, 0, 4); // Map % util on to the indicator strip
  }
  Display.MAX7219_Write(7, indicators[indicatorPercent]);
}

void resetDelayTimer()
{
  if (isFastPolling)
  {
    fastPollDelay.restart();
  }
  else
  {
    pollDelay.repeat();
  }
}

void stopFastPolling()
{
  fastPollDelay.stop();          // Stop fastPollDelay
  pollDelay.start(pollInterval); // Start pollDelay
  isFastPolling = false;         // Clear fast polling flag
}

bool isValidPoll()
{
  bool retVal = true;
  if (uptime == lastUptime)
  {
    // Serial.println("isValidPoll - False: uptime unchanged between polls");
    retVal = false;
  }
  else if (uptime < lastUptime)
  {
    Serial.println("isValidPoll - False: uptime is less than last uptime, rebooted?");
    retVal = false;
  }
  else if (uptime > 0 && lastUptime > 0)
  {
    if (isFastPolling && ((uptime - lastUptime + deltaTimeError) < (fastPollInterval / 10)))
    {
      Serial.print("isValidPoll - False: (Fast Poll) Implausible sample period: ");
      Serial.print(uptime - lastUptime);
      Serial.print(" (Uptime: ");
      Serial.print(uptime);
      Serial.print(" lastUptime: ");
      Serial.print(lastUptime);
      Serial.println(")");
      retVal = false;
    }
    else if (!isFastPolling && ((uptime - lastUptime + deltaTimeError) < (pollInterval / 10)))
    {
      Serial.print("isValidPoll - False: (Regular Poll) Implausible sample period: ");
      Serial.print(uptime - lastUptime);
      Serial.print(" (Uptime: ");
      Serial.print(uptime);
      Serial.print(" lastUptime: ");
      Serial.print(lastUptime);
      Serial.println(")");
      retVal = false;
    }
  }
  if (uptime > 0 && lastUptime == 0)
  {
    Serial.println("isValidPoll - False: lastUptime still zero, update with current uptime");
    retVal = false;
  }
  if (uptime > 0)
  {
    lastUptime = uptime;
  }
  return retVal;
}

float calculateBandwidth(uint64_t current, uint64_t last, uint32_t speed, uint32_t currentTime, uint32_t lastTime)
{
  const double deltaTimeSec = (double)(currentTime - lastTime) / 100.0;
  const float bandwidth = (float)((double)(current - last) * 8.0 * 100.0 /
                                  ((double)speed * deltaTimeSec));
  Serial.print("current: ");
  Serial.print(current);
  Serial.print(" - last: ");
  Serial.print(last);
  Serial.print(" - speed: ");
  Serial.print(speed);
  Serial.print(" - bandwidth %: ");
  Serial.print(bandwidth);
  Serial.print(" - deltaTimeSec: ");
  Serial.println(deltaTimeSec);
  return bandwidth;
}

void getSNMP()
{
  snmpRequest.cancelPendingRequests();

  // Build a SNMP get-request, add multiple OID to a single request
  bool requestReady = true;
  requestReady = snmpRequest.addOIDPointer(callbackInOctets) && requestReady;
  requestReady = snmpRequest.addOIDPointer(callbackOutOctets) && requestReady;
  requestReady = snmpRequest.addOIDPointer(callbackUptime) && requestReady;
  if (!requestReady)
  {
    snmpRequest.clearOIDList();
    return;
  }

  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  if (!snmpRequest.sendTo(router))
  {
    snmpRequest.cancelPendingRequests();
  }

  snmpRequest.clearOIDList();
}
