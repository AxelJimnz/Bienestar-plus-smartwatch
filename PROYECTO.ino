/**
 * ============================================================================
 * PROYECTO: MONITOR DE SIGNOS VITALES IOT CON ALERTAS (ESP32)
 * ============================================================================
 * * DESCRIPCIÓN:
 * Sistema que monitorea ritmo cardíaco (MAX30102) y temperatura ambiente (BMP280).
 * Muestra los datos en pantalla OLED, sincroniza hora/fecha vía WiFi (NTP)
 * y emite alertas visuales y sonoras (Buzzer) si los valores salen de rango.
 * * HARDWARE:
 * - MCU: ESP32 Dev Module
 * - Pantalla: OLED 0.96" I2C (SSD1306) -> Pines SDA(21), SCL(22)
 * - Sensor 1: MAX30102 (Pulso/Oxígeno) -> Pines SDA(21), SCL(22)
 * - Sensor 2: BMP280 (Temp/Presión)    -> Pines SDA(21), SCL(22)
 * - Actuador: Buzzer (Zumbador)        -> Pin GPIO 18
 * * AUTOR: [Tu Nombre]
 * FECHA: Diciembre 2025
 * ============================================================================
 */

// --- 1. LIBRERÍAS NECESARIAS ---
#include <Wire.h>               // Comunicación I2C
#include <Adafruit_GFX.h>       // Gráficos básicos
#include <Adafruit_SSD1306.h>   // Controlador Pantalla OLED
#include <Adafruit_BMP280.h>    // Controlador Sensor Temperatura
#include "MAX30105.h"           // Controlador Sensor Pulso (Funciona para MAX30102)
#include "heartRate.h"          // Algoritmo de detección de latidos
#include <WiFi.h>               // Conexión Inalámbrica
#include "time.h"               // Gestión de hora mundial

// --- 2. CREDENCIALES WI-FI (¡EDITAR AQUÍ!) ---
const char* ssid     = "RIUAEMFI";  // <-- Tu red
const char* password = "";    // <-- Tu clave

// --- 3. CONFIGURACIÓN DE UMBRALES (ALERTAS) ---
const int PIN_BUZZER = 18;          // Pin positivo del Buzzer
const float UMBRAL_TEMP_MAX = 30.0; // Alarma si T > 30°C (Bajo para pruebas fáciles)
const int UMBRAL_BPM_MAX = 110;     // Alarma Taquicardia (>110)
const int UMBRAL_BPM_MIN = 50;      // Alarma Bradicardia (<50)

// --- 4. CONFIGURACIÓN HORARIA (NTP) ---
// GMT Offset: Segundos de diferencia con UTC.
// México Centro: UTC -6 horas = -6 * 3600 = -21600
const long  gmtOffset_sec = -21600; 
const int   daylightOffset_sec = 0;   // Horario de verano (0 si no aplica)
const char* ntpServer = "pool.ntp.org";

// --- 5. DEFINICIÓN DE OBJETOS ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

MAX30105 particleSensor; // Objeto del sensor de pulso
Adafruit_BMP280 bmp;     // Objeto del sensor de temperatura

// --- 6. VARIABLES GLOBALES ---

// Estado del Hardware
bool bmpStatus = false;      // True si BMP280 inicia bien
bool alarmTriggered = false; // True si hay una emergencia activa
bool buzzerState = false;    // Estado actual del sonido (On/Off)

// Variables Matemáticas para el Pulso
const byte RATE_SIZE = 4;    // Tamaño del promedio (últimos 4 latidos)
byte rates[RATE_SIZE];       // Array de almacenamiento
byte rateSpot = 0;           // Índice del array
long lastBeat = 0;           // Momento exacto del último latido detectado
float beatsPerMinute;        // Cálculo instantáneo (muy volátil)
int beatAvg = 0;             // Promedio suavizado (Valor final a mostrar)

// Temporizadores (para multitarea sin delay)
long lastScreenUpdate = 0;   // Controla refresco de pantalla
long lastBuzzerTone = 0;     // Controla intermitencia del sonido

// ============================================================================
// FUNCIONES AUXILIARES (Hora y Fecha)
// ============================================================================

// Imprime HH:MM en la pantalla
void printTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.print("--:--");
    return;
  }
  char timeString[6];
  strftime(timeString, 6, "%H:%M", &timeinfo);
  display.print(timeString);
}

// Imprime DD/MM/AAAA en la pantalla
void printDate() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.print("Sincronizando...");
    return;
  }
  char dateString[12];
  strftime(dateString, 12, "%d/%m/%Y", &timeinfo);
  display.print(dateString);
}

// ============================================================================
// SETUP: CONFIGURACIÓN INICIAL
// ============================================================================
void setup() {
  Serial.begin(115200); // Iniciar monitor serie para depuración

  // Configurar Buzzer
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Asegurar silencio

  // A) INICIAR PANTALLA OLED
  // Dirección I2C suele ser 0x3C
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("ERROR CRITICO: Pantalla OLED no encontrada"));
    for(;;); // Detener ejecución
  }
  
  // Mensaje de bienvenida
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Iniciando Sistema...");
  display.display();

  // B) CONEXIÓN WIFI
  WiFi.begin(ssid, password);
  int retry = 0;
  // Intentar conectar durante unos segundos
  while (WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Si conecta, obtenemos la hora de internet
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    display.println("WiFi: CONECTADO");
  } else {
    display.println("WiFi: ERROR (Offline)");
  }
  display.display();
  delay(1000);

  // C) INICIAR SENSOR TEMPERATURA (BMP280)
  // Intentamos dirección 0x76 y luego 0x77
  if (bmp.begin(0x76)) {
    bmpStatus = true;
    Serial.println("BMP280 OK (0x76)");
  } else if (bmp.begin(0x77)) {
    bmpStatus = true;
    Serial.println("BMP280 OK (0x77)");
  } else {
    Serial.println("BMP280 ERROR");
  }

  // D) INICIAR SENSOR PULSO (MAX30102)
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 ERROR");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Error Sensor Pulso");
    display.display();
    // Continuamos aunque falle, pero no medirá pulso
  } else {
    Serial.println("MAX30102 OK");
    particleSensor.setup(); 
    // Ajuste de potencia LED (0x1F = Intensidad media/baja para evitar saturación)
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
  }
  
  display.clearDisplay();
}

