# Broadband Usage Display

This project serves to create a glanceable display to show my current broadband utilisation. Which has proved invaluable when trying to work from home and another family member starts another multi-gigabyte game update.

Requires a router which supports SNMP. I'm using a [Draytek Vigor 2860](https://amzn.to/2zIIOLe).

![Front](images/broadband_usage_display_front.png)
![Rear](images/broadband_usage_display_rear.png)

## What it shows

![Annotated Picture](images/broadband_usage_display_front_2_annotated.png)

The 7-segment display and the outer 3 colour PowerBar show the download bandwidth utilisation percent. The horizontal green, yellow, red, blue LEDs are used to show the percentage upload bandwidth utilisation percent.

## Requirements

### Hardware

- Wemos D1 Mini
- USB Power supply
- MottramLabs 4 Digit Display with Bar Graph (Wemos version) [MottramLabs.com](https://www.mottramlabs.com/display_products.html)
- Router with SNMP support and ADSL-LINE-MIB
  - You could alter the OID's to query standard interfaces

### Libraries

- Data collection via SNMP: [Arduino SNMP Manager](https://github.com/shortbloke/Arduino_SNMP_Manager)
- Data polling interval control via: [MillisDelay](https://www.forward.com.au/pfod/ArduinoProgramming/TimingDelaysInArduino.html#using)
- Power Display Driver via: [MAX7219 Digits](https://github.com/Mottramlabs/MAX7219-7-Segment-Driver)

## Configuration

Before flashing your Wemos, edit [broadbandspeed.ino](broadbandspeed.ino) and set:

- `ssid` and `password` with your WiFi connection information
- `IPAddress router(192, 168, 200, 1);` replace with the IP address of your router
- `community` the SNMP community string of your router
- The maximum upload and download speeds are read from the ADSL-LINE-MIB to get the current sync speed. You can manually set a threshold if you prefer or if it's not available
- `pollInterval` controls how frequently data is requested from the router. Default is 15 second.

There are lots of other configurable parameters, as you'll see if you look through the code. Hopefully the names and comments make it easy enough to understand.
