/* View Readme.md */

#include <Arduino.h>
#include "config.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ELClient.h>
#include <ELClientMqtt.h>

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

