#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
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
const char *community = "public";
const int snmpVersion = 1; // SNMP Version 1 = 0, SNMP Version 2 = 1
// OIDs
char *oidAdslDownSpeed = ".1.3.6.1.2.1.10.94.1.1.4.1.2.4"; // Guage ADSL Down Sync Speed
char *oidAdslUpSpeed = ".1.3.6.1.2.1.10.94.1.1.5.1.2.4";   // Guage ADSL Up Sync Speed
char *oidInOctets = ".1.3.6.1.2.1.2.2.1.10.4";             // Counter32 ifInOctets.4
char *oidOutOctets = ".1.3.6.1.2.1.2.2.1.16.4";            // Counter32 ifOutOctets.4
char *oidUptime = ".1.3.6.1.2.1.1.3.0";                    // TimeTicks Uptime
//************************************

//************************************
//* Settings                         *
//************************************
const int fastPollInterval = 1000; // Perform initial fast polling to populate data.
const int pollInterval = 15000;    // delay in milliseconds (15000 = 15 seconds)
//************************************

//************************************
//* Initialise                       *
//************************************
// Variables
unsigned int downSpeed = 0;
unsigned int upSpeed = 0;
unsigned int inOctets = 0;
unsigned int outOctets = 0;
int uptime = 0;
int lastUptime = 0;

float bandwidthInUtilPct = 0;
float bandwidthOutUtilPct = 0;
unsigned int lastInOctets = 0;
unsigned int lastOutOctets = 0;
// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and recieve packets
SNMPManager snmp = SNMPManager(community);             // Starts an SMMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SMMPGet instance to send requests

// Blank callback pointer for each OID
ValueCallback *callbackDownSpeed;
ValueCallback *callbackUpSpeed;
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

  // Create a handler for each of the OID
  snmp.addGuageHandler(oidAdslDownSpeed, &downSpeed);
  snmp.addGuageHandler(oidAdslUpSpeed, &upSpeed);
  snmp.addCounter32Handler(oidInOctets, &inOctets);
  snmp.addCounter32Handler(oidOutOctets, &outOctets);
  snmp.addTimestampHandler(oidUptime, &uptime);

  // Create the call back ID's for each OID
  callbackDownSpeed = snmp.findCallback(oidAdslDownSpeed);
  callbackUpSpeed = snmp.findCallback(oidAdslUpSpeed);
  callbackInOctets = snmp.findCallback(oidInOctets);
  callbackOutOctets = snmp.findCallback(oidOutOctets);
  callbackUptime = snmp.findCallback(oidUptime);

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
  // We need to wait for the callbacks variables to be updated before we do the calculations.
  // If router rebooted, then Uptime maybe less that than lastuptime
  if (uptime != lastUptime)
  {
    calculateBandwidths();
    updateDisplay();
  }
}

void updateDisplay()
{
  // Download Utilisation
  Display.Display_Value(1, bandwidthInUtilPct, 1, 0x00);        // Display % util on 7 segment displays
  int barPercent = map(int(bandwidthInUtilPct), 0, 100, 0, 16); // Map % util on to the Green->Amber->Red LEDs
  Display.MAX7219_Write(5, Bar_1[barPercent]);
  Display.MAX7219_Write(6, Bar_2[barPercent]);
  // Upload Utilisation
  int indicatorPercent = map(int(bandwidthOutUtilPct), 0, 100, 0, 4); // Map % util on to the indicator strip
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

void calculateBandwidths()
{
  int deltaTime = 0;
  if (uptime < lastUptime)
  {
    Serial.println("Uptime less than lastUptime. Skip calculation.");
  }
  else if (uptime > 0 && lastUptime > 0)
  {
    deltaTime = (uptime - lastUptime) / 100;
    if (isFastPolling && (deltaTime < (fastPollInterval / 1000)))
    {
      Serial.println("Fast Poll: Implausable sample period. Skipping.");
    }
    else if (!isFastPolling && (deltaTime < (pollInterval / 1000)))
    {
      Serial.println("Regular Poll: Implausable sample period. Skipping.");
    }
    else
    {
      if (isFastPolling)
      {
        stopFastPolling();
      }
      if (inOctets >= lastInOctets)
      {
        bandwidthInUtilPct = ((float)((inOctets - lastInOctets) * 8) / (float)(downSpeed * deltaTime) * 100);
      }
      else if (lastInOctets > inOctets)
      {
        Serial.println("inOctets Counter wrapped");
        bandwidthInUtilPct = (((float)((4294967295 - lastInOctets) + inOctets) * 8) / (float)(downSpeed * deltaTime) * 100);
      }
      if (outOctets >= lastOutOctets)
      {
        bandwidthOutUtilPct = ((float)((outOctets - lastOutOctets) * 8) / (float)(upSpeed * deltaTime) * 100);
      }
      else if (lastOutOctets > outOctets)
      {
        Serial.println("outOctets Counter wrapped");
        bandwidthOutUtilPct = (((float)((4294967295 - lastOutOctets) + outOctets) * 8) / (float)(upSpeed * deltaTime) * 100);
      }
      // Serial.print("In %: ");
      // Serial.print(bandwidthInUtilPct);
      // Serial.print(" - Out %: ");
      // Serial.println(bandwidthOutUtilPct);
    }
  }
  // Update last samples
  lastUptime = uptime;
  lastInOctets = inOctets;
  lastOutOctets = outOctets;
}

void getSNMP()
{
  //build a SNMP get-request
  snmpRequest.addOIDPointer(callbackDownSpeed);
  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();

  snmpRequest.addOIDPointer(callbackUpSpeed);
  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();

  snmpRequest.addOIDPointer(callbackInOctets);
  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();

  snmpRequest.addOIDPointer(callbackOutOctets);
  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();

  snmpRequest.addOIDPointer(callbackUptime);
  snmpRequest.setIP(WiFi.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();
}
