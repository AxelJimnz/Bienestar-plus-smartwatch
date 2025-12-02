#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include "MAX30105.h"
#include "heartRate.h"

// --- PANTALLA ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- MAX30102 (PULSO) ---
MAX30105 particleSensor;
const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg;

Adafruit_BMP280 bmp; 
bool bmpStatus = false;

long lastScreenUpdate = 0;
const int SCREEN_INTERVAL = 250; 

void setup() {
  Serial.begin(115200);
  while(!Serial);
  Serial.println("\n--- INICIANDO SISTEMA ---");

  // 1. PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("ERROR CRITICO: Pantalla OLED no encontrada"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Conectando...");
  display.display();

  if (bmp.begin(0x76)) {
    Serial.println("EXITO: BMP280 encontrado en 0x76");
    bmpStatus = true;
  } else if (bmp.begin(0x77)) {
    Serial.println("EXITO: BMP280 encontrado en 0x77");
    bmpStatus = true;
  } else {
    Serial.println("FALLO: No se encuentra BMP280 (Revisar cables)");
    bmpStatus = false;
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("FALLO: MAX30102 no encontrado");
    display.setCursor(0,10);
    display.print("Error MAX30102");
    display.display();
    while (1);
  }
  Serial.println("EXITO: MAX30102 Iniciado");
  
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeGreen(0); 
  
  display.clearDisplay();
}

void loop() {
  // Leemos valor IR
  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; 
      rateSpot %= RATE_SIZE; 
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  if (millis() - lastScreenUpdate > SCREEN_INTERVAL) {
    
    float temp = 0;
    if (bmpStatus) {
      temp = bmp.readTemperature();
    }
    
    display.clearDisplay();

    // TEMPERATURA
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (bmpStatus) {
      display.print("Temp: ");
      display.print(temp, 1); 
      display.print(" C");
    } else {
      display.print("Temp: Error");
    }
    display.drawLine(0, 10, 128, 10, WHITE); 

    // PULSO
    if (irValue < 50000) {
      display.setCursor(30, 30);
      display.print("NO DEDO");
      display.setCursor(0, 55);
      // Mostramos el valor crudo para depurar
      display.print("Raw: "); display.print(irValue);
      beatAvg = 0; 
    } else {
      display.setCursor(0, 20);
      display.print("Detectando...");
      
      display.setTextSize(3); 
      display.setCursor(35, 35);
      if(beatAvg > 40) { // Solo mostrar si es un ritmo lógico
        display.print(beatAvg);
      } else {
        display.print("--"); 
      }
      display.setTextSize(1);
      display.print(" BPM");
    }

    display.display();
    lastScreenUpdate = millis();
  }
}