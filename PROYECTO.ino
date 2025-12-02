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

// --- 2. ZONA HORARIA (México Centro / CST) ---
const long  gmtOffset_sec = -21600; 
const int   daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

// --- PANTALLA ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- SENSORES ---
MAX30105 particleSensor;
Adafruit_BMP280 bmp; 
bool bmpStatus = false;

// Variables Pulso
const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg;

// Variables Timer
long lastScreenUpdate = 0;
const int SCREEN_INTERVAL = 250; 

// --- FUNCIONES DE TIEMPO ---
void printTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.print("--:--");
    return;
  }
  // Hora:Minuto
  char timeString[6];
  strftime(timeString, 6, "%H:%M", &timeinfo);
  display.print(timeString);
}

void printDate() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.print("Buscando fecha...");
    return;
  }
  // Día/Mes/Año
  char dateString[12];
  strftime(dateString, 12, "%d/%m/%Y", &timeinfo);
  display.print(dateString);
}

void setup() {
  Serial.begin(115200);

  // 1. PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error OLED")); for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Conectando WiFi...");
  display.display();

  // 2. WIFI
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    display.println("Conectado!");
  } else {
    display.println("Modo Offline");
  }
  display.display();
  delay(1000);

  // 3. SENSORES
  if (bmp.begin(0x76)) bmpStatus = true;
  else if (bmp.begin(0x77)) bmpStatus = true;

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    // Si falla el pulso, avisamos pero continuamos
    Serial.println("Error MAX30102");
  } else {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
  }
}

void loop() {
  // --- LECTURA RÁPIDA (Pulso) ---
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

  // --- ACTUALIZAR PANTALLA (Lento) ---
  if (millis() - lastScreenUpdate > SCREEN_INTERVAL) {
    display.clearDisplay();

    // --------------------------
    // ZONA 1: CABECERA (Top)
    // --------------------------
    display.setTextSize(1);
    
    // Temperatura (Izq)
    display.setCursor(0, 0);
    if (bmpStatus) {
      display.print(bmp.readTemperature(), 1); 
      display.cp437(true); display.write(167); display.print("C");
    } else {
      display.print("-- C");
    }

    // Hora (Der)
    display.setCursor(95, 0); 
    if(WiFi.status() == WL_CONNECTED) printTime();
    else display.print("--:--");

    // Línea separadora superior
    display.drawLine(0, 10, 128, 10, WHITE); 

    // --------------------------
    // ZONA 2: CUERPO (Pulso)
    // --------------------------
    if (irValue < 50000) {
      // Estado: Esperando dedo
      display.setCursor(25, 25);
      display.setTextSize(1);
      display.println("COLOCA DEDO");
      beatAvg = 0;
    } else {
      // Estado: Leyendo
      display.setTextSize(1);
      display.setCursor(0, 15);
      display.print("BPM:");

      display.setTextSize(3); // Fuente grande
      display.setCursor(35, 25);
      if(beatAvg > 40) {
        display.print(beatAvg);
      } else {
        display.print("--"); 
      }
      
      // Animación simple (latido)
      display.setTextSize(1);
      display.setCursor(110, 25);
      display.print("<3");
    }

    // --------------------------
    // ZONA 3: PIE DE PÁGINA (Fecha)
    // --------------------------
    // Línea separadora inferior
    display.drawLine(0, 52, 128, 52, WHITE); 
    
    display.setTextSize(1);
    display.setCursor(35, 56); // Centrado aprox
    if(WiFi.status() == WL_CONNECTED) {
      printDate();
    } else {
      display.print("Sin Fecha");
    }

    display.display();
    lastScreenUpdate = millis();
  }
}