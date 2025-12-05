/**
 * ============================================================================
 * PROYECTO: MONITOR DE SIGNOS VITALES IOT (IP EN SERIAL)
 * ============================================================================
 * FUNCIONES:
 * 1. IP se muestra en el MONITOR SERIAL (No en pantalla).
 * 2. Webserver con datos en tiempo real.
 * 3. Pantalla OLED con interfaz gráfica completa.
 * 4. Alertas sonoras y visuales.
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <WebServer.h>
#include "time.h"

const char* ssid     = "RIUAEMFI"; 
const char* password = "";

const int PIN_BUZZER = 18;
const float UMBRAL_TEMP_MAX = 30.0;
const int UMBRAL_BPM_MAX = 110;
const int UMBRAL_BPM_MIN = 50;

WebServer server(80);
const long  gmtOffset_sec = -21600; // México Centro (-6h)
const int   daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MAX30105 particleSensor;
Adafruit_BMP280 bmp; 

bool bmpStatus = false;
bool alarmTriggered = false;
bool buzzerState = false;

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;

// Timers
long lastScreenUpdate = 0;
long lastBuzzerTone = 0;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Monitor Signos Vitales</title>
  <style>
    body { font-family: 'Arial', sans-serif; background-color: #121212; color: #ffffff; text-align: center; margin: 0; }
    h1 { color: #03dac6; margin-top: 20px; }
    .card { background-color: #1e1e1e; max-width: 400px; margin: 20px auto; padding: 20px; border-radius: 15px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
    .data-val { font-size: 3rem; font-weight: bold; color: #ffffff; }
    .unit { font-size: 1.2rem; color: #bbbbbb; }
    .label { font-size: 1.2rem; color: #03dac6; text-transform: uppercase; letter-spacing: 2px; }
    .alert-box { display: none; background-color: #cf6679; color: black; padding: 15px; margin-top: 20px; border-radius: 10px; font-weight: bold; animation: blink 1s infinite; }
    @keyframes blink { 0% {opacity: 1;} 50% {opacity: 0.5;} 100% {opacity: 1;} }
  </style>
</head>
<body>
  <h1>Monitor Paciente IoT</h1>
  <div class="card">
    <div class="label">Temperatura</div>
    <div id="temp" class="data-val">--</div><div class="unit">&deg;C</div>
  </div>
  <div class="card">
    <div class="label">Ritmo Card&iacute;aco</div>
    <div id="bpm" class="data-val">--</div><div class="unit">BPM</div>
  </div>
  <div id="warning" class="alert-box">&#9888; &iexcl;ALERTA DETECTADA!</div>
<script>
setInterval(function() { getData(); }, 1000);
function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var data = JSON.parse(this.responseText);
      document.getElementById("temp").innerHTML = data.temp;
      document.getElementById("bpm").innerHTML = data.bpm;
      var alertBox = document.getElementById("warning");
      if(data.alert == true) {
        alertBox.style.display = "block";
        document.body.style.backgroundColor = "#3e1818";
      } else {
        alertBox.style.display = "none";
        document.body.style.backgroundColor = "#121212";
      }
    }
  };
  xhttp.open("GET", "/data", true);
  xhttp.send();
}
</script></body></html>
)rawliteral";

// SERVIDOR
void handleRoot() { server.send(200, "text/html", index_html); }

void handleData() {
  float t = 0;
  if(bmpStatus) t = bmp.readTemperature();
  String json = "{";
  json += "\"temp\":\"" + String(t, 1) + "\",";
  if(beatAvg > 0) json += "\"bpm\":\"" + String(beatAvg) + "\",";
  else json += "\"bpm\":\"--\",";
  json += "\"alert\":" + String(alarmTriggered ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200); // IMPORTANTE: Velocidad del monitor serial
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // 1. INICIAR PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error OLED")); for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Conectando WiFi...");
  display.display();

  // 2. CONECTAR WIFI
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500); 
    Serial.print("."); 
    retry++;
  }
  Serial.println(""); // Salto de línea

  // ---------------------------------------------------------
  // MOSTRAR IP EN MONITOR SERIAL
  // ---------------------------------------------------------
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Imprimir en Serial (Consola)
    Serial.println("==================================");
    Serial.println("   CONEXION WIFI ESTABLECIDA");
    Serial.println("==================================");
    Serial.print(" DIRECCION IP:  http://");
    Serial.println(WiFi.localIP());
    Serial.println("==================================");
    
    // Feedback visual mínimo en pantalla
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi: CONECTADO");
    display.println("Revisa Monitor Serial");
    display.println("para ver la IP.");
    display.display();
    delay(2000); 
    
  } else {
    Serial.println("ERROR: No se pudo conectar al WiFi.");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Error WiFi");
    display.display();
    delay(2000);
  }

  // 3. INICIAR SENSORES
  if (bmp.begin(0x76)) bmpStatus = true;
  else if (bmp.begin(0x77)) bmpStatus = true;

  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
  }

  // 4. INICIAR WEB SERVER
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Servidor Web Iniciado.");
}

// Funciones auxiliares
void printTime() {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    char timeString[6];
    strftime(timeString, 6, "%H:%M", &timeinfo);
    display.print(timeString);
  } else display.print("--:--");
}
void printDate() {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    char dateString[12];
    strftime(dateString, 12, "%d/%m/%Y", &timeinfo);
    display.print(dateString);
  }
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  server.handleClient(); // Atender Web

  // Lectura Pulso
  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; 
      rateSpot %= RATE_SIZE; 
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
  if (irValue < 50000) beatAvg = 0;

  // Alarmas
  float currentTemp = 0;
  if (bmpStatus) currentTemp = bmp.readTemperature();
  bool dangerTemp = (currentTemp > UMBRAL_TEMP_MAX);
  bool dangerPulse = (beatAvg > 0) && (beatAvg > UMBRAL_BPM_MAX || beatAvg < UMBRAL_BPM_MIN);

  if (dangerTemp || dangerPulse) {
    alarmTriggered = true;
    if (millis() - lastBuzzerTone > 200) { 
      lastBuzzerTone = millis();
      buzzerState = !buzzerState;
      digitalWrite(PIN_BUZZER, buzzerState);
    }
  } else {
    alarmTriggered = false;
    digitalWrite(PIN_BUZZER, LOW);
  }

  // Actualizar Pantalla (Cada 250ms)
  if (millis() - lastScreenUpdate > 250) {
    display.clearDisplay();
    
    // Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    if(dangerTemp) display.print("!! ");
    if(bmpStatus) { display.print(currentTemp, 1); display.print("C"); }
    display.setCursor(95, 0); printTime();
    display.drawLine(0, 10, 128, 10, WHITE); 

    // Body
    if (irValue < 50000) {
      display.setCursor(30, 25); display.print("COLOCA DEDO");
    } else {
      display.setCursor(0, 15);
      if(dangerPulse) display.print("ANOMALIA"); else display.print("Normal");
      display.setTextSize(3); display.setCursor(35, 25);
      if(beatAvg > 0) display.print(beatAvg); else display.print("--");
      display.setTextSize(1); display.setCursor(105, 45); display.print("BPM");
    }

    // Footer
    display.drawLine(0, 52, 128, 52, WHITE); 
    display.setCursor(35, 56);
    if (alarmTriggered) {
      if (buzzerState) {
        display.setTextColor(BLACK, WHITE); display.print("PELIGRO"); display.setTextColor(WHITE);
      } else display.print(" PELIGRO ");
    } else printDate();

    display.display();
    lastScreenUpdate = millis();
  }
}