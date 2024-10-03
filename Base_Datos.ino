#include <WiFi.h>
#include "time.h"
#include "painlessMesh.h"
#include <Arduino_JSON.h>

// MESH Details
#define   MESH_PREFIX     "MeshEveOscMar"
#define   MESH_PASSWORD   "123456789"
#define   MESH_PORT       5555

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "NSfA6YaRDRAkPlQIXxGTOGRtMXLwT2fsfyvoMR4H7vs04ko_SYB1qA60v20ldgdMEHQhXcDGNN8HdIlJBcPjCA=="
#define INFLUXDB_ORG "10d89bbe19cbee1c"
#define INFLUXDB_BUCKET "Base"

// Time zone info
#define TZ_INFO "COT5"
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

Scheduler userScheduler;
painlessMesh mesh;

// Variables para almacenar datos recibidos
double temperature = 0;
double humidity = 0;
int nodeNumber = 2;
int color[3] = {0, 0, 0};
String localTime = "";

// NTP Server y zona horaria
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;  // GMT-5 (Zona horaria de Bogotá)
const int   daylightOffset_sec = 0;  // Sin ajuste por horario de verano

Point sensor("environment");

// Prototipos de funciones
void sendInfoToNode1();
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void conectionWifi();
void sendToInfluxDB(float temp, float hum);
void controllerWifiInfluxdb(double temp, double hum);
void setupNTP();
String getLocalTime();

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
  strftime(timeStringBuff, sizeof(timeStringBuff),  "%Y-%m-%d %H:%M:%S", &timeinfo); // Formato AM/PM
  return String(timeStringBuff);
}

// Conexion a Wifi
void conectionWifi() {
  // Conectar a WiFi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("TP-LINK_DE16", "67743667");
  Serial.print("Conectando a WiFi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");
  
  // Sincronización de tiempo para InfluxDB
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Verificar conexión a InfluxDB
  if (client.validateConnection()) {
    Serial.print("Conectado a InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("Fallo la conexión a InfluxDB: ");
    Serial.println(client.getLastErrorMessage());
  }
  // Prueba adicional de conexión a base de datos
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conexión a Internet establecida.");
    if (client.validateConnection()) {
      Serial.println("Conexión a InfluxDB exitosa.");
    } else {
      Serial.println("Error en la conexión a InfluxDB: " + client.getLastErrorMessage());
    }
  } else {
    Serial.println("No hay conexión a Internet.");
  }
}

// Función para enviar datos a InfluxDB
void sendToInfluxDB(float temp, float hum) {
  // Limpiar el punto anterior
  sensor.clearFields();
  // Add tags
  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());
  // Agregar etiquetas
  sensor.addTag("Nodo", String(nodeNumber));
  // Agregar campos de datos
  sensor.addField("Temperatura", temperature);  // Asegúrate de enviar solo números
  sensor.addField("Humedad", humidity);
  // Enviar los datos a InfluxDB
  if (!client.writePoint(sensor)) {
    Serial.print("Error al escribir en InfluxDB: ");
    Serial.println(client.getLastErrorMessage());
  } else {
    Serial.println("Datos enviados a InfluxDB con éxito");
  }
}

// Función para reconectar a WiFi y a InfluxDB
void controllerWifiInfluxdb(double temp, double hum) {
  // Reconectar a WiFi
  conectionWifi();
  // Enviar datos a InfluxDB
  float temperature = round(temp * 100.0) / 100.0f;
  float humidity = round(hum * 100.0) / 100.0f;
  sendToInfluxDB(temperature, humidity);
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
  int node = myObject["node"];
  temperature = (double)myObject["temp"];
  humidity = (double)myObject["hum"];
  localTime = (const char*)myObject["time"];  // Extraer la hora recibida
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

  mesh.stop();
  controllerWifiInfluxdb(temperature, humidity);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.update();
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
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("TP-LINK_DE16", "67743667");
  Serial.print("Conectando a WiFi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");
  setupNTP();  // Configurar NTP
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");  // Sincronización de tiempo para InfluxDB
  if (client.validateConnection()) {
    Serial.print("Conectado a InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("Fallo la conexión a InfluxDB: ");
    Serial.println(client.getLastErrorMessage());
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conexión a Internet establecida.");
    if (client.validateConnection()) {
      Serial.println("Conexión a InfluxDB exitosa.");
    } else {
      Serial.println("Error en la conexión a InfluxDB: " + client.getLastErrorMessage());
    }
  } else {
    Serial.println("No hay conexión a Internet.");
  }
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
