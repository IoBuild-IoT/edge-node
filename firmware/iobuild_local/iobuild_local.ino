// Arduino IDE: Placa "ESP32 Dev Module" · Librerías: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
/* IoBuild · Prototipo completo (modo local, sin nube)
 * Pines (iguales al diagram.json de Wokwi):
 *   DHT22=4 · PIR=27 · Relé1 (luz pasillo)=26 · Relé2 (clima)=25 · LED estado=18 · Buzzer=23 · Servo=19
 * Reglas:
 *   R1 Luz pasillo : PIR detecta movimiento -> relé 1 ON; OFF 10 s tras el último movimiento
 *   R2 Clima       : T >= ambiente+1.5 °C o HR >= ambiente+10 % -> relé 2 ON;
 *                    OFF cuando T <= ambiente+0.5 y HR <= ambiente+5 (histéresis)
 *   R3 Persiana    : T sube 1 °C -> servo ABRE (180°); baja 1 °C -> CIERRA (0°), girando lento (~3.6 s)
 *   R4 Alarma      : T >= ambiente+3 °C -> buzzer intermitente + LED estado rápido; se libera bajo ambiente+2
 *   LED estado     : fijo = nodo OK · parpadeo rápido = alarma · parpadeo lento = DHT22 sin lectura
 * Al arrancar hace una VALIDACIÓN DE CONEXIONES: prueba cada LED, el buzzer y el servo uno por uno.
 */
#include <Arduino.h>
#include <DHT.h>

#define PIN_DHT     4
#define PIN_PIR     27
#define PIN_RELE1   26
#define PIN_RELE2   25
#define PIN_ESTADO  18
#define PIN_BUZZER  23
#define PIN_SERVO   19

// Muchos módulos de relé físicos se activan con LOW. Si en el autotest el relé hace
// lo contrario (prende cuando dice APAGADO), cambia esto a true.
const bool RELE_ACTIVO_BAJO = true;   // tus relés físicos se activan con LOW (en Wokwi: false)

const uint32_t PIR_CALENTAMIENTO_MS = 30000;
const uint32_t HOLD_MS    = 10000;
const uint32_t T_MUESTREO = 2000;

// Persiana: recorrido y velocidad del servo (más grande = gira más; más ms = más lento)
const int CERRADA = 0, ABIERTA = 180;
const uint32_t MS_POR_GRADO = 20;   // 180° x 20 ms = 3.6 s de giro
const float PASO_SERVO   = 1.0;   // °C para abrir/cerrar persiana
const float CLIMA_ON_T   = 1.5, CLIMA_OFF_T = 0.5;   // °C sobre ambiente
const float CLIMA_ON_H   = 10,  CLIMA_OFF_H = 5;     // % HR sobre ambiente
const float ALARMA_ON_T  = 3.0, ALARMA_OFF_T = 2.0;  // °C sobre ambiente

DHT dht(PIN_DHT, DHT22);

// Estado
bool luzOn = false, climaOn = false, alarma = false, dhtFalla = false;
int  persiana = CERRADA;            // destino: CERRADA o ABIERTA
int  servoPos = CERRADA;            // posición real; avanza grado a grado hacia el destino
uint32_t tServo = 0;
uint32_t tUltMov = 0, tMuestreo = 0;
bool hayMov = false;
float tRef = NAN, tAmb = NAN, hAmb = NAN, tAct = NAN, hAct = NAN;
int  fallasDht = 0;
bool pirPrev = false, luzPrev = false, climaPrev = false, alarmaPrev = false;
int  persianaPrev = CERRADA;

// ---------- Actuadores ----------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void servoInit()           { ledcAttach(PIN_SERVO, 50, 16); }
void servoDuty(uint32_t d) { ledcWrite(PIN_SERVO, d); }
#else
void servoInit()           { ledcSetup(4, 50, 16); ledcAttachPin(PIN_SERVO, 4); }  // canal 4: no choca con tone()
void servoDuty(uint32_t d) { ledcWrite(4, d); }
#endif

