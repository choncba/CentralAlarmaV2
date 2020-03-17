//////////////////////////////////////////////////////////////////////////////////////
//        Central de Alarma con conectividad MQTT a través de ESP-Link              //
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
// Panel de alarma HA:                                                              //
// https://www.home-assistant.io/components/alarm_control_panel.mqtt/               //
// Envia los comandos: 'DISARM', 'ARM_HOME', 'ARM_AWAY' desde el front end          //
//                                                                                  //
//////////////////////////////////////////////////////////////////////////////////////
//
//  Script para subir el sketch con avrdude:
//  C:\Program Files (x86)\Arduino\hardware\tools\avr\bin>avrdude -DV -patmega1284p -Pnet:192.168.1.20:23 -carduino -b115200 -U flash:w:C:\Users\ChoN\OneDrive\Platform\CentralAlarma\.pioenvs\mightycore1284\firmware.hex:i -C C:\Users\ChoN\AppData\Local\Arduino15\packages\MightyCore\hardware\avr\2.0.1\avrdude.conf
//
/////////////////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <EEPROM.h>
// https://github.com/MCUdude/MightyCore#pinout
// Setup: Definicion de pines Standard
//      Nombre  Arduino   PIN
#define LED     0         // PB0
#define OW_PIN  1         // PB1
#define VAC     2         // PB2
#define NA1     3         // PB3
#define NA2     4         // PB4
#define pwrRX   5         // PB5
#define pwrTX   6         // PB6
#define SIM_RES 7         // PB7
#define RX0     8         // PD0
#define TX0     9         // PD1
#define RX1     10        // PD2
#define TX1     11        // PD3
#define SIR2    12        // PD4
#define SIR1    13        // PD5
#define LED_EXT 14        // PD6
#define DHT_PIN 15        // PD7
#define IN1     16        // PC0
#define IN2     17        // PC1
#define IN3     18        // PC2
#define IN4     19        // PC3
#define IN5     20        // PC4
#define IN6     21        // PC5
#define IN7     22        // PC6
#define IN8     23        // PC7
#define VBAT    24        // PA0
#define RFD     25        // PA1
#define RFC     26        // PA2
#define RFB     27        // PA3
#define RFA     28        // PA4
#define NA5     29        // PA5
#define NA6     30        // PA6
#define LCR     31        // PA7

#define SerialDebug     Serial
#define SerialModem     Serial1        // Modulo SIM800 conectado al Serial1 por hardware

#define USE_GSM         // Modulo GSM SIM800L
#define USE_SENSORES    // Habilita/Deshabilita DHT22, 18B20 y LCR
#define USE_RF          // Habilita/Deshabilita entradas RF
#define USE_POWER_METER // Habilita/Deshabilita medidor de potencia

// Manejo de JSON para mensajes MQTT
#include <ArduinoJson.h>

// Interface SLIP MQTT con ESP-Link
#include <ELClient.h>
#include <ELClientMqtt.h>

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

bool connected;       // Flag que indica conexión MQTT OK
#define RETAIN  true  // Flag retain para publicaciones mqtt
#define QoS     0     // QoS de payload MQTT

#ifdef USE_SENSORES
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTTYPE DHT22
DHT_Unified dht(DHT_PIN, DHTTYPE);

#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(OW_PIN);
DallasTemperature sensors(&oneWire);
#endif

#ifdef USE_GSM
#include <ThreadedGSM.h>
ThreadedGSM SIM800(SerialModem);
#define ModemBaudRate  115200
#define ALARM_INPUTS 8	// Cantidad de entradas de la alarma
#define NUM_REGISTROS 5
#define NUM_COMANDOS 10
const char comandos[NUM_COMANDOS][12] = {"ACTIVAR","DESACTIVAR","INFO","SAVE","DELETE","LIST","PIN","ENTRADA","FACTORY", "INPUTFUN"};
enum comandos_enum {ACTIVAR=0,DESACTIVAR,INFO,SAVE,DELETE,LIST,PIN,ENTRADA,FACTORY,INPUTFUN};
const char modes[5][20] = {"Desactivada", "ACT Total/Parcial", "ACT Total", "ACT Siempre", "ACT Demorada"};
// Estructura para guardar variables en eeprom
struct data
{
	char data_set;
	char inputs_names[ALARM_INPUTS][20];	// Nombres de las entradas
  char inputs_function[ALARM_INPUTS];
	char PIN[5];							// clave PIN
	char reg_numbers[NUM_REGISTROS][15];	// Números de teléfono de la agenda
}datosGSM;

