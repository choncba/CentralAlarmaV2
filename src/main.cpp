/* View Readme.md */

#include <Arduino.h>
#include "config.h"
#include <EEPROM.h>
#include <ELClient.h>
#include <ELClientMqtt.h>
#include <ArduinoJson.h>

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

// Estructura con los estados de la alarma
struct
{
    uint8_t Entrada[8] = { 1,1,1,1,1,1,1,1 };
    uint8_t AlarmStatus = 0;
    uint8_t AlarmNextStatus = 0;
    uint8_t TriggerCause = 0;
    bool Vac = 0;
    float Vbat = 0;
    float OutsideTemp = 0;
    float OutsideHum = 0;
    float InsideTemp = 0;
    uint8_t LumExt = 0;
    uint8_t RFin[4] = { 1,1,1,1 };
    bool GsmStatus;
    uint8_t GsmSignal;
    float voltage;
    float current;
    float power;
    float energy;
}Status;

// Estructura para las opciones de la alarma y GSM que se almacenan en EEPROM
struct
{
	char data_set;
	char inputs_names[ALARM_INPUTS][20] = { "","","","","","","","" };	    // Nombres de las entradas
    uint8_t inputs_function[ALARM_INPUTS] = { 1, 1, 1, 1, 1, 1, 1, 1 };   // Funciones de las entradas
	char PIN[5] = "1234";						                                        // clave PIN
	char reg_numbers[NUM_PHONES][15] = { "", "", "", "", "" };	            // Números de teléfono de la agenda
}Options;

unsigned long timer;

// String StringTopic = "";
// char CharTopic[100];
// String StringData = "";
// char CharData[10];

// Callback made from esp-link to notify of wifi status changes
void wifiCb(void* response) {
  ELClientResponse *res = (ELClientResponse*)response;
  if (res->argc() == 1) {
    uint8_t status;
    res->popArg(&status, 1);

    if(status == STATION_GOT_IP) {
      DEBUG_PRINTLN(F("WIFI CONNECTED"));
    } else {
      DEBUG_PRINT(F("WIFI NOT READY: "));
      DEBUG_PRINTLN(status);
    }
  }
}

// Callback when MQTT is connected
// Para poder recibir JSON largos se modifico en ELClient.h:
//        uint8_t _protoBuf[128]; /**< Protocol buffer */
// Por:   uint8_t _protoBuf[512]; /**< Protocol buffer */
void mqttConnected(void* response) {
  DEBUG_PRINTLN(F("MQTT connected!"));
  connected = true;

  mqtt.subscribe(SET_TOPIC);
  mqtt.subscribe(OPTIONS_TOPIC);
  mqtt.publish(MQTT_AVAILABILITY_TOPIC,MQTT_CONNECTED_STATUS,QoS,RETAIN);
}

// Callback when MQTT is disconnected
void mqttDisconnected(void* response) {
  DEBUG_PRINTLN(F("MQTT disconnected"));
  connected = false;
}

// Callback when an MQTT message arrives for one of our subscriptions
void mqttData(void* response) {
  ELClientResponse *res = (ELClientResponse *)response;

  DEBUG_PRINT(F("Received: topic="));
  String topic = res->popString();
  DEBUG_PRINTLN(topic);

  DEBUG_PRINT(F("data="));
  String data = res->popString();
  DEBUG_PRINTLN(data);

}

void mqttPublished(void* response) {
  DEBUG_PRINTLN(F("MQTT published"));
}

/***
* // Publica todas las variables en un unico JSON
* Ejemplo de JSON, ver espacio necesario con https://arduinojson.org/v6/assistant/
* Topic: /CentralAlarma/status
* Data:
* {
  "status":1,
  "alarm":{
    "status": 0,
    "trigger_cause": 1,
    "pin": 12345678,
    "inputs_status":[0,0,0,0,0,0,0,0],
    "inputs_names":["01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789"],
    "inputs_function":[0,0,0,0,0,0,0,0]
  },
  "sensors":{
    "temp_int":99.99,
    "temp_ext":99.99,
    "hum_ext":99,
    "lcr":99,
    "v_bat":99.99,
    "vac":1,
    "voltage":999,
    "current":99.99,
    "power":9999.99,
    "energy":999999
  },
  "gsm":{
        "status":1,
        "signal_level":99,
        "numbers":["01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789","01234567890123456789"]
  },
  "rf":[0,0,0,0]
  }
***/