void moverServo(int grados) {
  uint32_t us = map(constrain(grados, 0, 180), 0, 180, 500, 2400);
  servoDuty(us * 65535UL / 20000UL);
}

void rele(int pin, bool on) { digitalWrite(pin, (on != RELE_ACTIVO_BAJO) ? HIGH : LOW); }

void buzzer(bool on) { if (on) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER); }

// Prueba un pin con señal ALTA y luego BAJA, para descubrir cómo responde lo conectado
void probarPin(int pin, const char *nombre) {
  Serial.printf("\n[%s]\n", nombre);
  Serial.println("  Señal ALTA (3.3 V) 2 s -> debe PRENDER si el LED va directo o el relé es activo-alto");
  digitalWrite(pin, HIGH); delay(2000);
  Serial.println("  Señal BAJA (0 V) 2 s   -> si prende AHORA y no antes: relé activo-bajo, pon RELE_ACTIVO_BAJO = true");
  digitalWrite(pin, LOW);  delay(2000);
  Serial.println("  Si no prendió en ninguno de los dos: revisa cableado, polaridad del LED o alimentación del relé");
  rele(pin, false);
}

void autotest() {
  Serial.println("\n======== VALIDACIÓN DE CONEXIONES ========");
  Serial.println("Mira cada componente mientras la consola lo nombra.");
  probarPin(PIN_RELE1, "1/5 LED amarillo · luz pasillo · D26");
  probarPin(PIN_RELE2, "2/5 LED azul · clima · D25");

  Serial.println("\n[3/5 LED verde · estado · D18]  ENCENDIDO 2 s");
  digitalWrite(PIN_ESTADO, HIGH); delay(2000); digitalWrite(PIN_ESTADO, LOW);

  Serial.println("\n[4/5 Buzzer · alarma · D23]  3 pitidos");
  for (int i = 0; i < 3; i++) { buzzer(true); delay(300); buzzer(false); delay(300); }

  Serial.println("\n[5/5 Servo · persiana · D19]  0° -> 180° -> 0° (lento)");
  for (int g = CERRADA; g <= ABIERTA; g++) { moverServo(g); delay(MS_POR_GRADO); }
  delay(500);
  for (int g = ABIERTA; g >= CERRADA; g--) { moverServo(g); delay(MS_POR_GRADO); }

  Serial.println("\n======== FIN VALIDACIÓN ========\n");
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_RELE1, OUTPUT);  rele(PIN_RELE1, false);
  pinMode(PIN_RELE2, OUTPUT);  rele(PIN_RELE2, false);
  pinMode(PIN_ESTADO, OUTPUT); digitalWrite(PIN_ESTADO, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  dht.begin();
  servoInit();
  Serial.println("IoBuild · prototipo completo (modo local)");
  autotest();
  moverServo(servoPos);
  Serial.println("PIR calentando 30 s (se ignora). No toques el DHT22: se toma el ambiente de referencia.");
}

