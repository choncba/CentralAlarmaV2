/* View Readme.md */

#include <Arduino.h>
#include "config.h"
#include <EEPROM.h>
#include <ELClient.h>
#include <ELClientMqtt.h>
#include <ArduinoJson.h>
#include "Timer.h"  // Ver MightyCore

#ifdef USE_GSM
#include <ThreadedGSM.h>
ThreadedGSM SIM800(SerialModem);
#endif

ELClient esp(&Serial);
ELClientMqtt mqtt(&esp);

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
	char inputs_names[ALARM_INPUTS][20] = { "","","","","","","","" };	    // Nombres de las entradas
  uint8_t inputs_function[ALARM_INPUTS] = { 1, 1, 1, 1, 1, 1, 1, 1 };   // Funciones de las entradas
	char PIN[5] = "1234";						                                        // clave PIN
	char reg_numbers[NUM_PHONES][15] = { "", "", "", "", "" };	            // Números de teléfono de la agenda
}Options;

bool mqtt_update = false; // Flag para indicar cuando es necesario publicar los estados

// Timers
#ifdef USE_RANDOM_SENSORS
unsigned long timer;
#endif
Timer t_leds, t_sensores, t_mqtt, t_gsm;
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

/***
 * Suscripciones:
 * 
 * Topic: /TestJSON/set
 * Data Esperada:
 * {
 *   "set":"ARM_HOME"
 * } 
 * 
 * Topic: /TestJSON/options
 * Data Esperada:
 * { 
 *  "inputs_names":["IN1","IN2","IN3","IN4","IN5","IN6","IN7","IN8"], 
 *  "inputs_function":[0,0,0,0,0,0,0,0], 
 *  "numbers":["0123456789","0123456789","0123456789","0123456789","0123456789"] 
 * }
***/
void mqttData(void* response) {
  ELClientResponse *res = (ELClientResponse *)response;

  DEBUG_PRINT(F("Received: topic="));
  String topic = res->popString();
  DEBUG_PRINTLN(topic);

  DEBUG_PRINT(F("data="));
  String data = res->popString();
  DEBUG_PRINTLN(data);

// Hassio publica:  Topic: /TestJSON/set
//                  Data: (0 , 1, 2) - DISARMED/ARMED_HOME/ARMED_AWAY
  if(topic.indexOf("/set")>0){
    Status.AlarmNextStatus = data.toInt();
    mqtt_update = true;
  }

// Hassio publica:  Topic: /TestJSON/options
//                  Data:   { 
                          //   "pin":1234
                          //   "inputs_names":["IN1","IN2","IN3","IN4","IN5","IN6","IN7","IN8"], 
                          //   "inputs_function":[3,1,3,4,0,0,0,0], 
                          //   "numbers":["0123456789","0123456789","0123456789","0123456789","0123456789"] 
                          //}
  if(topic.indexOf("/options")>0){
    const size_t capacity = JSON_ARRAY_SIZE(5) + 2*JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(4) + 290;
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
            }
#ifdef USE_EEPROM
            saveEEPROM();
#endif
            mqtt_update = true;
          }
  }
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
// Publica todas las variables en un unico JSON
void UpdateMQTT()
{
  if(mqtt_update) // Seteo este flag global en cualquier parte y actualizo en loop
  {
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
      DEBUG_PRINTLN(F("Enviando Datos..."));
      mqtt.publish(STATUS_TOPIC, DataMQTT, QoS, RETAIN);

    }
    mqtt_update = false;
  }
}

void mqttPublish(void *context){ 
  (void)context;
  mqtt_update = true; 
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
                        }
#ifdef USE_GSM          
                        id_timerAviso = 0;
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
                        EnviarAvisoSMS = true;
#endif
                        break;
      default:          break;      
    }
    Status.AlarmStatus = Status.AlarmNextStatus;
    mqttPublish((void*)0);
  }
}
#pragma endregion