void PublicarMQTT(void* context)
{
  (void)context;
  if(connected){
    const size_t capacity = JSON_ARRAY_SIZE(4) + JSON_ARRAY_SIZE(5) + 3*JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(10) + 520;
    DynamicJsonDocument doc(capacity);

    doc["status"] = 1;

    JsonObject alarm = doc.createNestedObject("alarm");
    alarm["status"] = Status.AlarmStatus;
    alarm["trigger_cause"] = Status.TriggerCause;
    alarm["pin"] = Options.PIN;

    JsonArray alarm_inputs_status = alarm.createNestedArray("inputs_status");
    JsonArray alarm_inputs_names = alarm.createNestedArray("inputs_names");
    JsonArray alarm_inputs_function = alarm.createNestedArray("inputs_function");
    for(int i=0; i < ALARM_INPUTS; i++){
      alarm_inputs_status.add(Status.Entrada[i]);
      alarm_inputs_names.add((String)Options.inputs_names[i]);
      alarm_inputs_function.add(Options.inputs_function[i]);
    }

    JsonObject sensors = doc.createNestedObject("sensors");
    sensors["temp_int"] = Status.InsideTemp;
    sensors["temp_ext"] = Status.OutsideTemp;
    sensors["hum_ext"] = Status.OutsideHum;
    sensors["lcr"] = Status.LumExt;
    sensors["v_bat"] = Status.Vbat;
    sensors["vac"] = Status.Vac;
    sensors["voltage"] = Status.voltage;
    sensors["current"] = Status.current;
    sensors["power"] = Status.power;
    sensors["energy"] = Status.energy;

    JsonObject gsm = doc.createNestedObject("gsm");
    gsm["status"] = Status.GsmStatus;
    gsm["signal_level"] = Status.GsmSignal;

    JsonArray gsm_numbers = gsm.createNestedArray("numbers");
    for(int i=0; i<NUM_PHONES; i++){
      gsm_numbers.add((String)Options.reg_numbers[i]);
    }

    JsonArray rf = doc.createNestedArray("rf");
    for(int i=0; i<RF_INPUTS; i++){
      rf.add(Status.RFin[i]);
    }
    
    char DataMQTT[capacity];
    serializeJson(doc, DataMQTT);
    mqtt.publish(STATUS_TOPIC, DataMQTT, QoS, RETAIN);

  }
}
#pragma endregion

#pragma region SETUP
void setup() {
  pinMode(LED, OUTPUT);
  pinMode(SIM_RES, OUTPUT);
  pinMode(SIR1, OUTPUT);
  pinMode(SIR2, OUTPUT);
  pinMode(LED_EXT, OUTPUT);
  pinMode(IN1, INPUT_PULLUP);
  pinMode(IN2, INPUT_PULLUP);
  pinMode(IN3, INPUT_PULLUP);
  pinMode(IN4, INPUT_PULLUP);
  pinMode(IN5, INPUT_PULLUP);
  pinMode(IN6, INPUT_PULLUP);
  pinMode(IN7, INPUT_PULLUP);
  pinMode(IN8, INPUT_PULLUP);
  pinMode(RFA, INPUT);
  pinMode(RFB, INPUT);
  pinMode(RFC, INPUT);
  pinMode(RFD, INPUT);

  SerialDebug.begin(115200);

  // Sync with ESP-link
  DEBUG_PRINTLN(F("EL-Client starting!"));
  esp.wifiCb.attach(wifiCb); // wifi status change callback, optional (delete if not desired)
  bool ok;
  do {
    ok = esp.Sync();      // sync up with esp-link, blocks for up to 2 seconds
    if (!ok) Serial.println(F("EL-Client sync failed!"));
  } while(!ok);
  DEBUG_PRINTLN(F("EL-Client synced!"));

  // Set-up callbacks for events and initialize with es-link.
  mqtt.connectedCb.attach(mqttConnected);
  mqtt.disconnectedCb.attach(mqttDisconnected);
  mqtt.publishedCb.attach(mqttPublished);
  mqtt.dataCb.attach(mqttData);
  mqtt.setup();
  mqtt.lwt(MQTT_AVAILABILITY_TOPIC,MQTT_DISCONNECTED_STATUS,QoS,RETAIN);

  DEBUG_PRINTLN(F("EL-MQTT ready"));

  timer = millis();
  
  randomSeed(analogRead(0));
}
#pragma endregion

void loop(){
    esp.Process();

    if(millis()- timer > 30000){

    // Genero valores aleatorios para sensores y cambio entradas
    for(int i=0;i<ALARM_INPUTS;i++){
        (Status.Entrada[i])?0:1;
        if(i<RF_INPUTS) (Status.RFin[i])?0:1;
    }

    Status.Vac ^=1;
    Status.TriggerCause = random(7);
    Status.Vbat = (float)random(110,140)/10;
    Status.OutsideTemp = (float)random(400)/10;
    Status.OutsideHum = random(99);
    Status.InsideTemp = (float)random(400)/10;
    Status.LumExt = random(100);
    Status.GsmStatus ^=1;
    Status.GsmSignal = random(100);
    Status.voltage = (float)random(2000,2300)/10;
    Status.current = (float)random(1000)/100;
    Status.power = (float)random(1000000)/100;
    Status.energy = (float)random(100000)/10;

    PublicarMQTT((void*)0);
    timer = millis();
    Serial.println("Enviando Datos...");
    //mqtt.publish("/TestJSON","OK",QoS,RETAIN);
    }    
}



