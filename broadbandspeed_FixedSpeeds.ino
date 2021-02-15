#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>    // For Posting data to web logging service
#include <millisDelay.h>          // https://www.forward.com.au/pfod/ArduinoProgramming/TimingDelaysInArduino.html#using
#include <Arduino_SNMP_Manager.h> // https://github.com/shortbloke/Arduino_SNMP_Manager
#include <MAX7219_Digits.h>       // https://github.com/Mottramlabs/MAX7219-7-Segment-Driver

//************************************
//* Your WiFi info                   *
//************************************
const char *ssid = "YOUR SSID";
const char *password = "WIFI PASSWORD";
//************************************

//************************************
//* SNMP Device Info                 *
//************************************
IPAddress router(192, 168, 200, 1);
const char *community = "public";               // SNMP Community String
const int snmpVersion = 1;                      // SNMP Version 1 = 0, SNMP Version 2 = 1
const unsigned int downSpeed = 516000000;       // 516Mbps
const unsigned int upSpeed = 36000000;          // 36Mbps
char *oidInOctets = ".1.3.6.1.2.1.2.2.1.10.1";  // Counter32 ifInOctets.1
char *oidOutOctets = ".1.3.6.1.2.1.2.2.1.16.1"; // Counter32 ifOutOctets.1
char *oidUptime = ".1.3.6.1.2.1.1.3.0";         // TimeTicks Uptime
//************************************

//************************************
//* Settings                         *
//************************************
const int fastPollInterval = 1000; // Perform initial fast polling to populate data.
const int pollInterval = 15000;    // delay in milliseconds (15000 = 15 seconds)
const int deltaTimeError = 2;      // Permitted difference between poll interval and calculated uptime
//************************************

//************************************
//* Initialise                       *
//************************************
// Variables
unsigned int inOctets = 0;
unsigned int outOctets = 0;
int uptime = 0;
int lastUptime = 0;
int lastInOctetsUptime = 0;
int lastOutOctetsUptime = 0;

float bandwidthInUtilPct = 0;
float bandwidthOutUtilPct = 0;
unsigned int lastInOctets = 0;
unsigned int lastOutOctets = 0;
// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and recieve packets
SNMPManager snmp = SNMPManager(community);             // Starts an SMMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SMMPGet instance to send requests

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

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Get callbacks from creating a handler for each of the OID
  callbackInOctets = snmp.addCounter32Handler(oidInOctets, &inOctets);
  callbackOutOctets = snmp.addCounter32Handler(oidOutOctets, &outOctets);
  callbackUptime = snmp.addTimestampHandler(oidUptime, &uptime);

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
      stopFastPolling(); // Stop fast polling after good valid poll has occured and data for current and last stored.
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
      Serial.print("isValidPoll - False: (Fast Poll) Implausable sample period: ");
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
      Serial.print("isValidPoll - False: (Regular Poll) Implausable sample period: ");
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

float calculateBandwidth(unsigned int current, unsigned int last, unsigned int speed, int currentTime, int lastTime)
{
  float bandwidth = 0;
  float deltaTimeSec = (float)(currentTime - lastTime) / 100;
  if (last > current)
  {
    Serial.println("calculateBandwidth: last > current - Counter wrapped?");
    bandwidth = ((float)(((4294967295 - last) + current) * 8) / (float)((speed * deltaTimeSec))) * 100;
  }
  else
  {
    bandwidth = ((float)((current - last) * 8) / (float)((speed * deltaTimeSec))) * 100;
  }
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
  // Build a SNMP get-request, add multiple OID to a single request
  snmpRequest.addOIDPointer(callbackInOctets);
  snmpRequest.addOIDPointer(callbackOutOctets);
  snmpRequest.addOIDPointer(callbackUptime);

  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);

  snmpRequest.clearOIDList();
}
