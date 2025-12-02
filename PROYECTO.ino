#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include "time.h"

// --- 1. TUS DATOS WIFI ---
const char* ssid     = "Totalplay-BAA6_EXT";      // <--- Pon aquí tu red
const char* password = "BAA608A3cyGxZTJs";  

// --- 2. CONFIGURACIÓN DE ALERTAS ---
const int PIN_BUZZER = 18;        // Pin del Zumbador
const float UMBRAL_TEMP_MAX = 30.0; // Alerta si pasa de 30°C (Bajo para probar con el dedo)
const int UMBRAL_BPM_MAX = 110;     // Taquicardia
const int UMBRAL_BPM_MIN = 50;      // Bradicardia

// --- 3. ZONA HORARIA ---
const long  gmtOffset_sec = -21600; // México Centro (-6h)
const int   daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

// --- OBJETOS ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MAX30105 particleSensor;
Adafruit_BMP280 bmp; 

// Variables Globales
bool bmpStatus = false;
const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;

// Variables de Control de Tiempo (Timers)
long lastScreenUpdate = 0;
long lastBuzzerTone = 0;
bool buzzerState = false;
bool alarmTriggered = false; // Bandera para saber si hay peligro

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Asegurar silencio al inicio

  // PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error OLED")); for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Iniciando Sistema...");
  display.display();

  // WIFI
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500); Serial.print("."); retry++;
  }
  if (WiFi.status() == WL_CONNECTED) configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // SENSORES
  if (bmp.begin(0x76)) bmpStatus = true;
  else if (bmp.begin(0x77)) bmpStatus = true;

  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
  }
}

// Función auxiliar para imprimir hora
void printTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){ display.print("--:--"); return; }
  char timeString[6];
  strftime(timeString, 6, "%H:%M", &timeinfo);
  display.print(timeString);
}

// Función auxiliar para imprimir fecha
void printDate() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){ display.print("Iniciando..."); return; }
  char dateString[12];
  strftime(dateString, 12, "%d/%m/%Y", &timeinfo);
  display.print(dateString);
}

void loop() {
  // ------------------------------------------------
  // 1. LECTURA CRÍTICA (PULSO) - Máxima prioridad
  // ------------------------------------------------
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

  // Si no hay dedo, reseteamos promedio
  if (irValue < 50000) beatAvg = 0;

  // ------------------------------------------------
  // 2. SISTEMA DE ALARMA (Lógica)
  // ------------------------------------------------
  float currentTemp = 0;
  if (bmpStatus) currentTemp = bmp.readTemperature();

  // Comprobamos condiciones de peligro
  bool dangerTemp = (currentTemp > UMBRAL_TEMP_MAX);
  
  // Solo alarmamos por pulso si hay un dedo puesto (beatAvg > 0)
  bool dangerPulse = (beatAvg > 0) && (beatAvg > UMBRAL_BPM_MAX || beatAvg < UMBRAL_BPM_MIN);

  if (dangerTemp || dangerPulse) {
    alarmTriggered = true;
    
    // Sonido Intermitente (Beep... Beep...)
    // Usamos millis() para no detener el procesador con delay()
    if (millis() - lastBuzzerTone > 200) { // Velocidad del beep
      lastBuzzerTone = millis();
      buzzerState = !buzzerState;
      digitalWrite(PIN_BUZZER, buzzerState);
    }
  } else {
    alarmTriggered = false;
    digitalWrite(PIN_BUZZER, LOW); // Silencio
  }

  // ------------------------------------------------
  // 3. INTERFAZ GRÁFICA (Actualización lenta 250ms)
  // ------------------------------------------------
  if (millis() - lastScreenUpdate > 250) {
    display.clearDisplay();

    // --- TOP: Temp y Hora ---
    display.setTextSize(1);
    display.setCursor(0, 0);
    // Si hay fiebre, ponemos "!!" al lado de la temp
    if (dangerTemp) display.print("!! "); 
    display.print(currentTemp, 1); display.print("C");
    
    display.setCursor(95, 0); 
    if(WiFi.status() == WL_CONNECTED) printTime();
    else display.print("--:--");
    
    display.drawLine(0, 10, 128, 10, WHITE); 

    // --- CENTRO: Pulso ---
    if (irValue < 50000) {
      display.setCursor(30, 25);
      display.print("COLOCA DEDO");
    } else {
      display.setCursor(0, 15);
      // Mensaje dinámico según estado
      if (dangerPulse) display.print("PULSO ANORMAL");
      else display.print("Ritmo Normal");

      display.setTextSize(3); 
      display.setCursor(35, 25);
      if(beatAvg > 20) display.print(beatAvg);
      else display.print("--"); 
      
      display.setTextSize(1);
      display.setCursor(105, 45);
      display.print("BPM");
    }

    // --- BOTTOM: Fecha o ALERTA ---
    display.drawLine(0, 52, 128, 52, WHITE); 
    display.setCursor(35, 56);
    
    if (alarmTriggered) {
      // Si hay alarma, mostramos aviso visual parpadeante
      if (buzzerState) { // Sincronizado con el sonido
        display.setTextColor(BLACK, WHITE); // Texto invertido (fondo blanco)
        display.print("PELIGRO");
        display.setTextColor(WHITE);
      } else {
        display.print(" PELIGRO ");
      }
    } else {
      // Si todo está bien, mostramos la fecha
      if(WiFi.status() == WL_CONNECTED) printDate();
      else display.print("Monitoreando");
    }

    display.display();
    lastScreenUpdate = millis();
  }
}