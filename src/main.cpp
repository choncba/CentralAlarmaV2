/* View Readme.md */

#include <Arduino.h>
#include "config.h"
#include <EEPROM.h>
#include <ELClient.h>
#include <ELClientMqtt.h>
#include <ArduinoJson.h>
#include "Timer.h"  // Ver MightyCore
#include <Wire.h>

#pragma region Definicion de modulos adicionales

#ifdef USE_GSM
//#include <ThreadedGSM.h>
//ThreadedGSM SIM800(SerialModem);
#include <SIM800ThreadedSMS.h>
SIM800ThreadedSMS SIM800(SerialModem);
#endif

#ifdef USE_SENSOR_DHT22
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTTYPE DHT22
DHT_Unified dht(DHT_PIN, DHTTYPE);
#endif

#ifdef USE_SENSOR_18B20
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(OW_PIN);
DallasTemperature sensors(&oneWire);
#endif

#ifdef USE_SENSOR_POWER
#include <PZEM004T.h>         // Libreria de manejo del modulo medidor de potencia, usa softwareserial
PZEM004T pzem(pwrRX, pwrTX);  // (RX,TX) connect to TX,RX of PZEM
IPAddress ip(192,168,1,1);
#endif

#pragma endregion

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

void mqttPub_AlarmInputs(void);
void mqttPub_AlarmStatus(void);
void mqttPub_AlarmOptions(void);
void LeerSensores(void);
void mqttPublishAll(void*);  

#pragma region Variables Globales
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
    int GsmSignal;
    float voltage;
    float current;
    float power;
    float energy;
}Status;

