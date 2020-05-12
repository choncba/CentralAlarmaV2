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
	char inputs_names[ALARM_INPUTS][20] = { "","","","","","","","" };	// Nombres de las entradas
    uint8_t inputs_function[ALARM_INPUTS] = { 1, 1, 1, 1, 1, 1, 1, 1 }; // Funciones de las entradas
	char PIN[5] = {"1234"};						                        // clave PIN
	char reg_numbers[NUM_PHONES][15] = { "", "", "", "", "" };	            // Números de teléfono de la agenda
}Options;

unsigned long timer;

#pragma region MQTT
const char NombreTopic[] = {"/CentralAlarma"};
String StringTopic = "";
char CharTopic[100];
String StringData = "";
char CharData[10];

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
void mqttConnected(void* response) {
  DEBUG_PRINTLN(F("MQTT connected!"));
  connected = true;

  mqtt.subscribe(SET_TOPIC);
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

//   if(topic.indexOf("/set")>0)  // Si el topic contiene "/set"
//   {
//     if(data.equals("DISARM"))
//     {
//       Status.AlarmNextStatus = DISARMED;
//       DEBUG_PRINTLN("Alarma desactivada via MQTT");
//     }
//     if(data.equals("ARM_HOME"))
//     {
//       Status.AlarmNextStatus = ARMED_HOME;
//       DEBUG_PRINTLN("Alarma Activada en casa via MQTT");
//     } 
//     if(data.equals("ARM_AWAY"))
//     {
//       Status.AlarmNextStatus = ARMED_AWAY;
//       DEBUG_PRINTLN("Alarma Activada Fuera de casa via MQTT");
//     }
//   }
}

void mqttPublished(void* response) {
  DEBUG_PRINTLN(F("MQTT published"));
}

// Publica todas las variables en un unico JSON
/* Ejemplo de JSON, ver espacio necesario con https://arduinojson.org/v6/assistant/
{
  "central_alarma":
  {
    "status": "online",
    "alarm":{
      "status": "disarmed",
      "trigger_cause": "IN2",
      "inputs_status":[0,0,0,0,0,0,0,0],
      "inputs_names":["","","","","","","",""],
      "inputs_function":[0,0,0,0,0,0,0,0]
    },
    "sensors":{
      "temp_int":"25.0",
      "temp_ext":"27.8",
      "hum_ext":"33",
      "lcr":"90",
      "v_bat":"12.84",
      "vac":"1",
      "voltage":"220",
      "current":"1.35",
      "power":"875.2",
      "energy":"1385"
    },
    "gsm":{
      "status":1,
      "signal_level":"33"
    },
    "rf":[0,0,0,0]
  }
}
*/

void PublicarMQTT(void* context)
{
  (void)context;
  if(connected){
    uint8_t i;
    const size_t capacity = JSON_ARRAY_SIZE(4) + 3*JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + 2*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);

    JsonObject central_alarma = doc.createNestedObject("central_alarma");
    central_alarma["status"] = "online";

    JsonObject central_alarma_alarm = central_alarma.createNestedObject("alarm");
    central_alarma_alarm["status"] = AlarmStatus[Status.AlarmStatus];
    central_alarma_alarm["trigger_cause"] = Options.inputs_names[Status.TriggerCause];
    
    JsonArray central_alarma_alarm_inputs_status = central_alarma_alarm.createNestedArray("inputs_status");
    JsonArray central_alarma_alarm_inputs_names = central_alarma_alarm.createNestedArray("inputs_names");
    JsonArray central_alarma_alarm_inputs_function = central_alarma_alarm.createNestedArray("inputs_function");
    for(i=0; i<ALARM_INPUTS;i++){
      central_alarma_alarm_inputs_status.add(Status.Entrada[i]);
      central_alarma_alarm_inputs_names.add(Options.inputs_names[i]);
      central_alarma_alarm_inputs_function.add(Options.inputs_function[i]);
    }

    JsonObject central_alarma_sensors = central_alarma.createNestedObject("sensors");
    central_alarma_sensors["temp_int"] = Status.InsideTemp;
    central_alarma_sensors["temp_ext"] = Status.OutsideTemp;
    central_alarma_sensors["hum_ext"] = Status.OutsideHum;
    central_alarma_sensors["lcr"] = Status.LumExt;
    central_alarma_sensors["v_bat"] = Status.Vbat;
    central_alarma_sensors["vac"] = Status.Vac;
    central_alarma_sensors["voltage"] = Status.voltage;
    central_alarma_sensors["current"] = Status.current;
    central_alarma_sensors["power"] = Status.power;
    central_alarma_sensors["energy"] = Status.energy;

    JsonObject central_alarma_gsm = central_alarma.createNestedObject("gsm");
    central_alarma_gsm["status"] = Status.GsmStatus;
    central_alarma_gsm["signal_level"] = Status.GsmSignal;

    JsonArray central_alarma_rf = central_alarma.createNestedArray("rf");
    for(i=0; i<RF_INPUTS;i++){
      central_alarma_rf.add(Status.RFin[i]);
    }
    
    char DataMQTT[capacity];
    mqtt.publish(NombreTopic, DataMQTT, QoS, RETAIN);

    //size_t n = serializeJson(doc, DataMQTT);
    //mqtt.publish(NombreTopic, DataMQTT, n, QoS, RETAIN);
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
        Status.Entrada[i]^=1;
        if(i<RF_INPUTS) Status.RFin[i]^=1;
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



