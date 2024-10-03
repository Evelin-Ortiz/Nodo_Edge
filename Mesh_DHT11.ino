#include <Adafruit_Sensor.h>
#include "painlessMesh.h"
#include <Arduino_JSON.h>
#include "DHT.h"
#include <WiFi.h>
#include "time.h"

// MESH Details
#define MESH_PREFIX     "MeshEveOscMar" // name for your MESH
#define MESH_PASSWORD   "123456789"     // password for your MESH
#define MESH_PORT       5555            // default port

#define DHTPIN 27       // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11   // DHT 11 sensor type

DHT dht(DHTPIN, DHTTYPE);

// NTP server details
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;    // GMT-5 for Bogotá
const int daylightOffset_sec = 0;     // No daylight saving time

int nodeNumber = 1;  // Node identifier
String readings;
Scheduler userScheduler;
painlessMesh mesh;

// Declaración de la función sendMessage
void sendMessage();

Task taskSendMessage(TASK_SECOND * 5, TASK_FOREVER, &sendMessage);

bool monitoring = false;
unsigned long lastToggleTime = 0;

// Function to configure NTP and get the time
void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// Function to get local time in AM/PM format
String getFormattedTime() {
  struct tm timeinfo;

  // Retry obtaining time up to 10 times
  int retries = 10;
  while (!getLocalTime(&timeinfo) && retries > 0) {
    Serial.println("Fallo al obtener la hora. Reintentando...");
    retries--;
  }

  if (retries == 0) {
    return "Error obteniendo la hora";
  }

  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M:%S %p", &timeinfo);
  return String(timeStringBuff);
}

String getReadings() {
  JSONVar jsonReadings;
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  String currentTime = getFormattedTime();

  Serial.print(F("Humidity: "));
  Serial.print(humidity, 1);
  Serial.print(F("%  Temperature: "));
  Serial.print(temperature, 1);
  Serial.println(F("°C "));
  Serial.print(F("Hora: "));
  Serial.println(currentTime);

  jsonReadings["node"] = nodeNumber;
  jsonReadings["temp"] = round(temperature * 100.0) / 100.0f;
  jsonReadings["hum"] = round(humidity * 100.0) / 100.0f;
  jsonReadings["time"] = currentTime;
  jsonReadings["monitoring"] = monitoring ? "Encendido" : "Apagado";

  readings = JSON.stringify(jsonReadings);
  return readings;
}

void sendMessage() {
  if (monitoring) {
    String msg = getReadings();
    Serial.println(msg);
    mesh.sendBroadcast(msg);
  }
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mensaje recibido desde %u msg=%s\n", from, msg.c_str());
  JSONVar myObject = JSON.parse(msg.c_str());

  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Error al parsear JSON");
    return;
  }

  int node = myObject["node"];
  String color = (const char*)myObject["color"];
  String timeReceived = (const char*)myObject["time"];

  Serial.print("Node: ");
  Serial.println(node);
  Serial.print("Led color: ");
  Serial.println(color);
  Serial.print("Hora local: ");
  Serial.println(timeReceived);
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Reiniciar WiFi antes de conectarse
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA); // Modo estación
  
  Serial.println("Conectando a WiFi...");

  // Intentar conexión WiFi con timeout
  WiFi.begin("TP-LINK_DE16", "67743667");
  unsigned long startTime = millis();
  unsigned long timeout = 10000; // Tiempo máximo de espera: 10 segundos

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    if (millis() - startTime > timeout) {
      Serial.println("\nNo se pudo conectar a WiFi. Reiniciando...");
      ESP.restart(); // Reiniciar si no se conecta en el tiempo límite
    }
  }

  Serial.println("\nConectado a WiFi");

  // Configurar NTP para sincronizar la hora
  setupNTP();

  // Mostrar la hora actual
  String currentTime = getFormattedTime();
  Serial.print("Hora actual: ");
  Serial.println(currentTime);

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();
}

void loop() {
  mesh.update();

  unsigned long currentMillis = millis();
  if (currentMillis - lastToggleTime >= 20000) {
    monitoring = !monitoring;
    lastToggleTime = currentMillis;
    Serial.print("Monitoreo: ");
    Serial.println(monitoring ? "Encendido" : "Apagado");
  }
}