// ============================================================================
// LOOP: BUCLE PRINCIPAL
// ============================================================================
void loop() {
  
  // --------------------------------------------------------------------------
  // BLOQUE 1: PROCESAMIENTO RÁPIDO (Lectura de Pulso)
  // Se ejecuta contínuamente sin delays para no perder datos.
  // --------------------------------------------------------------------------
  long irValue = particleSensor.getIR(); // Leer valor Infrarrojo crudo

  // checkForBeat analiza si hubo un cambio brusco en la luz (un latido)
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat; // Tiempo desde el último latido
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0); // Calcular BPM instantáneo

    // Filtro: Descartar lecturas imposibles (<20 o >255)
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; // Guardar en buffer
      rateSpot %= RATE_SIZE; // Mover índice circular
      
      // Calcular promedio suave
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  // Si el valor IR es < 50,000, no hay dedo en el sensor -> BPM es 0
  if (irValue < 50000) beatAvg = 0;

  // --------------------------------------------------------------------------
  // BLOQUE 2: LÓGICA DE ALARMAS Y BUZZER
  // Verifica umbrales y activa el sonido si es necesario.
  // --------------------------------------------------------------------------
  float currentTemp = 0;
  if (bmpStatus) currentTemp = bmp.readTemperature();

  // Condiciones de Alerta:
  bool dangerTemp = (currentTemp > UMBRAL_TEMP_MAX);
  // Solo alertamos por pulso si hay un dedo puesto (beatAvg > 0)
  bool dangerPulse = (beatAvg > 0) && (beatAvg > UMBRAL_BPM_MAX || beatAvg < UMBRAL_BPM_MIN);

  if (dangerTemp || dangerPulse) {
    alarmTriggered = true;
    
    // Generador de tono intermitente (BEEP - silencio - BEEP)
    // Usamos millis() para no bloquear el procesador
    if (millis() - lastBuzzerTone > 200) { // Cada 200ms cambia estado
      lastBuzzerTone = millis();
      buzzerState = !buzzerState;
      digitalWrite(PIN_BUZZER, buzzerState);
    }
  } else {
    // Todo normal -> Apagar alarmas
    alarmTriggered = false;
    digitalWrite(PIN_BUZZER, LOW);
  }

  // --------------------------------------------------------------------------
  // BLOQUE 3: ACTUALIZACIÓN DE PANTALLA (Baja Velocidad)
  // Se actualiza cada 250ms para no parpadear ni saturar la CPU.
  // --------------------------------------------------------------------------
  if (millis() - lastScreenUpdate > 250) {
    display.clearDisplay();

    // --- ZONA SUPERIOR: Ambiente y Hora ---
    display.setTextSize(1);
    
    // Temperatura (Izquierda)
    display.setCursor(0, 0);
    if (dangerTemp) display.print("!! "); // Marca de alerta
    if (bmpStatus) {
      display.print(currentTemp, 1); 
      display.cp437(true); display.write(167); display.print("C"); // Símbolo °
    } else {
      display.print("-- C");
    }

    // Hora (Derecha)
    display.setCursor(95, 0); 
    if(WiFi.status() == WL_CONNECTED) printTime();
    else display.print("--:--");

    display.drawLine(0, 10, 128, 10, WHITE); // Línea divisoria

    // --- ZONA CENTRAL: Ritmo Cardíaco ---
    if (irValue < 50000) {
      // Estado: Esperando dedo
      display.setCursor(30, 25);
      display.print("COLOCA EL");
      display.setCursor(45, 38);
      display.print("DEDO");
    } else {
      // Estado: Midiendo
      display.setCursor(0, 15);
      if (dangerPulse) display.print("PULSO ANORMAL");
      else display.print("Ritmo Normal");

      // Número BPM Grande
      display.setTextSize(3); 
      display.setCursor(35, 25);
      if(beatAvg > 0) display.print(beatAvg);
      else display.print("--"); 
      
      // Unidad pequeña
      display.setTextSize(1);
      display.setCursor(105, 45);
      display.print("BPM");
    }

    // --- ZONA INFERIOR: Fecha o Mensaje de Alerta ---
    display.drawLine(0, 52, 128, 52, WHITE); // Línea divisoria inferior
    display.setCursor(35, 56);
    
    if (alarmTriggered) {
      // Si hay alarma, mostramos aviso parpadeante sincronizado con el buzzer
      if (buzzerState) { 
        display.setTextColor(BLACK, WHITE); // Texto negro, fondo blanco
        display.print("PELIGRO");
        display.setTextColor(WHITE);        // Restaurar color normal
      } else {
        display.print(" PELIGRO ");
      }
    } else {
      // Si todo está bien, mostramos la fecha
      if(WiFi.status() == WL_CONNECTED) printDate();
      else display.print("Monitoreando...");
    }

    display.display(); // Enviar buffer a la pantalla
    lastScreenUpdate = millis(); // Resetear timer de pantalla
  }
}