// ---------- Loop ----------
void loop() {
  uint32_t now = millis();

  // R1: PIR con calentamiento y antirrebote de 300 ms
  static bool pirEst = false, pirRaw = false; static uint32_t tRaw = 0;
  bool lect = digitalRead(PIN_PIR);
  if (lect != pirRaw) { pirRaw = lect; tRaw = now; }
  if (now - tRaw >= 300) pirEst = lect;
  bool pir = pirEst;
  if (now > PIR_CALENTAMIENTO_MS && pir && (!pirPrev || !hayMov)) { tUltMov = now; hayMov = true; }
  luzOn = hayMov && (now - tUltMov < HOLD_MS);
  rele(PIN_RELE1, luzOn);

  if (pir != pirPrev) {
    pirPrev = pir;
    Serial.printf("[%6lus] PIR: %s%s\n", now / 1000, pir ? "MOVIMIENTO detectado" : "sin movimiento",
                  (pir && now <= PIR_CALENTAMIENTO_MS) ? "  (ignorado: calentando)" : "");
  }
  if (luzOn != luzPrev) {
    luzPrev = luzOn;
    Serial.printf("[%6lus] LUZ PASILLO: %s\n", now / 1000, luzOn ? "ENCENDIDA" : "APAGADA (10 s sin movimiento)");
  }

  // Lectura DHT22 cada 2 s: R2, R3, R4
  if (now - tMuestreo >= T_MUESTREO) {
    tMuestreo = now;
    float t = dht.readTemperature(), h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      if (++fallasDht >= 3 && !dhtFalla) { dhtFalla = true; Serial.printf("[%6lus] DHT22: SIN LECTURA (revisa cable en D4)\n", now / 1000); }
    } else {
      if (dhtFalla) Serial.printf("[%6lus] DHT22: lectura recuperada\n", now / 1000);
      fallasDht = 0; dhtFalla = false;
      tAct = t; hAct = h;
      if (isnan(tAmb)) { tAmb = t; hAmb = h; tRef = t; }

      // El ambiente sigue cambios lentos solo en reposo
      if (!climaOn && !alarma) { tAmb += 0.02 * (t - tAmb); hAmb += 0.02 * (h - hAmb); }

      // R2: clima con histéresis
      if (!climaOn && (t >= tAmb + CLIMA_ON_T || h >= hAmb + CLIMA_ON_H)) climaOn = true;
      else if (climaOn && t <= tAmb + CLIMA_OFF_T && h <= hAmb + CLIMA_OFF_H) climaOn = false;

      // R3: persiana por pasos de 1 °C
      if (persiana == CERRADA) { if (t < tRef) tRef = t; if (t >= tRef + PASO_SERVO) { persiana = ABIERTA; tRef = t; } }
      else                { if (t > tRef) tRef = t; if (t <= tRef - PASO_SERVO) { persiana = CERRADA; tRef = t; } }

      // R4: alarma de sobretemperatura
      if (!alarma && t >= tAmb + ALARMA_ON_T) alarma = true;
      else if (alarma && t <= tAmb + ALARMA_OFF_T) alarma = false;
    }
    if (dhtFalla) climaOn = false;   // sin datos, clima a estado seguro

    rele(PIN_RELE2, climaOn);

    if (climaOn != climaPrev) { climaPrev = climaOn; Serial.printf("[%6lus] CLIMA: %s\n", now / 1000, climaOn ? "ENCENDIDO" : "APAGADO"); }
    if (persiana != persianaPrev) { persianaPrev = persiana; Serial.printf("[%6lus] PERSIANA: %s (%d°)\n", now / 1000, persiana == ABIERTA ? "ABIERTA" : "CERRADA", persiana); }
    if (alarma != alarmaPrev) { alarmaPrev = alarma; Serial.printf("[%6lus] ALARMA: %s\n", now / 1000, alarma ? "SOBRETEMPERATURA" : "liberada"); }

    Serial.printf("[%6lus] T=%.1f°C HR=%.0f%% (amb %.1f°C %.0f%%) · PIR=%d · luz=%s clima=%s persiana=%d° alarma=%s\n",
                  now / 1000, tAct, hAct, tAmb, hAmb, pir, luzOn ? "ON" : "OFF", climaOn ? "ON" : "OFF",
                  persiana, alarma ? "SI" : "no");
  }

  // LED estado y buzzer
  if (alarma)        digitalWrite(PIN_ESTADO, (now / 100) % 2);   // rápido: alarma
  else if (dhtFalla) digitalWrite(PIN_ESTADO, (now / 500) % 2);   // lento: DHT22 sin lectura
  else               digitalWrite(PIN_ESTADO, HIGH);              // fijo: todo OK

  static bool bzOn = false;
  bool bzQuiere = alarma && (now / 250) % 2;
  if (bzQuiere != bzOn) { bzOn = bzQuiere; buzzer(bzOn); }

  // Persiana: barrido lento, 1° cada MS_POR_GRADO, sin bloquear el resto
  if (servoPos != persiana && now - tServo >= MS_POR_GRADO) {
    tServo = now;
    servoPos += persiana > servoPos ? 1 : -1;
    moverServo(servoPos);
  }
}