// Estructura para las opciones de la alarma y GSM que se almacenan en EEPROM
struct
{
	char data_set;
	char inputs_names[ALARM_INPUTS][20] = { "","","","","","","","" };	  // Nombres de las entradas
  uint8_t inputs_function[ALARM_INPUTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // Funciones de las entradas
	char PIN[5] = "1234";						                                      // clave PIN
	char reg_numbers[NUM_PHONES][15] = { "", "", "", "", "" };	          // Números de teléfono de la agenda
  uint8_t active_numbers[NUM_PHONES] = {0, 0, 0, 0, 0 };                // Numeros habilitados
}Options;

// Timers
#ifdef USE_RANDOM_SENSORS
unsigned long timer;
#endif
Timer t_leds, t_sensores, t_mqtt; //, t_gsm;
int timerLEDS = 0;
int id_timerAviso = 0;

//Flags
bool EnviarAvisoSMS = false;
#pragma endregion

#pragma region Funciones EEPROM
void saveEEPROM(){
  EEPROM.put(0, Options);
}

void readEEPROM(){
  EEPROM.get(0,Options);
}

void defaultEEPROM(){
  int i;
  String aux;
  Options.data_set = 'T';
  for(i = 0; i < ALARM_INPUTS; i++){
    sprintf(&Options.inputs_names[i][0], "IN%d", i+1);
    Options.inputs_function[i] = DESACTIVADA;
  }
  aux = "1234";
  aux.toCharArray(Options.PIN, sizeof(Options.PIN));
  aux = "";
    for(i = 0; i < NUM_PHONES; i++){
    aux.toCharArray(&Options.reg_numbers[i][0], sizeof(Options.reg_numbers[i]));
    Options.active_numbers[i] = false;
  }
  saveEEPROM();
}
#pragma endregion

#pragma region Funciones MQTT
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
// Por:   uint8_t _protoBuf[1024]; /**< Protocol buffer */ Para ArfuinoJson v6+
void mqttConnected(void* response) {
  DEBUG_PRINTLN(F("MQTT connected!"));
  connected = true;

  mqtt.subscribe(ALARM_SET_TOPIC);
  mqtt.subscribe(ALARM_SET_OPTIONS_TOPIC);
  mqtt.subscribe(SMS_SEND_TOPIC);
  mqtt.subscribe(MQTT_HA_AVAILAVILITY);
  mqtt.publish(MQTT_AVAILABILITY_TOPIC,MQTT_CONNECTED_STATUS,QoS,RETAIN);

  mqttPublishAll((void*)0);
}

// Callback when MQTT is disconnected
void mqttDisconnected(void* response) {
  DEBUG_PRINTLN(F("MQTT disconnected"));
  connected = false;
}

/* Suscripciones: */

void mqttData(void* response) {
  ELClientResponse *res = (ELClientResponse *)response;

  DEBUG_PRINT(F("Received: topic="));
  String topic = res->popString();
  DEBUG_PRINTLN(topic);

  DEBUG_PRINT(F("data="));
  String data = res->popString();
  DEBUG_PRINTLN(data);

// Hassio publica:  Topic: /Central/Alarm/Set
//                  data: "DISARM", "ARM_HOME", "ARM_AWAY", "PENDING", "TRIGGERED"
  if(topic == ALARM_SET_TOPIC){
    for(uint8_t value = 0; value<NUM_STATUS; value++){
      if(data == AlarmCMD[value]){
        Status.AlarmNextStatus = value;
        break;
      }
    }
  }

  // Verifica el estado de HomeAssistant, publica todo si el mismo se reinicia
  if(topic == MQTT_HA_AVAILAVILITY){
    if(data == MQTT_CONNECTED_STATUS){
      mqttPublishAll((void*)0);
    }
  }

// Hassio publica:  Topic: /Central/Alarm/SetOptions
//                  Data:  
                        // { 
                        // "pin":1234
                        // "inputs_names":["IN1","IN2","IN3","IN4","IN5","IN6","IN7","IN8"], 
                        // "inputs_function":[3,1,3,4,0,0,0,0], 
                        // "numbers":["0123456789","0123456789","0123456789","0123456789","0123456789"],
                        // "act_numbers":[0,0,0,0] 
                        // }
  if(topic == ALARM_SET_OPTIONS_TOPIC){
    const size_t capacity = 2*JSON_ARRAY_SIZE(5) + 2*JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(4) + 290;
    DynamicJsonDocument doc(capacity);

    DeserializationError err= deserializeJson(doc, data);
					if(err)
					{
            DEBUG_PRINT(F("Error JSON: "));
						DEBUG_PRINTLN(err.c_str());
					}
					else
					{
            strlcpy(Options.PIN, doc["pin"], sizeof(Options.PIN));
            for(int i=0; i<ALARM_INPUTS;i++)
            {
              strlcpy(Options.inputs_names[i], doc["inputs_names"][i], sizeof(Options.inputs_names[i]));
              Options.inputs_function[i] = doc["inputs_function"][i];
            }

            for(int i=0; i<NUM_PHONES;i++)
            {
              strlcpy(Options.reg_numbers[i], doc["numbers"][i], sizeof(Options.reg_numbers[i]));
              Options.active_numbers[i] = doc["act_numbers"][i];
            }
#ifdef USE_EEPROM
            saveEEPROM();
#endif
            mqttPub_AlarmOptions();
          }
  }
/*
* Recibe en /SMS/send
* {
*   "number":123456789,
*   "message":"abcdefghyjklmnopqrstuvwxyz" -> 100 caracteres maximo
* }
*/
  if(topic == SMS_SEND_TOPIC){
    const size_t capacity = JSON_OBJECT_SIZE(2) + 150;
    DynamicJsonDocument doc(capacity);

    DeserializationError err= deserializeJson(doc, data);
    if(err)
    {
      DEBUG_PRINT(F("Error JSON: "));
      DEBUG_PRINTLN(err.c_str());
    }
    else
    {
      String number = doc["number"].as<String>();
      String message = doc["message"].as<String>();
      // Mando el SMS con el contenido del JSON
      SIM800.sendSMS(number, message);
    }
  }
}

void mqttPublished(void* response) {
  DEBUG_PRINTLN(F("MQTT published"));
}

//////////////////////////////////////
//        Publicaciones MQTT        //
//////////////////////////////////////

// Publico el estado de la alarma
void mqttPub_AlarmStatus(){
  if(connected)
  {
    DEBUG_PRINTLN(F("Enviando Datos de estado de la Alarma"));
    mqtt.publish(ALARM_STATUS_TOPIC, AlarmStatus_txt[Status.AlarmStatus],QoS,RETAIN);
  }
}

// Publico el estado de las entradas de la alarma y entradas RF
void mqttPub_AlarmInputs(){
  if(connected)
  {
    StaticJsonDocument<128> doc;

    JsonArray sensors = doc.createNestedArray("sensors");

    for(int i=0; i < ALARM_INPUTS; i++){
      sensors.add(Status.Entrada[i]);
    }

    JsonArray rf = doc.createNestedArray("rf");
      for(int i=0; i<RF_INPUTS; i++){
        rf.add(Status.RFin[i]);
      }
      
      char DataMQTT[128];
      serializeJson(doc, DataMQTT);
      DEBUG_PRINTLN(F("Enviando Datos de las entradas de la Alarma"));
      mqtt.publish(ALARM_INPUTS_TOPIC, DataMQTT, QoS, RETAIN);
  }
}

// Publico las opciones de la alarma
void mqttPub_AlarmOptions(){
  if(connected)
  {
    StaticJsonDocument<512> doc;

    doc["pin"] = Options.PIN;

    JsonArray alarm_inputs_names = doc.createNestedArray("inputs_names");
    JsonArray alarm_inputs_function = doc.createNestedArray("inputs_function");
    for(int i=0; i < ALARM_INPUTS; i++){
      alarm_inputs_names.add((String)Options.inputs_names[i]);
      alarm_inputs_function.add(Options.inputs_function[i]);
    }

    JsonArray numbers = doc.createNestedArray("numbers");
    JsonArray act_numbers = doc.createNestedArray("act_numbers");
    for(int i=0; i<NUM_PHONES; i++){
      numbers.add((String)Options.reg_numbers[i]);
      act_numbers.add(Options.active_numbers[i]);
    }

    char DataMQTT[512];
    serializeJson(doc, DataMQTT);
    DEBUG_PRINTLN(F("Enviando Datos de Opciones de la Alarma"));
    mqtt.publish(ALARM_OPTIONS_TOPIC, DataMQTT, QoS, RETAIN);
  }
}

// Corrijo a 2 decimales, ver https://arduinojson.org/v6/how-to/configure-the-serialization-of-floats/
double round2(double value) {
   return (int)(value * 100 + 0.5) / 100.0;
}

// Publico informacion de los sensores
void mqttPub_Sensors(){
  if(connected)
  {
    if(Status.InsideTemp != 0)  // Evito que mande valores nulos si los sensores aun no están inicializados
    {
      StaticJsonDocument<256> doc;
    
      doc["trigger_cause"] = Status.TriggerCause;
      doc["temp_int"] = round2(Status.InsideTemp);
      doc["temp_ext"] = round2(Status.OutsideTemp);
      doc["hum_ext"] = round2(Status.OutsideHum);
      doc["lcr"] = Status.LumExt;
      doc["v_bat"] = round2(Status.Vbat);
      doc["vac"] = Status.Vac;
      doc["voltage"] = Status.voltage;
      doc["current"] = Status.current;
      doc["power"] = Status.power;
      doc["energy"] = Status.energy;
      doc["gsm_status"] = Status.GsmStatus;
      doc["gsm_signal"] = Status.GsmSignal;

      char DataMQTT[256];
      serializeJson(doc, DataMQTT);
      DEBUG_PRINTLN(F("Enviando Datos de Sensores"));
      mqtt.publish(SENSORS_TOPIC, DataMQTT, QoS, RETAIN);
    }
  }
}