#endif

#ifdef USE_POWER_METER
#include <PZEM004T.h>         // Libreria de manejo del modulo medidor de potencia, usa softwareserial
PZEM004T pzem(pwrRX, pwrTX);  // (RX,TX) connect to TX,RX of PZEM
IPAddress ip(192,168,1,1);
#endif

#include "Timer.h"  // Ver MightyCore
Timer t_leds, t_sensores, t_mqtt, t_gsm;

int timerLEDS;
const unsigned long unSegundo = 1000L;  // 1000 mSeg
const unsigned long treintaSegundos = 30 * unSegundo; // 30000 ms / 30 seg
const unsigned long unMinuto = 60 * unSegundo; // 60000 ms / 1 min
const unsigned long diezMinutos = 10 * unMinuto; // 600000 ms/10 min
const unsigned long treintaMinutos = 30 * unMinuto; // 600000 ms/10 min

#pragma region Variables Globales
// Entradas Alarma
#define INPUTS 8
const uint8_t Entrada[INPUTS] = { IN1, IN2, IN3, IN4, IN5, IN6, IN7, IN8 };
enum Entrada_enum {in1 = 0, in2, in3, in4, in5, in6, in7, in8 };
const char Entrada_txt[INPUTS][4] = { "in1", "in2", "in3", "in4", "in5", "in6", "in7", "in8" };

#define NUM_STATUS 5
enum Alarm_Status_enum { DISARMED=0, ARMED_HOME, ARMED_AWAY, PENDING, TRIGGERED };
const char Alarm_Status_txt[NUM_STATUS][11] = { "disarmed", "armed_home", "armed_away", "pending", "triggered"};

// Salidas
#define OUTPUTS 3
const uint8_t Salida[OUTPUTS] = { SIR1, SIR2, LED_EXT };
enum Salidas_enum { SIRENA1 = 0, SIRENA2, LED_CAMPANA };
const char Salida_txt [OUTPUTS][11] = { "sirena_ext", "sirena_int", "led_ext"};
// Entradas RF
#define RF_INPUTS 4
const uint8_t RfIN[RF_INPUTS] = { RFA, RFB, RFC, RFD };
enum teclas_rf { TECLA_A = 0, TECLA_B, TECLA_C, TECLA_D };
const char TeclaRf_txt [RF_INPUTS][8] = { "tecla_a", "tecla_b", "tecla_c", "tecla_d" };

struct Status {
	uint8_t Entrada[INPUTS] = { 1,1,1,1,1,1,1,1 };
  uint8_t AlarmStatus = 0;
  uint8_t AlarmNextStatus = 0;
  uint8_t TriggerCause = 0;
	bool Vac = 0;
  float Vbat = 0;
  float OutsideTemp = 0;
  float OutsideHum = 0;
  float InsideTemp = 0;
  uint8_t LumExt = 0;
  uint8_t RFin[RF_INPUTS] = { 1,1,1,1 };
  bool GsmStatus;
  uint8_t GsmSignal;
#ifdef USE_POWER_METER
  float voltage;
  float current;
  float power;
  float energy;
#endif
}Central;

const char NombreTopic[] = {"/CentralAlarma"};
String StringTopic = "";
char CharTopic[100];
String StringData = "";
char CharData[10];

#ifdef USE_GSM 
char telefono[20];
char texto[100];
int id_timerAviso = 0;
#endif
#pragma endregion

