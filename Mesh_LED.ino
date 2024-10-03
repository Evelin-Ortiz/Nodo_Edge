#include <WiFi.h>
#include "time.h"
#include "painlessMesh.h"
#include <Arduino_JSON.h>

// MESH Details
#define   MESH_PREFIX     "MeshEveOscMar"
#define   MESH_PASSWORD   "123456789"
#define   MESH_PORT       5555

// Pines para el LED RGB
#define RED_PIN 22
#define GREEN_PIN 32
#define BLUE_PIN 33

// Canales PWM
#define RED_CHANNEL 0
#define GREEN_CHANNEL 1
#define BLUE_CHANNEL 2
#define LEDC_TIMER_13_BIT 13
#define LEDC_BASE_FREQ 5000

Scheduler userScheduler;
painlessMesh mesh;

// Variables para almacenar datos recibidos
double temperature = 0;
double humidity = 0;
int nodeNumber = 2;
int color[3] = {0, 0, 0};
String localTime = "";
bool monitoring = false; // Estado del monitoreo
bool ledEnabled = true; // Estado del LED

// Prototipos de funciones
void controlLedRGB(double temp, double hum);
void sendInfoToNode1();
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);

// NTP Server y zona horaria
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;  // Para ajustar la hora GMT-5 (dependiendo de tu zona horaria)
const int   daylightOffset_sec = 3600; // Ajuste de horario de verano

// Función para configurar NTP
void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// Función para obtener la hora actual en formato AM/PM
String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Error obteniendo la hora";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M:%S %p", &timeinfo); // Formato AM/PM
  return String(timeStringBuff);
}

// Función para configurar PWM
void setupPWM() {
  ledcSetup(RED_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcSetup(GREEN_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcSetup(BLUE_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);

  ledcAttachPin(RED_PIN, RED_CHANNEL);
  ledcAttachPin(GREEN_PIN, GREEN_CHANNEL);
  ledcAttachPin(BLUE_PIN, BLUE_CHANNEL);
}

// Función para convertir valores RGB a texto
String getColorName(int r, int g, int b) {
  if (r == 255 && g == 0 && b == 0) {
    return "rojo";
  } else if (r == 0 && g == 255 && b == 0) {
    return "verde";
  } else if (r == 0 && g == 0 && b == 255) {
    return "azul";
  } else {
    return "desconocido";
  }
}

// Función para controlar el color del LED RGB
void controlLedRGB(double temp, double hum) {
  if (ledEnabled) { // Solo cambia el color si el LED está habilitado
    if (temp < 20) {
      color[0] = 0;
      color[1] = 0;
      color[2] = 255; // Azul
    } else if (temp >= 20 && temp < 28) {
      color[0] = 0;
      color[1] = 255;
      color[2] = 0; // Verde
    } else {
      color[0] = 255;
      color[1] = 0;
      color[2] = 0; // Rojo
    }
    ledcWrite(RED_CHANNEL, color[0] * 8191 / 255);
    ledcWrite(GREEN_CHANNEL, color[1] * 8191 / 255);
    ledcWrite(BLUE_CHANNEL, color[2] * 8191 / 255);
  } else {
    ledcWrite(RED_CHANNEL, 0); // Apagar LED rojo
    ledcWrite(GREEN_CHANNEL, 0); // Apagar LED verde
    ledcWrite(BLUE_CHANNEL, 0); // Apagar LED azul
  }
}

// Función para enviar información al Nodo 1
void sendInfoToNode1() {
  JSONVar jsonResponse;
  jsonResponse["node"] = 2;
  String colorName = getColorName(color[0], color[1], color[2]);
  jsonResponse["color"] = colorName;
  jsonResponse["time"] = localTime;

  String response = JSON.stringify(jsonResponse);
  mesh.sendBroadcast(response);

  Serial.println("Enviando estado del LED RGB al Nodo 1: ");
  Serial.println(response);
}


// Callback para recibir mensajes
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mensaje recibido desde %u: %s\n", from, msg.c_str());

  JSONVar myObject = JSON.parse(msg.c_str());

  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Error al parsear JSON");
    return;
  }

  nodeNumber = (int)myObject["node"];
  temperature = (double)myObject["temp"];
  humidity = (double)myObject["hum"];
  localTime = (const char*)myObject["time"]; // Extraer la hora recibida
  String status = (const char*)myObject["monitoring"]; // Obtener el estado de monitoreo

  Serial.print("Nodo: ");
  Serial.println(nodeNumber);
  Serial.print("Temperatura: ");
  Serial.print(temperature, 1);
  Serial.println(" °C");
  Serial.print("Humedad: ");
  Serial.print(humidity, 1);
  Serial.println(" %");
  Serial.print("Hora local: ");
  Serial.println(localTime);  // Imprimir la hora local

  controlLedRGB(temperature, humidity);
  sendInfoToNode1();
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("Nueva conexión, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.println("Cambios en las conexiones");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Hora ajustada %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

void setup() {
  Serial.begin(115200);
  setupPWM();

  WiFi.begin("TP-LINK_DE16", "67743667"); // Conectar a WiFi para obtener NTP
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println(" Conectado a WiFi");

  setupNTP(); // Configurar NTP para obtener la hora local

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
}

void loop() {
  mesh.update();
}