 // Publico el SMS recibido por MQTT
void mqttPub_ReceivedSMS(String& Number, String& Message){
  if(connected)
  {
    StaticJsonDocument<256> doc;
    doc["number"] = Number;
    doc["message"] = Message;
    
    char DataMQTT[256];
    serializeJson(doc, DataMQTT);
    mqtt.publish(SMS_RECEIVE_TOPIC, DataMQTT, QoS, RETAIN);
  }
}

void mqttPublishAll(void *context){ 
  (void)context;
  mqttPub_AlarmInputs();
  mqttPub_AlarmOptions();
  mqttPub_AlarmStatus();
  mqttPub_Sensors();
}

void mqttPublishSensors(void *context){ 
  (void)context;
  LeerSensores();
  mqttPub_Sensors();
}

#pragma endregion

#pragma region Funciones Alarma
void ActivarSirenas(uint8_t modo)
{
  digitalWrite(SIR1, modo);
  digitalWrite(SIR2, modo);
}

void BlinkLeds(void* context)
{
  (void) context;
  digitalWrite(LED,!digitalRead(LED));
  digitalWrite(LED_EXT,!digitalRead(LED_EXT));
}

void beep()
{
  t_leds.pulseImmediate(SIR1,100, HIGH);  //Produce un pulso en alto de 100 mSeg y luego queda en bajo
  t_leds.pulseImmediate(SIR2,100, HIGH);
}

void checkAlarma(){
  if(Status.AlarmStatus != Status.AlarmNextStatus)
  {
    switch(Status.AlarmNextStatus)
    {
      case DISARMED:    t_leds.stop(timerLEDS);
                        digitalWrite(LED, LOW);
                        digitalWrite(LED_EXT, LOW);
                        ActivarSirenas(LOW);
                        if(Status.AlarmStatus == ARMED_AWAY)
                        {
                          beep(); // Suenan las sirenas durante 100 mSeg
#ifdef USE_GSM          
                          EnviarAvisoSMS = true;
#endif                          
                        }
                        break;
      case ARMED_HOME:  t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(1000, BlinkLeds, (void*)0); // Parpadean los leds cada 1 segundo
                        break;
      case ARMED_AWAY:  t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(1000, BlinkLeds, (void*)0); // Parpadean los leds cada 1 segundo
                        beep(); // Suenan las sirenas durante 100 mSeg
#ifdef USE_GSM
                        EnviarAvisoSMS = true;
#endif
                        break;
      case PENDING:     break;
      case TRIGGERED:   ActivarSirenas(HIGH);
                        t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(100, BlinkLeds, (void*)0); // Parpadean los leds cada 100 mSseg
#ifdef USE_GSM
                        EnviarAvisoSMS = true;
#endif
                        break;
      default:          break;      
    }
    Status.AlarmStatus = Status.AlarmNextStatus;
    mqttPub_AlarmStatus();
  }
}
#pragma endregion

#pragma region Lectura de entradas y Sensores
// Lectura de entradas de la Alarma
void AlarmInputsCheck(){
  static uint8_t RoundCheck[ALARM_INPUTS] = { 0,0,0,0,0,0,0,0 };
  const uint8_t RoundCheckThreshole = 8;

  for (uint8_t cnt = 0; cnt < ALARM_INPUTS; cnt++)
  {
    uint8_t curStatus = digitalRead(ALARM_INPUT[cnt]);
    if (Status.Entrada[cnt] != curStatus)
    {
      delay(5);
      curStatus = digitalRead(ALARM_INPUT[cnt]);
      if (Status.Entrada[cnt] != curStatus)
      {
        RoundCheck[cnt]++;
      }
      else  RoundCheck[cnt] = 0;

      if (RoundCheck[cnt] >= RoundCheckThreshole)
      {
        Status.Entrada[cnt] = curStatus;
        RoundCheck[cnt] = 0;
        // Verifico primero la configuracion de la entrada
        switch(Options.inputs_function[cnt]){
          case DESACTIVADA: break;  // Desactivada, no dispara la alarma
          case DISP_TOT_PARCIAL:    // Dispara en armado total o parcial (Perimetrales)
                            if(Status.AlarmStatus == ARMED_HOME || Status.AlarmStatus == ARMED_AWAY){ // Alarma armada en casa o fuera, dispara
                              Status.AlarmNextStatus = TRIGGERED;    // Disparo la alarma
                              Status.TriggerCause = cnt;             // Guardo que entrada causo el disparo
                            }
                            break;
          case DISP_TOTAL:          // Dispara solo en armado total (Sensores internos)
                            if(Status.AlarmStatus == ARMED_AWAY){    // Alarma armada fuera de casa, dispara
                              Status.AlarmNextStatus = TRIGGERED;    // Disparo la alarma
                              Status.TriggerCause = cnt;             // Guardo que entrada causo el disparo
                            }
                            break;
          case DISP_SIEMPRE:        // Dispara SIEMPRE y mientras esté abierta (Tamper campana) 
                            if(Status.Entrada[cnt]){                 // Si esta abierta, aun con la alarma desactivada dispara
                              Status.AlarmNextStatus = TRIGGERED;    // Disparo la alarma
                              Status.TriggerCause = cnt;             // Guardo que entrada causo el disparo
                            }
                            break;
          case DISP_DEMORADO:       // Dispara luego de un minuto de activada
                            break;
          default: break;
        }
        mqttPub_AlarmInputs();
      }
    }
  }
}

#ifdef USE_RF
// Lectura de las entradas RF y acciones sobre la alarma predefinidas
void RfInputsCheck(){
  static uint8_t RoundCheck[RF_INPUTS] = { 0,0,0,0 };
  const uint8_t RoundCheckThreshole = 8;

  for (uint8_t cnt = 0; cnt < RF_INPUTS; cnt++)
  {
    uint8_t curStatus = digitalRead(RF_INPUT[cnt]);
    if (Status.RFin[cnt] != curStatus)
    {
      delay(5);
      curStatus = digitalRead(RF_INPUT[cnt]);
      if (Status.RFin[cnt] != curStatus)
      {
        RoundCheck[cnt]++;
      }
      else  RoundCheck[cnt] = 0;

      if (RoundCheck[cnt] >= RoundCheckThreshole)
      {
        Status.RFin[cnt] = curStatus;
        RoundCheck[cnt] = 0;
        
        switch(cnt)
        {
          case TECLA_A: Status.AlarmNextStatus = ARMED_AWAY;
                        break;
          case TECLA_B: Status.AlarmNextStatus = ARMED_HOME;
                        break;
          case TECLA_C: Status.AlarmNextStatus = DISARMED;
                        break;
          case TECLA_D:
                        break;
          default:      break;
        }
        mqttPub_AlarmInputs();
      }
    }
  }
}
#endif

// Lectura de Sensores
#ifndef USE_RANDOM_SENSORS
void LeerSensores()
{

#ifdef USE_SENSOR_DHT22  
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if(!isnan(event.temperature)) Status.OutsideTemp = event.temperature;

  dht.humidity().getEvent(&event);
  if(!isnan(event.relative_humidity)) Status.OutsideHum = event.relative_humidity;
#endif

#ifdef USE_SENSOR_18B20
  sensors.requestTemperatures();
  float int_temp = sensors.getTempCByIndex(0);
  if(!isnan(int_temp))  Status.InsideTemp = int_temp;
#endif

#ifdef USE_SENSOR_LCR
  Status.LumExt = 0;
  int lectura_lcr = analogRead(LCR);
  Status.LumExt = map(lectura_lcr, 0, 1023, 100, 0);
#endif

  int lectura_vbat = analogRead(VBAT);
  Status.Vbat = lectura_vbat * 0.01967;  // Vbat = Vad.(R1+R2)/R2; Vad=Lectura.5/1024 => Vbat = Lectura.(R1+R2)/R2).(5/1024)
  if(Status.Vac!=digitalRead(VAC)){
    EnviarAvisoSMS = true;
  }
  Status.Vac = digitalRead(VAC);

#ifdef USE_SENSOR_POWER
  float v_med = pzem.voltage(ip);
  if (v_med > 0.0) Status.voltage = v_med;

  float i_med = pzem.current(ip);
  if(i_med >= 0.0) Status.current = i_med;
  
  float p_med = pzem.power(ip);
  if(p_med >= 0.0) Status.power = p_med;
  
  float e_med = pzem.energy(ip);
  if(e_med >= 0.0) Status.energy = e_med;
#endif
}
#endif

#pragma endregion

#pragma region Funciones GSM
#ifdef USE_GSM
// Verifica la validez de un numero telefonico movil
bool check_number(String& Number){
  bool response = false;

  if(!Number.startsWith("0") && !(Number.length() < 7) && (Number != "")){
    response = true;
  }
  return response;
}

// Interpreta los comandos enviados por SMS
void rx_sms(SIM800ThreadedSMS& modem, String& Number, String& Message)
{
	uint8_t i, index, num_entrada;
	int indexStart, indexEnd;
	String newPIN, newName, command, aux, SMS_out, newNumber;

 	if(Message.startsWith(Options.PIN)){			      // Verifico que el SMS comience con el PIN autorizado
		indexStart = 5;                          // Desplazo hasta el primer caracter del comando
    	Message = Message.substring(indexStart);  // Recorto el pin, Message comienza ahora con el comando
    	DEBUG_PRINT("Mensaje recibido: ");
      DEBUG_PRINTLN(Message);
      DEBUG_PRINT("De: ");
      DEBUG_PRINTLN(Number);
		for(i=0; i<NUM_COMANDOS;i++){
			if(Message.startsWith(comandos[i])){      // Comparo con la lista de comandos
				command = comandos[i];	
				indexStart = command.length() + 1; // Ajusto el indice del comienzo del valor después del comando
				switch (i)
				{
					case ACTIVAR:					// Activa la Alarma - SMS: PIN ACTIVAR
					SMS_out = "Alarma Activada";
          Status.AlarmNextStatus = ARMED_AWAY;
					break;
					case DESACTIVAR:				// Desactiva la alarma - SMS: PIN DESACTIVAR
					SMS_out = "Alarma Desactivada";
          Status.AlarmNextStatus = DISARMED;
					break;
					case INFO:						// Envia SMS con info - SMS: PIN INFO
					SMS_out = "Alarm " + String(AlarmStatus_txt[Status.AlarmStatus]);
          SMS_out += " - ";
          for (int i = 0; i < ALARM_INPUTS; i++)
          {
            SMS_out += (String)Options.inputs_names[i];
            SMS_out += "/"; 
            SMS_out += (Status.Entrada[i])?"OPEN":"CLOSED";
            if(i<ALARM_INPUTS-1)  SMS_out += ", ";
          }
          SMS_out += " - VAC ";
          SMS_out += (!Status.Vac) ? "OK" : "ERROR";
          break;
					case SAVE:						// Guardo el número en la lista de receptores de avisos - SMS: PIN SAVE NUMERO
					indexEnd = Message.length();
					newNumber = Message.substring(indexStart, indexEnd);
					for(index = 0; index < NUM_PHONES; index++){
						if(String(Options.reg_numbers[index]) == "" || String(Options.reg_numbers[index]).startsWith("0")){
              if(check_number(newNumber)){
                newNumber.toCharArray(&Options.reg_numbers[index][0], sizeof(Options.reg_numbers[index]));	
							  saveEEPROM();	// Guardo en memoria
                Options.active_numbers[index] = true;
							  SMS_out = newNumber + " Guardado";
              }
              else{
                SMS_out = newNumber + " NO valido";
              }
							break;
						} 
					}
					if(index >= NUM_PHONES){
						SMS_out = "Registro lleno, borre uno para continuar";
					}
					break;
					case DELETE:					// Elimino el número de la lista de receptores de aviso, o todos - SMS: PIN DELETE NUMERO / PIN DELETE ALL
					indexEnd = Message.length();
					newNumber = Message.substring(indexStart, indexEnd);
					aux = "";
					if(newNumber.startsWith("ALL")){
						for(index = 0; index < NUM_PHONES; index++){	// Borro todos los registros
							aux.toCharArray(&Options.reg_numbers[index][0], sizeof(Options.reg_numbers[index]));
						}
						SMS_out = "Todos los registros Borrados";
						saveEEPROM();
					}
					else{
						for(index = 0; index < NUM_PHONES; index++){	
							if(String(Options.reg_numbers[index]) == newNumber){										// Si encuentro el numero en el registro
								aux.toCharArray(&Options.reg_numbers[index][0], sizeof(Options.reg_numbers[index])); 	// Lo borro
								saveEEPROM();
								index = NUM_PHONES;
                Options.active_numbers[index] = false;
								SMS_out = newNumber + " Borrado";
								break;
							}
							else{
								SMS_out = "No se encontro el numero";
							}
						}
					}
					break;
					case LIST:						// Envio SMS con lista de receptores de avisos - SMS: PIN LIST
					readEEPROM();
					SMS_out = "Numeros registrados: ";
					for(index = 0; index < NUM_PHONES; index++){
            if(Options.active_numbers[index] == 1){
              SMS_out += String(index + 1) + " " + String(Options.reg_numbers[index]);
            }
						if(index <(NUM_PHONES-1)) SMS_out += ", ";	
					}
					break;
					case PIN:						// Cambio el PIN - SMS: ANTIGUO_PIN PIN NUEVO_PIN
					indexEnd = Message.length();
					newPIN = Message.substring(indexStart, indexEnd);
					if(newPIN.length() != 4){
						SMS_out = "El pin debe tener 4 digitos";
					}
					else{
						newPIN.toCharArray(Options.PIN, sizeof(Options.PIN));
						SMS_out = "PIN cambiado a " + String(Options.PIN);
						saveEEPROM();
					}
					break;
					case ENTRADA:					// Cambio el nombre de una entrada - SMS: PIN ENTRADA NUM_ENTRADA NOMBRE_ENTRADA
					num_entrada = Message.substring(indexStart, Message.indexOf(" ", indexStart)).toInt();
					if((num_entrada > 0) && (num_entrada <= ALARM_INPUTS)){
						num_entrada -= 1;
						indexStart = Message.indexOf(" ", indexStart) + 1;
						indexEnd = Message.length();
						newName = Message.substring(indexStart, indexEnd);
						newName.toCharArray(&Options.inputs_names[num_entrada][0], sizeof(Options.inputs_names[num_entrada]));
						saveEEPROM();
						SMS_out = "Entrada " + String(num_entrada + 1) + String(" actualizado a ") + newName; 
					}
					else{
						SMS_out = "Comando correcto: PIN ENTRADA (1-8) NOMBRE_ENTRADA";
					}
					break;
					case FACTORY:					// Borra todos los datos de memoria - SMS: PIN FACTORY
					defaultEEPROM();
					SMS_out = "Memoria restaurada de fabrica";
					break;
					default:	break;
				}
				break;
			}
			else
			{
				SMS_out = "Comando Inválido";
			}
		}
	}
	else{
		SMS_out = "ERROR de PIN";
	}
  if(Number.length()<7){  // SMS recibido de un número no válido
    DEBUG_PRINTLN("Numero invalido");
    //SIM800.EraseSMS();    // Borra todos los mensajes recibidos
  }
  else{
    DEBUG_PRINT("Respuesta: ");
    DEBUG_PRINTLN(SMS_out);
    DEBUG_PRINT("Enviada a ");
    DEBUG_PRINTLN(Number);
    SIM800.sendSMS(Number, SMS_out);
  }
  
}

// Envia avisos por SMS a los numeros registrados
bool AvisoSMS()
{
  bool send_complete = false;
  static uint8_t send_index = 0;
  String Number = String(Options.reg_numbers[send_index]);
  String Message = "";
  switch (Status.AlarmStatus)
  {
  case TRIGGERED: Message = "Alarma Disparada!, motivo: " + String(Options.inputs_names[Status.TriggerCause]);
                  break;
  case ARMED_AWAY: Message = "Alarma Activada";
                  break;
  case DISARMED:  Message = "Alarma Desactivada";
                  break;                                     
  default:  if(!Status.Vac){
              Message = "Corte de Energia, bateria " + String(Status.Vbat) + "V";
            }
            else{
              Message = "Energia Reestablecida";
            }
            break;
  }
  
  if(!Number.startsWith("0") && Options.active_numbers[send_index]){ // Verifico que el número sea válido y esté habilitado
    if(SIM800.getBusy() == 0){  // Verifico si está libre para enviar
      SIM800.sendSMS(Number, Message);
      DEBUG_PRINT("Mensaje: ");
      DEBUG_PRINT(Message);
      DEBUG_PRINT(" - Enviado a ");
      DEBUG_PRINTLN(Number);
      if(send_index < NUM_PHONES){
        send_index++;
      }
      else{
        send_index = 0;
        send_complete = true;
      } 
    }
  }
  else{
    if(send_index < NUM_PHONES){
      send_index++;
    }
    else{
      send_index = 0;
      send_complete = true;
    } 
  }

  return send_complete;
}

// Callback cuando se inicializa el modulo
void startup(SIM800ThreadedSMS& modem)
{
	DEBUG_PRINTLN("SIM800 Ready");
  //SIM800.EraseSMS();
  Status.GsmStatus = true;
}

// Callback para reiniciar el SIM800
void power(SIM800ThreadedSMS& modem, bool mode){
  digitalWrite(SIM_RES,mode);
  DEBUG_PRINT("SIM800 Power ");
  DEBUG_PRINTLN((mode)?"ON":"OFF");
  Status.GsmStatus = false;  
}

// Ver https://m2msupport.net/m2msupport/atcsq-signal-quality/
void signal(SIM800ThreadedSMS& modem, SIM800ThreadedSMS::SignalLevel& Signal){
  Status.GsmSignal = map(Signal.Value,0,30,0,100);
  DEBUG_PRINT("SIM800 Signal: ");
  DEBUG_PRINT(Signal.Value);
  DEBUG_PRINT("%, ");
  DEBUG_PRINT(Signal.Dbm);
  DEBUG_PRINTLN(" dBm");
}
#endif
#pragma endregion

#pragma region SETUP
void setup() {
  pinMode(LED, OUTPUT);
  pinMode(SIM_RES, OUTPUT);
  pinMode(SIR1, OUTPUT);
  pinMode(SIR2, OUTPUT);
  pinMode(LED_EXT, OUTPUT);
  // Seteo pines de las entradas de la alarma y llavero RF
  for(uint8_t i=0;i<ALARM_INPUTS;i++){
    pinMode(ALARM_INPUT[i], INPUT_PULLUP);
    Status.Entrada[i] = digitalRead(ALARM_INPUT[i]);
#ifdef USE_RF    
    if(i<RF_INPUTS){
      pinMode(RF_INPUT[i], INPUT_PULLUP);
      Status.RFin[i] = digitalRead(RF_INPUT[i]);
    }    
#endif    
  }

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
  
#ifdef USE_GSM
  SerialModem.begin(ModemBaudRate);

  SIM800.begin();

	//SIM800.setInterval(SIM800ThreadedSMS::INTERVAL_CLOCK, 60000);
	SIM800.setInterval(SIM800ThreadedSMS::INTERVAL_SIGNAL, treintaMinutos);
	SIM800.setInterval(SIM800ThreadedSMS::INTERVAL_INBOX, treintaSegundos);
	SIM800.setHandlers({
		//.signal = signal,
		.signal = signal,
		//.clock = clock,
		.clock = NULL,
		.incoming = rx_sms,
		//.incoming = NULL,
		.ready = startup,
		//.ready = NULL,
		.outgoing = NULL,
		.power = power
	});
#endif

#ifdef USE_EEPROM
  readEEPROM();                                 // Leo la EEPROM y cargo la estructura Options
  if(Options.data_set != 'T') defaultEEPROM();  // Si es la primera vez que se inicia y la memoria está en blanco, cargo datos por default
#endif

#ifdef USE_RANDOM_SENSORS  
  timer = millis();
  randomSeed(analogRead(0));
#else
#ifdef USE_SENSOR_POWER
  pzem.setAddress(ip);
  DEBUG_PRINTLN(F("PZEM004T Power Meter Started"));
#endif
#ifdef USE_SENSOR_DHT22
  dht.begin();
DEBUG_PRINTLN(F("DHT22 Temperature/Humidity Sensor Started"));
#endif
#ifdef USE_SENSOR_18B20
  sensors.begin();
  DEBUG_PRINTLN(F("DS18B20 Temperature sensor Started"));
#endif
  t_sensores.every(diezMinutos, mqttPublishSensors, (void*)0);   // Lee y publica la informacion de sensores cada 10 minutos
#endif

  t_mqtt.every(unaHora, mqttPublishAll, (void*)0); // Publica todo cada 1 hora

}
#pragma endregion

void loop(){
  esp.Process();
#ifdef USE_GSM
  SIM800.loop();      // Procesa los SMS entrantes/salientes
#endif
  AlarmInputsCheck(); // Verifica las entradas de la alarma
#ifdef USE_RF
  RfInputsCheck();    // Verifica las entradas de los llaveros RF
#endif  
  checkAlarma();      // Verifica los estados de la alarma
  t_sensores.update();// Update de los sensores
  t_leds.update();    // Update del estado de los leds
  t_mqtt.update();    // Update del estado del timer MQTT

#ifdef USE_RANDOM_SENSORS
  // Genero valores aleatorios para los sensores y los publico
  if(millis()- timer > 30000){
    Status.Vac ^=1;
    //Status.TriggerCause = random(7);
    Status.Vbat = (float)random(110,140)/10;
    Status.OutsideTemp = (float)random(400)/10;
    Status.OutsideHum = random(99);
    Status.InsideTemp = (float)random(400)/10;
    Status.LumExt = random(100);
    //Status.GsmStatus ^=1;
    //Status.GsmSignal = random(100);
    Status.voltage = (float)random(2000,2300)/10;
    Status.current = (float)random(1000)/100;
    Status.power = (float)random(1000000)/100;
    Status.energy = (float)random(100000)/10;
    timer = millis();
  }
#endif    

#ifdef USE_GSM
  if(EnviarAvisoSMS) EnviarAvisoSMS = !AvisoSMS();  // Envia el aviso por SMS en caso de disparo de la alarma
                                                    // el flag EnviarAvisoSMS pasa a false cuando termina con todos los n° de la agenda
#endif                                                    

}