// Prototipo de funciones
void PublicarMQTT(void *);
void AvisoGSM(void *);
void beep(void);
void BlinkLeds(void *);
void ActivarSirenas(uint8_t);
void getDataEEPROM(void);
void eraseData(void);

#pragma region Lectura de entradas y sensores
#ifdef USE_SENSORES
void LeerSensores(void* context)
{
  (void)context;
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if(!isnan(event.temperature)) Central.OutsideTemp = event.temperature;

  dht.humidity().getEvent(&event);
  if(!isnan(event.relative_humidity)) Central.OutsideHum = event.relative_humidity;

  sensors.requestTemperatures();
  float int_temp = sensors.getTempCByIndex(0);
  if(!isnan(int_temp))  Central.InsideTemp = int_temp;

  Central.LumExt = 0;
  int lectura_vbat=0;
  int lectura_lcr = 0;
  for(int i=0; i<10; i++)
  {
    lectura_lcr += analogRead(LCR);
    lectura_vbat += analogRead(VBAT);
    delay(10);
  }
  lectura_lcr = lectura_lcr/10;
  Central.LumExt = map(lectura_lcr, 0, 1023, 100, 0);
  lectura_vbat = lectura_vbat/10;
  Central.Vbat = lectura_vbat * 0.01967;  // Vbat = Vad.(R1+R2)/R2; Vad=Lectura.5/1024 => Vbat = Lectura.(R1+R2)/R2).(5/1024)
  Central.Vac = digitalRead(VAC);

#ifdef USE_POWER_METER
  float v_med = pzem.voltage(ip);
  if (v_med > 0.0) Central.voltage = v_med;

  float i_med = pzem.current(ip);
  if(i_med >= 0.0) Central.current = i_med;
  
  float p_med = pzem.power(ip);
  if(p_med >= 0.0) Central.power = p_med;
  
  float e_med = pzem.energy(ip);
  if(e_med >= 0.0) Central.energy = e_med;
#endif
}
#endif

void CheckEntradas(){
  static uint8_t RoundCheck[INPUTS] = { 0,0,0,0,0,0,0,0 };
  const uint8_t RoundCheckThreshole = 8;

  for (uint8_t cnt = 0; cnt < INPUTS; cnt++)
  {
    uint8_t curStatus = digitalRead(Entrada[cnt]);
    if (Central.Entrada[cnt] != curStatus)
    {
      delay(5);
      curStatus = digitalRead(Entrada[cnt]);
      if (Central.Entrada[cnt] != curStatus)
      {
        RoundCheck[cnt]++;
      }
      else  RoundCheck[cnt] = 0;

      if (RoundCheck[cnt] >= RoundCheckThreshole)
      {
        Central.Entrada[cnt] = curStatus;
        RoundCheck[cnt] = 0;
        if(Central.AlarmStatus == ARMED_HOME) // Verifico si la alarma está activada en casa
        {
          if(cnt < 6) // Reservo las entradas 7 y 8 para sensores dentro de la casa cuando estoy en ARMED_HOME
          {
            Central.AlarmNextStatus = TRIGGERED;  // Disparo la alarma con entradas 1 a 6
            Central.TriggerCause = cnt;           // Guardo que entrada causo el disparo
          }
        }
        if(Central.AlarmStatus == ARMED_AWAY)
        {
          Central.AlarmNextStatus = TRIGGERED;    // Disparo la alarma
          Central.TriggerCause = cnt;             // Guardo que entrada causo el disparo
        }
        PublicarMQTT((void*)0);
      }
    }
  }
}

