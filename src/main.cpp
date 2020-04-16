//////////////////////////////////////////////////////////////////////////////////////
//        Central de Alarma V2 con conectividad MQTT a través de ESP-Link           //
//                                                                                  //
//    Hardware:                                                                     //
//            - ATMEGA1284P                                                         //
//            - ESP8266 - ESP01 4MBIT                                               //
//            - Módulo GSM SIM800L                                                  //
//            - DS18B20                                                             //
//            - DHT22                                                               //
//            - LCR                                                                 //
//            - Módulos DC/DC para carga de Batería 12v y regulación +5v            //
//            - Salidas a sirenas con MOSFET                                        //
//            - Entradas para llavero RF con PT2272-L4                              //
//            - Modulo de medicion de Potencia AC                                   //
//    Esquematico: https://easyeda.com/editor#id=|d1a352035b8044ac87127abeea01cdcf  //
//                                                                                  //
//    Software:                                                                     //
//            - ATMEGA1284: Mighty Core (cargar bootloader desde Arduino usando la  //
//                          opcion USBASP (Mighty Core))                            //
//                          ELClient para protocolo SLIP y MQTT                     //
//                          ThreadedGSM - version modificada                        //
//            - ESP8266: esp-link v3.0.14-g963ffbb                                  //
//            - Controller: HomeAssistant - Ver configuracion al final              //
//                                                                                  //
//            Envio y recepción de comandos JSON por MQTT                           //
//                                                                                  //
//////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "config.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ELClient.h>
#include <ELClientMqtt.h>

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

