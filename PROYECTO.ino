
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <WebServer.h>
#include "time.h"

// --- 1. CREDENCIALES WI-FI ---
const char* ssid     = "RIUAEMFI"; 
const char* password = "";

// --- 2. CONFIGURACIÓN DE ALERTAS ---
const int PIN_BUZZER = 18;
const float UMBRAL_TEMP_MAX = 30.0;
const int UMBRAL_BPM_MAX = 110;
const int UMBRAL_BPM_MIN = 50;

// --- 3. NOTAS MUSICALES (FRECUENCIAS) ---
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define REST      0

// --- LA PARTITURA (MARCHA IMPERIAL CORTA) ---
// Notas de la melodía
int melody[] = {
  NOTE_A4, NOTE_A4, NOTE_A4, NOTE_F5, NOTE_C5, NOTE_A4, NOTE_F5, NOTE_C5, NOTE_A4, REST
};

// Duración de cada nota (en milisegundos)
int noteDurations[] = {
  500, 500, 500, 350, 150, 500, 350, 150, 650, 1000
};

// Variables para controlar la música sin delay
int currentNote = 0;
long lastNoteTime = 0;
bool isPlaying = false;
int melodyLength = sizeof(melody) / sizeof(melody[0]);

// --- 4. HARDWARE Y WEB ---
WebServer server(80);
const long  gmtOffset_sec = -21600; 
const int   daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MAX30105 particleSensor;
Adafruit_BMP280 bmp; 

// --- 5. VARIABLES GLOBALES ---
bool bmpStatus = false;
bool alarmTriggered = false;

// Variables Pulso
const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;

// Timers generales
long lastScreenUpdate = 0;

// ============================================================================
// HTML WEB
// ============================================================================
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
  <div class="card"><div class="label">Temperatura</div><div id="temp" class="data-val">--</div><div class="unit">&deg;C</div></div>
  <div class="card"><div class="label">Ritmo Card&iacute;aco</div><div id="bpm" class="data-val">--</div><div class="unit">BPM</div></div>
  <div id="warning" class="alert-box">&#9888; &iexcl;ALERTA CR&Iacute;TICA!</div>
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
        alertBox.style.display = "block"; document.body.style.backgroundColor = "#3e1818";
      } else {
        alertBox.style.display = "none"; document.body.style.backgroundColor = "#121212";
      }
    }
  };
  xhttp.open("GET", "/data", true); xhttp.send();
}
</script></body></html>
)rawliteral";

// ============================================================================
// FUNCIONES SERVIDOR Y SETUP
// ============================================================================

void handleRoot() { server.send(200, "text/html", index_html); }

void handleData() {
  float t = 0; if(bmpStatus) t = bmp.readTemperature();
  String json = "{";
  json += "\"temp\":\"" + String(t, 1) + "\",";
  if(beatAvg > 0) json += "\"bpm\":\"" + String(beatAvg) + "\","; else json += "\"bpm\":\"--\",";
  json += "\"alert\":" + String(alarmTriggered ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  
  // CONFIGURACIÓN AUDIO ESP32
  // Usamos ledc para generar tonos PWM en el ESP32
  ledcAttach(PIN_BUZZER, 2000, 8); // Pin, Frecuencia base, Resolución

  // PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println(F("Error OLED")); for(;;); }
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(0, 10); display.println("Conectando WiFi..."); display.display();

  // WIFI
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) { delay(500); Serial.print("."); retry++; }
  Serial.println("");

  // IP EN MONITOR SERIAL
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("=== WIFI CONECTADO ===");
    Serial.print("IP: http://"); Serial.println(WiFi.localIP());
    
    display.clearDisplay(); display.setCursor(0,0);
    display.println("WiFi OK!"); display.println("Ver IP en Serial"); display.display();
    delay(2000);
  } else {
    display.println("Error WiFi"); display.display();
  }

  // SENSORES
  if (bmp.begin(0x76)) bmpStatus = true; else if (bmp.begin(0x77)) bmpStatus = true;
  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
  }
  
  server.on("/", handleRoot); server.on("/data", handleData); server.begin();
}

// Funciones Hora/Fecha
void printTime() {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){ char s[6]; strftime(s,6,"%H:%M",&timeinfo); display.print(s); }
  else display.print("--:--");
}
void printDate() {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){ char s[12]; strftime(s,12,"%d/%m/%Y",&timeinfo); display.print(s); }
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  server.handleClient(); // WEB

  // 1. PULSO
  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat; lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; rateSpot %= RATE_SIZE; 
      beatAvg = 0; for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x]; beatAvg /= RATE_SIZE;
    }
  }
  if (irValue < 50000) beatAvg = 0;

  // 2. DETECCIÓN DE ALARMA
  float currentTemp = 0; if (bmpStatus) currentTemp = bmp.readTemperature();
  bool dangerTemp = (currentTemp > UMBRAL_TEMP_MAX);
  bool dangerPulse = (beatAvg > 0) && (beatAvg > UMBRAL_BPM_MAX || beatAvg < UMBRAL_BPM_MIN);

  if (dangerTemp || dangerPulse) {
    alarmTriggered = true;
  } else {
    alarmTriggered = false;
    // Si la alarma se apaga, reseteamos la música y silenciamos
    currentNote = 0;
    ledcWriteTone(PIN_BUZZER, 0); // Silencio
  }

  // 3. REPRODUCTOR DE MÚSICA (NO-BLOQUEANTE)
  if (alarmTriggered) {
    // Revisamos si ya pasó el tiempo de la nota actual para tocar la siguiente
    if (millis() - lastNoteTime > noteDurations[currentNote]) {
      lastNoteTime = millis(); // Reseteamos cronómetro de nota
      
      // Tocar la nota actual
      ledcWriteTone(PIN_BUZZER, melody[currentNote]);
      
      // Preparar la siguiente nota
      currentNote++;
      if (currentNote >= melodyLength) {
        currentNote = 0; // Si termina la canción, vuelve a empezar (Loop)
      }
    }
  }

  // 4. PANTALLA
  if (millis() - lastScreenUpdate > 250) {
    display.clearDisplay();
    // Header
    display.setTextSize(1); display.setCursor(0, 0);
    if(dangerTemp) display.print("!! ");
    if(bmpStatus) { display.print(currentTemp, 1); display.print("C"); }
    display.setCursor(95, 0); printTime();
    display.drawLine(0, 10, 128, 10, WHITE); 

    // Body
    if (irValue < 50000) {
      display.setCursor(30, 25); display.print("COLOCA DEDO");
    } else {
      display.setCursor(0, 15);
      if(dangerPulse) display.print("PELIGRO"); else display.print("Normal");
      display.setTextSize(3); display.setCursor(35, 25);
      if(beatAvg > 0) display.print(beatAvg); else display.print("--");
      display.setTextSize(1); display.setCursor(105, 45); display.print("BPM");
    }

    // Footer
    display.drawLine(0, 52, 128, 52, WHITE); 
    display.setCursor(35, 56);
    if (alarmTriggered) {
       display.print(" SONANDO ALARMA "); // Aviso visual de música
    } else printDate();

    display.display();
    lastScreenUpdate = millis();
  }
}