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

// Habilitar funciones
#define USE_GSM             // Modulo GSM SIM800L
#define USE_SENSOR_DHT22    // Habilita/Deshabilita DHT22
#define USE_SENSOR_18B20    // Habilita/Deshabilita 18B20 
#define USE_SENSOR_LCR      // Habilita/Deshabilita LCR
#define USE_SENSOR_POWER    // Habilita/Deshabilita medidor de potencia
#define USE_RF              // Habilita/Deshabilita entradas RF

// MQTT
bool connected;       // Flag que indica conexión MQTT OK
#define RETAIN  true  // Flag retain para publicaciones mqtt
#define QoS     0     // QoS de payload MQTT