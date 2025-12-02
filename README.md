# Monitor de Signos Vitales y Ambiente con ESP32
Este proyecto consiste en un sistema integrado de monitoreo de salud y ambiente basado en el microcontrolador ESP32. 
El dispositivo visualiza el ritmo cardíaco (BPM), la temperatura ambiental, y la hora/fecha actual sincronizada por internet, alertando mediante sonido y señales visuales si se detectan anomalías.

## Características Principales
- Lectura de Pulso Cardíaco: Uso del sensor MAX30102 para cálculo de BPM en tiempo real.
- Monitoreo Ambiental: Medición de temperatura (°C) mediante sensor BMP280.
- Interfaz Visual: Pantalla OLED 128x64 con interfaz gráfica dividida (Cabecera, Cuerpo, Pie).
- Sincronización NTP: Hora y fecha automáticas vía Wi-Fi.
- Sistema de Alertas:
  - Audio: Buzzer activo en caso de valores fuera de rango.
  - Visual: Mensaje de "PELIGRO" en pantalla.
 
## Hardware Requerido
1. Microcontrolador: ESP32 (Dev Kit V1).
2. Pantalla: OLED 0.96" I2C (Driver SSD1306).
3. Sensor de Pulso: MAX30102 (Módulo I2C).
4. Sensor de Temperatura/Presión: BMP280 (Módulo I2C).
5. Actuador: Buzzer (Zumbador) activo o pasivo.
6. Cables: Jumpers y Protoboard.

## Diagrama de Conexiones
Todos los sensores comparten el bus I2C del ESP32.
| **Componente**   | **Pin Componente** | **Pin ESP32** | **Notas**                             |
| ---------------- | ------------------ | ------------- | ------------------------------------- |
| **Bus I2C**      | SDA                | **GPIO 21**   | Todos los SDA van aquí                |
| **Bus I2C**      | SCL                | **GPIO 22**   | Todos los SCL van aquí                |
| **Alimentación** | VCC / VIN          | **3.3V**      | Algunos módulos MAX30102 requieren 5V |
| **Tierra**       | GND                | **GND**       | Tierra común para todo                |
| **Buzzer**       | Positivo (+)       | **GPIO 18**   | Pin configurable en código            |
| **Buzzer**       | Negativo (-)       | **GND**       | -                                     |

## Librerías Necesarias
Para compilar este código en Arduino IDE, debes instalar las siguientes librerías desde el Gestor de Bibliotecas:
1. Adafruit GFX Library
2. Adafruit SSD1306
3. Adafruit BMP280 Library
4. SparkFun MAX3010x Pulse and Proximity Sensor Library
Nota: Las librerías WiFi.h y time.h vienen instaladas por defecto en el paquete de tarjetas ESP32.

## Configuración del Código
Antes de subir el código, asegúrate de editar las siguientes líneas al inicio del archivo .ino:
1. Conexion a Wi-Fi
 ```
const char* ssid     = "NOMBRE_TU_RED"; 
const char* password = "TU_CONTRASEÑA";
```
2. Zona Horaria
Ajusta el gmtOffset_sec según tu país (Segundos de diferencia con UTC).
- México (Centro): -21600
- España: 3600
- Argentina: -10800

3. Umbrales de Alarma
Puedes calibrar cuándo se dispara la alarma de peligro:
 ```
const float UMBRAL_TEMP_MAX = 30.0; // Temp. ambiente crítica (ej. para pruebas)
const int UMBRAL_BPM_MAX = 110;     // Taquicardia
const int UMBRAL_BPM_MIN = 50;      // Bradicardia
```
## Instrucciones de Uso
1. Encendido: Al conectar el ESP32, mostrará "Conectando WiFi...".
2. Reposo: Si no se detecta un dedo, mostrará la temperatura, hora y el mensaje "COLOCA DEDO".
3. Medición: Coloca la yema del dedo suavemente sobre el sensor MAX30102 (luz roja).
  - Nota: Si presionas demasiado fuerte, cortas la circulación y la lectura fallará.
4. Estabilización: Espera entre 5 y 10 segundos para que el promedio de BPM se estabilice.
5. Alertas: Si la temperatura sube del umbral o el ritmo cardíaco es anormal, sonará el buzzer y la pantalla parpadeará.

## Licencia
Este proyecto es de código abierto para fines educativos.