#ifdef USE_RF
void CheckRF(){
  static uint8_t RoundCheck[RF_INPUTS] = { 0,0,0,0 };
  const uint8_t RoundCheckThreshole = 8;

  for (uint8_t cnt = 0; cnt < RF_INPUTS; cnt++)
  {
    uint8_t curStatus = digitalRead(RfIN[cnt]);
    if (Central.RFin[cnt] != curStatus)
    {
      delay(5);
      curStatus = digitalRead(RfIN[cnt]);
      if (Central.RFin[cnt] != curStatus)
      {
        RoundCheck[cnt]++;
      }
      else  RoundCheck[cnt] = 0;

      if (RoundCheck[cnt] >= RoundCheckThreshole)
      {
        Central.RFin[cnt] = curStatus;
        RoundCheck[cnt] = 0;
        switch(cnt)
        {
          case TECLA_A: Central.AlarmNextStatus = ARMED_AWAY;
                        break;
          case TECLA_B: Central.AlarmNextStatus = ARMED_HOME;
                        break;
          case TECLA_C: Central.AlarmNextStatus = DISARMED;
                        break;
          case TECLA_D:
                        break;
          default:      break;
        }
        PublicarMQTT((void*)0);
      }
    }
  }
}
#endif
#pragma endregion

#pragma region Funciones MQTT
// Callback made from esp-link to notify of wifi status changes
void wifiCb(void* response) {
  ELClientResponse *res = (ELClientResponse*)response;
  if (res->argc() == 1) {
    uint8_t status;
    res->popArg(&status, 1);

    if(status == STATION_GOT_IP) {
      Serial.println(F("WIFI CONNECTED"));
    } else {
      Serial.print(F("WIFI NOT READY: "));
      Serial.println(status);
    }
  }
}

// Callback when MQTT is connected
void mqttConnected(void* response) {
  Serial.println(F("MQTT connected!"));
  connected = true;

  StringTopic = String(NombreTopic) + "/set";
  StringTopic.toCharArray(CharTopic, sizeof(CharTopic));
  mqtt.subscribe(CharTopic);

  StringTopic = String(NombreTopic) + "/status/LWT";
  StringTopic.toCharArray(CharTopic, sizeof(CharTopic));
  mqtt.publish(CharTopic,"online",QoS,RETAIN);

#ifdef USE_SENSORES
  LeerSensores((void*)0);
#endif  

  PublicarMQTT((void*)0);
}

// Callback when MQTT is disconnected
void mqttDisconnected(void* response) {
  Serial.println(F("MQTT disconnected"));
  connected = false;
}

// Callback when an MQTT message arrives for one of our subscriptions
void mqttData(void* response) {
  ELClientResponse *res = (ELClientResponse *)response;

  Serial.print(F("Received: topic="));
  String topic = res->popString();
  Serial.println(topic);

  Serial.print(F("data="));
  String data = res->popString();
  Serial.println(data);

  if(topic.indexOf("/set")>0)  // Si el topic contiene "/set"
  {
    if(data.equals("DISARM"))
    {
      Central.AlarmNextStatus = DISARMED;
      Serial.println("Alarma desactivada via MQTT");
    }
    if(data.equals("ARM_HOME"))
    {
      Central.AlarmNextStatus = ARMED_HOME;
      Serial.println("Alarma Activada en casa via MQTT");
    } 
    if(data.equals("ARM_AWAY"))
    {
      Central.AlarmNextStatus = ARMED_AWAY;
      Serial.println("Alarma Activada Fuera de casa via MQTT");
    }
  }
}

