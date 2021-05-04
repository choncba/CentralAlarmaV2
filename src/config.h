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

// Serial
#define SerialDebug     Serial
#define SerialModem     Serial1        // Modulo SIM800 conectado al Serial1 por hardware
#define ModemBaudRate   115200

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(x)      SerialDebug.print(x)
#define DEBUG_PRINTLN(x)    SerialDebug.println(x)
#define DEBUG_PRINTF(x)     SerialDebug.printf(x)   //https://github.com/MCUdude/MightyCore#printf-support
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x)     
#endif

// #ifdef DEBUG
// #define DEBUG_PRINT(...)    {SerialDebug.print(__VA_ARGS__);}      
// #define DEBUG_PRINTLN(...)  {SerialDebug.println(__VA_ARGS__);}
// #define DEBUG_PRINTF(...)   {SerialDebug.printf(__VA_ARGS__);}   //https://github.com/MCUdude/MightyCore#printf-support
// #else
// #define DEBUG_PRINT(...){}
// #define DEBUG_PRINTLN(...){}
// #define DEBUG_PRINTF(...){}     
// #endif

// Habilitar funciones
#define USE_EEPROM          // Almacena datos de configuracion en memoria
#define USE_GSM             // Modulo GSM SIM800L
//#define USE_RANDOM_SENSORS  // Genera valores aleatorios para los sensores y los publica cada 30 seg.
#ifndef USE_RANDOM_SENSORS
#define USE_SENSOR_DHT22    // Habilita/Deshabilita DHT22
#define USE_SENSOR_18B20    // Habilita/Deshabilita 18B20 
#define USE_SENSOR_LCR      // Habilita/Deshabilita LCR
#define USE_SENSOR_POWER    // Habilita/Deshabilita medidor de potencia
#define USE_RF              // Habilita/Deshabilita entradas RF
#endif

// Constantes de Timers
const unsigned long unSegundo = 1000L;  // 1000 mSeg
const unsigned long treintaSegundos = 30 * unSegundo; // 30000 ms / 30 seg
const unsigned long unMinuto = 60 * unSegundo; // 60000 ms / 1 min
const unsigned long diezMinutos = 10 * unMinuto; // 600000 ms/10 min
const unsigned long treintaMinutos = 30 * unMinuto; // 600000 ms/10 min
const unsigned long unaHora = 60 * unMinuto;

/// *** Parametros de la alarma *** ///

#define ALARM_INPUTS    8   // Numero de entrada de sensores
const uint8_t ALARM_INPUT[ALARM_INPUTS] = { IN1, IN2, IN3, IN4, IN5, IN6, IN7, IN8 };
enum ALARM_INPUT_enum {in1 = 0, in2, in3, in4, in5, in6, in7, in8 };
#define NUM_STATUS      5   // Estados posibles de la alarma para HA
#define NUM_PHONES      5   // Numeros de telefono almacenados
// Comandos enviados/recibidos con HA
enum Alarm_Status_enum { DISARMED=0, ARMED_HOME, ARMED_AWAY, PENDING, TRIGGERED };
const char AlarmStatus[NUM_STATUS][11] = { "disarmed", "armed_home", "armed_away", "pending", "triggered"};
const char AlarmCMD[NUM_STATUS][11] = { "DISARM", "ARM_HOME", "ARM_AWAY", "PENDING", "TRIGGERED"};
// Comandos recibidos por SMS
#define NUM_COMANDOS 10
const char comandos[NUM_COMANDOS][12] = {"ACTIVAR","DESACTIVAR","INFO","SAVE","DELETE","LIST","PIN","ENTRADA","FACTORY", "INPUTFUN"};
enum comandos_enum {ACTIVAR=0,DESACTIVAR,INFO,SAVE,DELETE,LIST,PIN,ENTRADA,FACTORY,INPUTFUN};
// Modos de seteo de las entradas
#define INPUT_MODES 5
enum AlarmInputMode {DESACTIVADA=0, DISP_TOT_PARCIAL, DISP_TOTAL, DISP_SIEMPRE, DISP_DEMORADO};

#define RF_INPUTS       4   // Numero de entradas RF/llavero
const uint8_t RF_INPUT[RF_INPUTS] = { RFA, RFB, RFC, RFD };
enum RF_INPUT_enum {TECLA_A = 0, TECLA_B, TECLA_C, TECLA_D };
/////////////////////////////////////////

// Parametros MQTT - La configuración de conexión al servidor se realiza desde ESP-Link
bool connected;       // Flag que indica conexión MQTT OK
#define RETAIN  true  // Flag retain para publicaciones mqtt
#define QoS     0     // QoS de payload MQTT
#define NUM_PUBLISHED_TOPIC 3
#define NUM_SUSCRIBED_TOPIC 1

// Topics Base
#define MQTT_CLIENT_ID "Central"                        // Nombre del nodo
#define BASE_TOPIC "/" MQTT_CLIENT_ID                   // Topic de base
// Topics funciones de alarma
#define ALARM_TOPIC BASE_TOPIC "/Alarm"                 // Topic para funciones de alarma
#define ALARM_STATUS_TOPIC ALARM_TOPIC "/Status"        // Topic donde se publica el estado de la alarma
#define ALARM_INPUTS_TOPIC ALARM_TOPIC "/Inputs"        // Topic donde se publican las entradas de la alarma y llavero RF 
#define ALARM_OPTIONS_TOPIC ALARM_TOPIC "/Options"      // Topic donde se publican las opciones de la alarma
#define ALARM_SET_TOPIC ALARM_TOPIC "/Set"              // Topic donde se suscribe para recibir comandos de la alarma
#define ALARM_SET_OPTIONS_TOPIC ALARM_TOPIC "/SetOptions" // Topic donde se suscribe para recibir opciones de alarma
// Topics sensores
#define SENSORS_TOPIC BASE_TOPIC "/Sensors"             // Topic donde se publican los sensores
// Topics funciones SMS
#define SMS_TOPIC BASE_TOPIC "/SMS"                     // Topic para funciones SMS
#define SMS_SEND_TOPIC SMS_TOPIC "/send"
#define SMS_RECEIVE_TOPIC SMS_TOPIC "/received"
// Topic de disponibilidad
#define MQTT_AVAILABILITY_TOPIC BASE_TOPIC "/LWT"       // Topic para publicar LWT
#define MQTT_CONNECTED_STATUS "online"                  // Estados LWT
#define MQTT_DISCONNECTED_STATUS "offline"              // Estados LWT
// Topic de disponibilidad de Home Assistant
#define MQTT_HA_AVAILAVILITY "homeassistant/status"


