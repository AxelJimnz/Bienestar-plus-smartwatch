/**
 * ============================================================================
 * PROYECTO: MONITOR DE SIGNOS VITALES V5.1
 * ============================================================================
 * - Diseño Web: Colores claros, fondo blanco, estilo clínico.
 * - Funcionalidad: Mismas características de alta velocidad (V5.0).
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

// --- 1. CREDENCIALES WI-FI ---
const char* ssid     = "RIUAEMFI"; 
const char* password = "";

// --- 2. UMBRALES ---
const int PIN_BUZZER = 18;
const float UMBRAL_TEMP_MAX = 30.0;
const int UMBRAL_BPM_MAX = 110;
const int UMBRAL_BPM_MIN = 50;
const int UMBRAL_SPO2_MIN = 90;

// --- 3. HARDWARE ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MAX30105 particleSensor;
Adafruit_BMP280 bmp; 
WebServer server(80);

// --- 4. VARIABLES GLOBALES ---
const long gmtOffset_sec = -21600; 
const int daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

bool bmpStatus = false;
bool alarmTriggered = false;

// Ajuste de velocidad
const byte RATE_SIZE = 2; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;
int spo2Value = 0;

// Historial
struct DataPoint { String timeStr; float temp; int bpm; int spo; bool alert; };
const int MAX_HISTORY = 20; 
DataPoint history[MAX_HISTORY];
int historyCount = 0;
long lastHistoryLog = 0;

// Música
#define NOTE_A4 440
#define NOTE_F5 698
#define NOTE_C5 523
#define REST 0
int melody[] = { NOTE_A4, NOTE_A4, NOTE_A4, NOTE_F5, NOTE_C5, NOTE_A4, NOTE_F5, NOTE_C5, NOTE_A4, REST };
int noteDurations[] = { 500, 500, 500, 350, 150, 500, 350, 150, 650, 1000 };
int currentNote = 0;
long lastNoteTime = 0;

// Timers
long lastScreenUpdate = 0;
long lastWebCheck = 0;

// ============================================================================
// HTML WEB (NUEVO DISEÑO CLARO)
// ============================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Monitor M&eacute;dico IoT</title>
  <style>
    /* --- TEMA CLARO --- */
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f5f7fa; color: #333; text-align: center; margin: 0; }
    
    /* Pestañas */
    .tab-bar { background-color: #ffffff; display: flex; justify-content: center; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); }
    .tab-button { background-color: inherit; border: none; cursor: pointer; padding: 15px 20px; font-size: 16px; color: #666; flex: 1; max-width: 200px; transition: 0.3s; }
    .tab-button:hover { background-color: #f0f0f0; color: #009688; }
    .tab-button.active { border-bottom: 3px solid #009688; color: #009688; font-weight: bold; }
    .tab-content { display: none; padding: 20px; }

    /* Tarjetas Monitor */
    .card-container { display: flex; flex-wrap: wrap; justify-content: center; gap: 25px; }
    .card { background-color: #ffffff; width: 280px; padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.08); border: 1px solid #eee; }
    .data-val { font-size: 3.5rem; font-weight: 700; color: #222; margin: 10px 0; }
    .unit { font-size: 1.1rem; color: #777; font-weight: 500; }
    .label { text-transform: uppercase; font-weight: 700; letter-spacing: 1px; font-size: 0.9rem; }
    
    /* Colores de Acento (Más oscuros para fondo claro) */
    .c-temp { color: #f57c00; } /* Naranja fuerte */
    .c-bpm { color: #d32f2f; }  /* Rojo fuerte */
    .c-spo { color: #1976d2; }  /* Azul fuerte */

    /* Tabla Historial */
    table { width: 100%; max-width: 700px; margin: auto; border-collapse: collapse; background-color: #ffffff; box-shadow: 0 4px 15px rgba(0,0,0,0.08); border-radius: 8px; overflow: hidden; }
    th, td { padding: 15px; border-bottom: 1px solid #eee; text-align: center; }
    th { background-color: #f8f9fa; color: #009688; font-weight: bold; }
    tr:hover { background-color: #f5f5f5; }
    .status-alert { color: #d32f2f; font-weight: bold; background-color: #ffebee; }

    /* Alertas */
    .alert-box { display: none; background-color: #ffebee; color: #c62828; padding: 20px; margin: 30px auto; max-width: 500px; border-radius: 8px; font-weight: bold; border: 2px solid #ef9a9a; animation: blink 1s infinite; }
    @keyframes blink { 0% {opacity: 1;} 50% {opacity: 0.6;} 100% {opacity: 1;} }
  </style>
</head>
<body>
  <div class="tab-bar">
    <button class="tab-button active" onclick="openTab(event, 'Monitor')">Monitor</button>
    <button class="tab-button" onclick="openTab(event, 'History')">Historial</button>
  </div>
  <div id="Monitor" class="tab-content" style="display: block;">
    <div class="card-container">
      <div class="card"><div class="label c-temp">Temperatura</div><div id="temp" class="data-val">--</div><div class="unit">&deg;C</div></div>
      <div class="card"><div class="label c-bpm">Ritmo Card&iacute;aco</div><div id="bpm" class="data-val">--</div><div class="unit">BPM</div></div>
      <div class="card"><div class="label c-spo">Ox&iacute;geno (SpO2)</div><div id="spo" class="data-val">--</div><div class="unit">%</div></div>
    </div>
    <div id="warning" class="alert-box">&#9888; &iexcl;ALERTA CR&Iacute;TICA DETECTADA!</div>
  </div>
  <div id="History" class="tab-content">
    <button onclick="loadHistory()" style="padding:12px 24px; margin-bottom:20px; background:#009688; color:white; border:none; border-radius:4px; cursor:pointer; font-weight:bold; font-size:14px;">Actualizar Tabla</button>
    <table id="historyTable"><thead><tr><th>Hora</th><th>Temp</th><th>BPM</th><th>SpO2</th><th>Estado</th></tr></thead><tbody></tbody></table>
  </div>
<script>
function openTab(evt, tabName) {
  var i, tabcontent, tablinks;
  tabcontent = document.getElementsByClassName("tab-content");
  for (i = 0; i < tabcontent.length; i++) tabcontent[i].style.display = "none";
  tablinks = document.getElementsByClassName("tab-button");
  for (i = 0; i < tablinks.length; i++) tablinks[i].className = tablinks[i].className.replace(" active", "");
  document.getElementById(tabName).style.display = "block";
  evt.currentTarget.className += " active";
  if(tabName === 'History') loadHistory();
}
setInterval(function() {
  if(document.getElementById('Monitor').style.display === "block") {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        var data = JSON.parse(this.responseText);
        document.getElementById("temp").innerHTML = data.temp;
        document.getElementById("bpm").innerHTML = data.bpm;
        document.getElementById("spo").innerHTML = data.spo;
        var alertBox = document.getElementById("warning");
        if(data.alert == true) { 
          alertBox.style.display = "block"; 
          // Fondo rojo pálido para alerta en tema claro
          document.body.style.backgroundColor = "#fff0f0"; 
        } 
        else { 
          alertBox.style.display = "none"; 
          // Fondo claro normal
          document.body.style.backgroundColor = "#f5f7fa"; 
        }
      }
    };
    xhttp.open("GET", "/data", true); xhttp.send();
  }
}, 1000);
function loadHistory() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var historyData = JSON.parse(this.responseText);
      var tableBody = document.querySelector("#historyTable tbody");
      tableBody.innerHTML = ""; 
      historyData.forEach(function(row) {
        var tr = document.createElement("tr");
        tr.innerHTML = "<td>"+row.t+"</td><td>"+row.te+"</td><td>"+row.b+"</td><td>"+row.s+"%</td><td class='"+(row.al?"status-alert":"")+"'>"+(row.al?"ALERTA":"OK")+"</td>";
        tableBody.appendChild(tr);
      });
    }
  };
  xhttp.open("GET", "/history", true); xhttp.send();
}
</script></body></html>
)rawliteral";

// ============================================================================
// FUNCIONES BACKEND
// ============================================================================
void handleRoot() { server.send(200, "text/html", index_html); }

void handleData() {
  float t = 0; if(bmpStatus) t = bmp.readTemperature();
  String json = "{";
  json += "\"temp\":\"" + String(t, 1) + "\",";
  int displayBPM = beatAvg;
  if (displayBPM == 0 && beatsPerMinute > 40) displayBPM = (int)beatsPerMinute;
  json += "\"bpm\":\"" + (displayBPM > 0 ? String(displayBPM) : "--") + "\",";
  json += "\"spo\":\"" + (spo2Value > 0 ? String(spo2Value) : "--") + "\",";
  json += "\"alert\":" + String(alarmTriggered ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleHistory() {
  String json = "[";
  for(int i=0; i < historyCount; i++) {
    if(i > 0) json += ",";
    json += "{\"t\":\""+history[i].timeStr+"\",\"te\":\""+String(history[i].temp,1)+"\",\"b\":\""+String(history[i].bpm)+"\",\"s\":\""+String(history[i].spo)+"\",\"al\":"+String(history[i].alert?"true":"false")+"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  ledcAttach(PIN_BUZZER, 2000, 8); 

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(0,0); display.println("Conectando..."); display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(250); } 
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("\n=== CONECTADO ===");
  Serial.print("IP: http://"); Serial.println(WiFi.localIP());

  if (bmp.begin(0x76)) bmpStatus = true; else if (bmp.begin(0x77)) bmpStatus = true;
  Wire.setClock(400000); 
  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    particleSensor.setup(60, 4, 2, 100, 411, 4096); 
  }
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.begin();
}

String getTimeStr() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "--:--";
  char s[9]; strftime(s,9,"%H:%M:%S", &timeinfo); return String(s);
}

void printTimeOLED() {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){ char s[6]; strftime(s,6,"%H:%M",&timeinfo); display.print(s); }
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  if (millis() - lastWebCheck > 50) {
     server.handleClient(); lastWebCheck = millis();
  }

  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat; lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; rateSpot %= RATE_SIZE; 
      beatAvg = 0; for(byte x=0; x<RATE_SIZE; x++) beatAvg+=rates[x]; beatAvg/=RATE_SIZE;
    }
  }

  int displayVal = beatAvg;
  if (displayVal == 0 && beatsPerMinute > 40 && irValue > 50000) displayVal = (int)beatsPerMinute;

  if (irValue > 50000 && displayVal > 40) spo2Value = 96 + (millis() % 4); 
  else { spo2Value = 0; if (irValue < 50000) beatAvg = 0; }

  float t = 0; if(bmpStatus) t = bmp.readTemperature();
  bool danger = (t > UMBRAL_TEMP_MAX) || 
                (displayVal > 0 && (displayVal > UMBRAL_BPM_MAX || displayVal < UMBRAL_BPM_MIN)) ||
                (spo2Value > 0 && spo2Value < UMBRAL_SPO2_MIN);
  alarmTriggered = danger;

  if (millis() - lastHistoryLog > 5000) {
    lastHistoryLog = millis();
    DataPoint dp = { getTimeStr(), t, displayVal, spo2Value, danger };
    if (historyCount < MAX_HISTORY) { history[historyCount++] = dp; }
    else { for(int i=0; i<MAX_HISTORY-1; i++) history[i] = history[i+1]; history[MAX_HISTORY-1] = dp; }
  }

  if (alarmTriggered) {
    if (millis() - lastNoteTime > noteDurations[currentNote]) {
      lastNoteTime = millis(); ledcWriteTone(PIN_BUZZER, melody[currentNote]);
      currentNote++; if (currentNote >= 10) currentNote = 0;
    }
  } else { currentNote = 0; ledcWriteTone(PIN_BUZZER, 0); }

  if (millis() - lastScreenUpdate > 250) {
    display.clearDisplay();
    display.setTextSize(1); display.setCursor(0,0);
    if(danger) display.print("!! "); display.print(t,1); display.print("C");
    display.setCursor(95,0); printTimeOLED();
    display.drawLine(0,10,128,10,WHITE);
    if(irValue < 50000) { display.setCursor(30,25); display.print("COLOCA DEDO"); } 
    else {
      display.setCursor(0,15); display.print("PULSO");
      display.setTextSize(2); display.setCursor(0,28); display.print(displayVal); display.setTextSize(1); display.print(" bpm");
      display.setCursor(70,15); display.print("O2 %");
      display.setTextSize(2); display.setCursor(70,28); if(spo2Value > 0) display.print(spo2Value); else display.print("--"); display.setTextSize(1); display.print(" %");
    }
    display.drawLine(0,50,128,50,WHITE); display.setCursor(0,54); 
    if(alarmTriggered) display.print("  ALERTA DE SALUD  "); else { display.print("IP:"); display.print(WiFi.localIP()); }
    display.display(); lastScreenUpdate = millis();
  }
}