void mqttPublished(void* response) {
  Serial.println(F("MQTT published"));
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
    central_alarma_alarm["status"] = Alarm_Status_txt[Central.AlarmStatus];
    central_alarma_alarm["trigger_cause"] = Entrada_txt[Central.TriggerCause];
    
    JsonArray central_alarma_alarm_inputs_status = central_alarma_alarm.createNestedArray("inputs_status");
    JsonArray central_alarma_alarm_inputs_names = central_alarma_alarm.createNestedArray("inputs_names");
    JsonArray central_alarma_alarm_inputs_function = central_alarma_alarm.createNestedArray("inputs_function");
    for(i=0; i<INPUTS;i++){
      central_alarma_alarm_inputs_status.add(Central.Entrada[i]);
      central_alarma_alarm_inputs_names.add(datosGSM.inputs_names[i]);
      central_alarma_alarm_inputs_function.add(datosGSM.inputs_function[i]);
    }

    JsonObject central_alarma_sensors = central_alarma.createNestedObject("sensors");
    central_alarma_sensors["temp_int"] = Central.InsideTemp;
    central_alarma_sensors["temp_ext"] = Central.OutsideTemp;
    central_alarma_sensors["hum_ext"] = Central.OutsideHum;
    central_alarma_sensors["lcr"] = Central.LumExt;
    central_alarma_sensors["v_bat"] = Central.Vbat;
    central_alarma_sensors["vac"] = Central.Vac;
    central_alarma_sensors["voltage"] = Central.voltage;
    central_alarma_sensors["current"] = Central.current;
    central_alarma_sensors["power"] = Central.power;
    central_alarma_sensors["energy"] = Central.energy;

    JsonObject central_alarma_gsm = central_alarma.createNestedObject("gsm");
    central_alarma_gsm["status"] = Central.GsmStatus;
    central_alarma_gsm["signal_level"] = Central.GsmSignal;

    JsonArray central_alarma_rf = central_alarma.createNestedArray("rf");
    for(i=0; i<RF_INPUTS;i++){
      central_alarma_rf.add(Central.RFin[i]);
    }
    
    char DataMQTT[capacity];
    mqtt.publish(NombreTopic, DataMQTT, QoS, RETAIN);

    //size_t n = serializeJson(doc, DataMQTT);
    //mqtt.publish(NombreTopic, DataMQTT, n, QoS, RETAIN);
  }
}
#pragma endregion

