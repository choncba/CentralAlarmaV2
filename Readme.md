# Domotic Central Alarm System for Home Assistant
## ATMEGA1284P as main controller, uses Wireless WiFi Comunication with MQTT-JSON trough ESP8266 and ESP-Link firmware
### Main Board Hardware:
* ATMEGA1284P
* ESP8266 - ESP01 4MBIT
* SIM800L GSM Module
* DS18B20 Temperature sensor
* DHT22 Temperature/Humidity Sensor
* Resistive LCR Sensor
* DC/DC module for battery charge and +5v Power
* Dual MOSFET outputs for Sirens
* PT2272-L4 RF module for local wireless functions
* AC power measurement module PZEM22

### Main Board Firmware
* ATMEGA1284P uses [Mighty Core](https://github.com/MCUdude/MightyCore)
* [ELClient](https://github.com/jeelabs/el-client/tree/master/ELClient) - SLIP & MQTT communication to [ESP-Link](https://github.com/jeelabs/esp-link)
* ThreadedGSM - [modified version](https://github.com/choncba/ArduinoThreadedGSM)
* Adafruit Unified Sensor
* DHT sensor library
* OneWire
* DallasTemperature
* ArduinoJson

### Controller: [HomeAssistant]()

### [Schematic](https://easyeda.com/editor#id=|d1a352035b8044ac87127abeea01cdcf)