#pragma region Lectura de entradas y Sensores
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
      }
      mqtt_update = true;                       // Activo el flag para publicar el cambio
    }
  }
}
#ifdef USE_RF
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
        mqtt_update = true;
      }
    }
  }
}
#endif
#pragma endregion

#pragma region Funciones GSM
#ifdef USE_GSM
// Interpreta los comandos enviados por SMS
void rx_sms(ThreadedGSM& modem, String& Number, String& Message)
{
	uint8_t i, index, num_entrada;
	int indexStart, indexEnd;
	String newPIN, newName, command, aux, SMS_out, newNumber;

	if(Message.startsWith(Options.PIN)){			      // Verifico que el SMS comience con el PIN autorizado
		indexStart = 5;                          // Desplazo hasta el primer caracter del comando
    	Message = Message.substring(indexStart);  // Recorto el pin, Message comienza ahora con el comando
    	DEBUG_PRINT("Mensaje recibido: ");
      DEBUG_PRINTLN(Message);
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
					SMS_out = "Alarm " + String(AlarmStatus[Status.AlarmStatus]);
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
						if(String(Options.reg_numbers[index]) == ""){
							newNumber.toCharArray(&Options.reg_numbers[index][0], sizeof(Options.reg_numbers[index]));	
							saveEEPROM();	// Guardo en memoria
							SMS_out = newNumber + " Guardado";
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
						SMS_out += String(index + 1) + " " + String(Options.reg_numbers[index]);
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
	DEBUG_PRINT("Respuesta: ");
	DEBUG_PRINTLN(SMS_out);
	DEBUG_PRINT("Enviada a ");
	DEBUG_PRINTLN(Number);
	SIM800.sendSMS(Number, SMS_out);
}

// Envia avisos por SMS a los numeros registrados
bool AvisoSMS()
{
  bool send_complete = false;
  static uint8_t send_index = 0;
  String Number = String(Options.reg_numbers[send_index]);
  String Message = "Alarma Disparada!, motivo: " + String(Options.inputs_names[Status.TriggerCause]);
  if(!Number.startsWith("0")){ 
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
void startup(ThreadedGSM& modem)
{
	DEBUG_PRINTLN("SIM800 Ready");
  Status.GsmStatus = true;
}

// Callback para reiniciar el SIM800
void power(ThreadedGSM& modem, bool mode){
  digitalWrite(SIM_RES,mode);
  DEBUG_PRINT("SIM800 Power ");
  DEBUG_PRINTLN((mode)?"ON":"OFF");
  Status.GsmStatus = false;  
}

// Ver https://m2msupport.net/m2msupport/atcsq-signal-quality/
void signal(ThreadedGSM& modem, ThreadedGSM::SignalLevel& Signal){
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
#ifdef USE_RF    
    if(i<RF_INPUTS){
      pinMode(RF_INPUT[i], INPUT_PULLUP);
    }    
#endif    
  }

  SerialDebug.begin(115200);

#ifdef USE_GSM
  SerialModem.begin(ModemBaudRate);

  SIM800.begin();

	//SIM800.setInterval(ThreadedGSM::INTERVAL_CLOCK, 60000);
	SIM800.setInterval(ThreadedGSM::INTERVAL_SIGNAL, treintaMinutos);
	SIM800.setInterval(ThreadedGSM::INTERVAL_INBOX, 2*unSegundo);
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
  
#ifdef USE_EEPROM
  readEEPROM();                                 // Leo la EEPROM y cargo la estructura Options
  if(Options.data_set != 'T') defaultEEPROM();  // Si es la primera vez que se inicia y la memoria está en blanco, cargo datos por default
#endif

  mqtt_update = true;
  
  
#ifdef USE_RANDOM_SENSORS  
  timer = millis();
  randomSeed(analogRead(0));
#endif

  t_mqtt.every(diezMinutos, mqttPublish, (void*)0); // Publica todo cada 10 minutos
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
    mqtt_update = true;
  }
#endif    

  if(EnviarAvisoSMS) EnviarAvisoSMS = !AvisoSMS();

  UpdateMQTT();       // Publica los datos MQTT de ser necesario


}