#pragma region Funciones GSM
#ifdef USE_GSM
void rx_sms(ThreadedGSM& modem, String& Number, String& Message)
{
	uint8_t i, index, num_entrada, num_funcion;
	int indexStart, indexEnd;
	String newPIN, newName, command, aux, SMS_out, newNumber;

	if(Message.startsWith(datosGSM.PIN)){			    // Verifico que el SMS comience con el PIN autorizado
		indexStart = 5;                             // Desplazo hasta el primer caracter del comando
    	Message = Message.substring(indexStart);  // Recorto el pin, Message comienza ahora con el comando
    	Serial.print("Message: ");
    	Serial.println(Message);
		for(i=0; i<NUM_COMANDOS;i++){
			if(Message.startsWith(comandos[i])){      // Comparo con la lista de comandos
				command = comandos[i];	
				indexStart = command.length() + 1; // Ajusto el indice del comienzo del valor después del comando
				switch (i)
				{
					case ACTIVAR:					// Activa la Alarma - SMS: PIN ACTIVAR
          Central.AlarmNextStatus = ARMED_AWAY;
					SMS_out = "Alarma Activada";
					break;
					case DESACTIVAR:				// Desactiva la alarma - SMS: PIN DESACTIVAR
          Central.AlarmNextStatus = DISARMED;
					SMS_out = "Alarma Desactivada";
					break;
					case INFO:						// Envia SMS con info - SMS: PIN INFO
          SMS_out = "Alarma ";
          SMS_out += (Central.AlarmStatus == ARMED_AWAY) ? "ACTIVADA" : "DESACTIVADA";
          SMS_out += ", Entradas: ";
          for (int i = 0; i < INPUTS; i++)
          {
            SMS_out += String(i+1);
            SMS_out += " ";
            SMS_out += (Central.Entrada[i]) ? "ON" : "OFF";
            SMS_out += ", ";
          }
          SMS_out += "220VAC ";
          SMS_out += (!Central.Vac) ? "OK" : "ERROR";
					SMS_out = "Enviar INFO";
					break;
					case SAVE:						// Guardo el número en la lista de receptores de avisos - SMS: PIN SAVE NUMERO
					indexEnd = Message.length();
					newNumber = Message.substring(indexStart, indexEnd);
					for(index = 0; index < NUM_REGISTROS; index++){
						if(String(datosGSM.reg_numbers[index]) == ""){
							newNumber.toCharArray(&datosGSM.reg_numbers[index][0], sizeof(datosGSM.reg_numbers[index]));	
							EEPROM.put(0, datosGSM);	// Guardo el número en memoria
							SMS_out = newNumber + " Guardado";
							break;
						} 
					}
					if(index >= NUM_REGISTROS){
						SMS_out = "Registro lleno, borre uno para continuar";
					}
					break;
					case DELETE:					// Elimino el número de la lista de receptores de aviso, o todos - SMS: PIN DELETE NUMERO / PIN DELETE ALL
					indexEnd = Message.length();
					newNumber = Message.substring(indexStart, indexEnd);
					SerialDebug.print("Numero a borrar: ");
					SerialDebug.println(newNumber);
					aux = "";
					if(newNumber.startsWith("ALL")){
						for(index = 0; index < NUM_REGISTROS; index++){	// Borro todos los registros
							aux.toCharArray(&datosGSM.reg_numbers[index][0], sizeof(datosGSM.reg_numbers[index]));
						}
						SMS_out = "Todos los registros Borrados";
						EEPROM.put(0, datosGSM);
					}
					else{
						for(index = 0; index < NUM_REGISTROS; index++){	
							if(String(datosGSM.reg_numbers[index]) == newNumber){										// Si encuentro el numero en el registro
								aux.toCharArray(&datosGSM.reg_numbers[index][0], sizeof(datosGSM.reg_numbers[index])); 	// Lo borro
								EEPROM.put(0, datosGSM);
								index = NUM_REGISTROS;
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
					EEPROM.get(0, datosGSM);
					SMS_out = "Numeros registrados: ";
					for(index = 0; index < NUM_REGISTROS; index++){
						SMS_out += String(index + 1) + " " + String(datosGSM.reg_numbers[index]);
						if(index <(NUM_REGISTROS-1)) SMS_out += ", ";	
					}
					break;
					case PIN:						// Cambio el PIN - SMS: ANTIGUO_PIN PIN NUEVO_PIN
					indexEnd = Message.length();
					newPIN = Message.substring(indexStart, indexEnd);
					if(newPIN.length() != 4){
						SMS_out = "El pin debe tener 4 digitos";
					}
					else{
						newPIN.toCharArray(datosGSM.PIN, sizeof(datosGSM.PIN));
						SMS_out = "PIN cambiado a " + String(datosGSM.PIN);
						EEPROM.put(0, datosGSM);
					}
					break;
					case ENTRADA:					// Cambio el nombre de una entrada - SMS: PIN ENTRADA NUM_ENTRADA NOMBRE_ENTRADA
					num_entrada = Message.substring(indexStart, Message.indexOf(" ", indexStart)).toInt();
					if((num_entrada > 0) && (num_entrada <= ALARM_INPUTS)){
						num_entrada -= 1;
						indexStart = Message.indexOf(" ", indexStart) + 1;
						indexEnd = Message.length();
						newName = Message.substring(indexStart, indexEnd);
						newName.toCharArray(&datosGSM.inputs_names[num_entrada][0], sizeof(datosGSM.inputs_names[num_entrada]));
						EEPROM.put(0, datosGSM);
						SMS_out = "Entrada " + String(num_entrada + 1) + String(" actualizado a ") + newName; 
					}
					else{
						SMS_out = "Comando correcto: PIN ENTRADA (1-8) NOMBRE_ENTRADA";
					}
					break;
					case FACTORY:					// Borra todos los datos de memoria - SMS: PIN FACTORY
					eraseData();
					SMS_out = "Memoria restaurada de fabrica";
					break;
          case INPUTFUN:        // Des/Activa una entrada o cambia su funcion - SMS INPUTFUN NUM_ENTRADA:FUNCION - ej 1234 INPUTFUN 1:3
                                // Entradas: 1 a 8
                                // Funciones:
                                // 0 - Entrada Desactivada
                                // 1 - Entrada Activada - Dispara en Armado Total y Parcial
                                // 2 - Entrada Activada - Dispara solo en Armado Total (Sensor interno)
                                // 3 - Entrada Activada - Dispara siempre (Tamper campana/Botón Pánico)
                                // 4 - Entrada Activada - Disparo demorado (Puertas entrada/salida)
          num_entrada = Message.substring(indexStart, Message.indexOf(":", indexStart)).toInt();
          num_funcion = Message.substring(Message.indexOf(":", indexStart) + 1).toInt();
          if(datosGSM.inputs_function[num_entrada] != num_funcion){
            datosGSM.inputs_function[num_entrada] = num_funcion;
            EEPROM.put(0, datosGSM);
          }
          SMS_out = "Entrada " + String(num_entrada + 1) + String(" - ") + String(modes[num_funcion]);
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
	SerialDebug.print("Respuesta: ");
	SerialDebug.println(SMS_out);
	SerialDebug.print("Enviada a ");
	SerialDebug.println(Number);
	SIM800.sendSMS(Number, SMS_out);
}

void signal(ThreadedGSM& modem, ThreadedGSM::SignalLevel& Signal)
{
	SerialDebug.print("Modem signal: Dbm:");
	SerialDebug.print(Signal.Dbm);
	SerialDebug.print(" value: ");
	SerialDebug.println(Signal.Value);
  Central.GsmStatus = true; // verificar el estado acá
}

void startup(ThreadedGSM& modem)
{
	Serial.println("SIM800 Ready");
  Central.GsmStatus = true;
}

void AvisoGSM(void * context)
{
  (void)context;
  String SMS_out = "ALARMA DISPARADA! Motivo: Entrada " + String(datosGSM.inputs_names[Central.TriggerCause]);
  String sendNUM;
  for(int index = 0; index < NUM_REGISTROS; index++){
    if(String(datosGSM.reg_numbers[index]) != ""){
      sendNUM = datosGSM.reg_numbers[index];
      SIM800.sendSMS(sendNUM, SMS_out);
    } 
	}
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

  Serial.begin(115200);

  getDataEEPROM();
  
  Serial.println(F("EL-Client starting!"));
  // Sync-up with esp-link, this is required at the start of any sketch and initializes the
  // callbacks to the wifi status change callback. The callback gets called with the initial
  // status right after Sync() below completes.
  esp.wifiCb.attach(wifiCb); // wifi status change callback, optional (delete if not desired)
  bool ok;
  do {
    ok = esp.Sync();      // sync up with esp-link, blocks for up to 2 seconds
    if (!ok) Serial.println(F("EL-Client sync failed!"));
  } while(!ok);
  Serial.println(F("EL-Client synced!"));

  // Set-up callbacks for events and initialize with es-link.
  mqtt.connectedCb.attach(mqttConnected);
  mqtt.disconnectedCb.attach(mqttDisconnected);
  mqtt.publishedCb.attach(mqttPublished);
  mqtt.dataCb.attach(mqttData);
  mqtt.setup();
  mqtt.lwt("/CentralAlarma/status/LWT","offline",QoS,RETAIN);

  Serial.println(F("EL-MQTT ready"));

#ifdef USE_POWER_METER
  pzem.setAddress(ip);
#endif

#ifdef USE_SENSORES
  dht.begin();
  sensors.begin();
  t_sensores.every(unMinuto, LeerSensores, (void*)0);        // Lee la informacion de sonsores cada 1 minuto 
  t_mqtt.every(diezMinutos, PublicarMQTT, (void*)0); // Publica la informacion de sensores cada 10 minutos 
#endif

#ifdef USE_GSM
  digitalWrite(SIM_RES, HIGH);
  SerialModem.begin(ModemBaudRate);
  SIM800.begin();
  SIM800.setInterval(ThreadedGSM::INTERVAL_INBOX, 10*unSegundo);
  SIM800.setInterval(ThreadedGSM::INTERVAL_SIGNAL, diezMinutos);
	SIM800.setHandlers({
		.signal = signal,
		.clock = NULL,
		.incoming = rx_sms,
		.ready = startup,
		.outgoing = NULL,
		.power = NULL
	});
#endif

  t_mqtt.every(treintaMinutos, PublicarMQTT, (void*)0);  // Publica todo cada media hora
}
#pragma endregion

#pragma region LOOP
void loop() {

#ifdef USE_GSM
  SIM800.loop();
  t_gsm.update();
#endif

  CheckEntradas();

#ifdef USE_RF
  CheckRF();
#endif

  t_leds.update();
  t_mqtt.update();
  t_sensores.update();

  if(Central.AlarmStatus != Central.AlarmNextStatus)
  {
    switch(Central.AlarmNextStatus)
    {
      case DISARMED:    t_leds.stop(timerLEDS);
                        digitalWrite(LED, LOW);
                        digitalWrite(LED_EXT, LOW);
                        ActivarSirenas(LOW);
                        if(Central.AlarmStatus == ARMED_AWAY)
                        {
                          beep(); // Suenan las sirenas durante 100 mSeg
                        }
#ifdef USE_GSM          
                        t_gsm.stop(id_timerAviso);
#endif
                        break;
      case ARMED_HOME:  t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(1000, BlinkLeds, (void*)0); // Parpadean los leds cada 1 segundo
                        break;
      case ARMED_AWAY:  t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(1000, BlinkLeds, (void*)0); // Parpadean los leds cada 1 segundo
                        beep(); // Suenan las sirenas durante 100 mSeg
                        break;
      case PENDING:     break;
      case TRIGGERED:   ActivarSirenas(HIGH);
                        t_leds.stop(timerLEDS);
                        timerLEDS = t_leds.every(100, BlinkLeds, (void*)0); // Parpadean los leds cada 100 mSseg
#ifdef USE_GSM
                        id_timerAviso = t_gsm.after(unMinuto, AvisoGSM, (void*)0); // Manda SMS luego de un minuto
#endif
                        break;
      default:          break;      
    }
    Central.AlarmStatus = Central.AlarmNextStatus;
    PublicarMQTT((void*)0);
  }
  
}
#pragma endregion

#pragma region AUX
void ActivarSirenas(uint8_t modo)
{
  digitalWrite(Salida[SIRENA1], modo);
  digitalWrite(Salida[SIRENA2], modo);
}

void BlinkLeds(void* context)
{
  (void) context;
  digitalWrite(LED,!digitalRead(LED));
  digitalWrite(LED_EXT,!digitalRead(LED_EXT));
}

void beep()
{
  t_leds.pulseImmediate(Salida[SIRENA1],100, HIGH);  //Produce un pulso en alto de 100 mSeg y luego queda en bajo
  t_leds.pulseImmediate(Salida[SIRENA2],100, HIGH);
}
#pragma endregion

#pragma region Funciones EEPROM
void getDataEEPROM(){
  EEPROM.get(0, datosGSM);
}

void printDataEEPROM(){
  int i;
  EEPROM.get(0, datosGSM);	      // Obtengo los datos desde la memoria EEPROM

  Serial.println();
  Serial.println("Datos Guardados: ");
  Serial.print("data_set: ");
  Serial.println(datosGSM.data_set);
  Serial.println("Entradas Alarma:");
  for(i = 0; i < 8; i++){
    Serial.println(datosGSM.inputs_names[i]);
  }
  Serial.print("PIN: ");
  Serial.println(datosGSM.PIN);
  Serial.println("Registros:");
  for(i = 0; i < 5; i++){
    Serial.print(i+1);
    Serial.print(" - ");
    Serial.println(datosGSM.reg_numbers[i]);
  }
  Serial.println();
}

void eraseData(){
  int i;
  String aux;
  datosGSM.data_set = 'T';
  for(i = 0; i < ALARM_INPUTS; i++){
    sprintf(&datosGSM.inputs_names[i][0], "IN%d", i+1);
  }
  aux = "1234";
  aux.toCharArray(datosGSM.PIN, sizeof(datosGSM.PIN));
  aux = "";
    for(i = 0; i < NUM_REGISTROS; i++){
    aux.toCharArray(&datosGSM.reg_numbers[i][0], sizeof(datosGSM.reg_numbers[i]));
  }
  EEPROM.put(0, datosGSM);
}
#pragma endregion

