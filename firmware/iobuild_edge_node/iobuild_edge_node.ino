// IoBuild · Nodo edge ESP32 conectado a la plataforma
// Arduino IDE: Placa "ESP32 Dev Module"
// Librerías: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor" + "PubSubClient" (Nick O'Leary) + "ArduinoJson" 7.x
/*
 * El nodo habla el MISMO contrato MQTT que iot-simulator/simulator.py, así que el backend
 * lo trata como un dispositivo real sin cambios:
 *   publica   telemetry/{id}  {deviceId, eventId, timestamp, status, reported, energy_kwh, temperature_c, voltage_v}
 *   escucha   commands/{id}   JSON con el estado deseado (retenido, QoS 1)
 *   ack       al recibir una orden publica telemetry con reported == deseado (el backend la marca confirmada)
 *
 * Dispositivos de la plataforma que representa este nodo:
 *   ID_LUZ   -> tipo SmartLight     -> relé 1 (D26) · luz de pasillo con sensor de presencia
 *   ID_CLIMA -> tipo AirConditioner -> relé 2 (D25) · climatización con setpoint
 *
 * Reglas de borde (siguen funcionando sin internet):
 *   Luz      : habilitada (power=true, por defecto) -> PIR la enciende; OFF 10 s tras el último movimiento.
 *              power=false o brightness=0 desde la app -> apagada aunque haya movimiento.
 *   Clima    : sin órdenes de la app -> automático: ON a ambiente+1.5 °C o HR ambiente+10 %.
 *              con órdenes -> power=false apaga; power=true regula a targetTemperature según mode
 *              (cooling: ON sobre setpoint · heating: ON bajo setpoint · fan: siempre ON), histéresis 0.5 °C.
 *   Persiana : T sube 1 °C -> abre (180°); baja 1 °C -> cierra (0°), girando lento (~3.6 s). Solo local.
 *   Alarma   : T >= ambiente+3 °C -> buzzer + LED estado rápido; se libera bajo ambiente+2.
 *   LED estado (D18): fijo = conectado a la plataforma · lento = modo local (sin red) o DHT22 caído · rápido = alarma.
 *
 * Pines: DHT22=4 · PIR=27 · Relé1=26 · Relé2=25 · LED estado=18 · Buzzer=23 · Servo=19
 */
#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

// Configuración (WiFi, broker, IDs): copia secrets.example.h como secrets.h y edítalo.
// secrets.h está en .gitignore para no publicar contraseñas.
#include "secrets.h"

#define PIN_DHT     4
#define PIN_PIR     27
#define PIN_RELE1   26
#define PIN_RELE2   25
#define PIN_ESTADO  18
#define PIN_BUZZER  23
#define PIN_SERVO   19

const uint32_t PIR_CALENTAMIENTO_MS = 30000;
const uint32_t HOLD_MS        = 10000;
const uint32_t T_MUESTREO     = 2000;   // DHT22
const uint32_t T_TELEMETRIA   = 5000;   // mismo ritmo que el simulador
const uint32_t T_REINTENTO    = 5000;

// Persiana: recorrido y velocidad del servo (más grande = gira más; más ms = más lento)
const int CERRADA = 0, ABIERTA = 180;
const uint32_t MS_POR_GRADO = 20;   // 180° x 20 ms = 3.6 s de giro
const float PASO_SERVO  = 1.0;
const float CLIMA_ON_T  = 1.5, CLIMA_OFF_T = 0.5;
const float CLIMA_ON_H  = 10,  CLIMA_OFF_H = 5;
const float ALARMA_ON_T = 3.0, ALARMA_OFF_T = 2.0;
const float HISTERESIS_SETPOINT = 0.5;

// Consumo nominal para estimar energía (no hay medidor). ponytail: estimado, cambia si mides el real.
const float W_LUZ = 10, W_CLIMA = 1200, W_REPOSO = 0.5;
const float VOLTAJE_NOMINAL = 220.0;   // sin sensor de voltaje: valor nominal de red

DHT dht(PIN_DHT, DHT22);
WiFiClient wifi;
PubSubClient mqtt(wifi);

// Estado deseado que llega de la plataforma (se reporta tal cual para el ack)
JsonDocument desLuz, desClima;
bool ackLuz = false, ackClima = false;

// Estado local
bool luzOn = false, climaOn = false, alarma = false, dhtFalla = false;
int  persiana = CERRADA;            // destino: CERRADA o ABIERTA
int  servoPos = CERRADA;            // posición real; avanza grado a grado hacia el destino
uint32_t tServo = 0;
uint32_t tUltMov = 0, tMuestreo = 0, tTelemetria = 0, tReintento = 0;
bool hayMov = false, pir = false;
float tRef = NAN, tAmb = NAN, hAmb = NAN, tAct = NAN, hAct = NAN;
int  fallasDht = 0;
bool pirPrev = false, luzPrev = false, climaPrev = false, alarmaPrev = false, conectadoPrev = false;
int  persianaPrev = CERRADA;
uint32_t secuencia = 0;

// ---------- Actuadores ----------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void servoInit()           { ledcAttach(PIN_SERVO, 50, 16); }
void servoDuty(uint32_t d) { ledcWrite(PIN_SERVO, d); }
#else
void servoInit()           { ledcSetup(4, 50, 16); ledcAttachPin(PIN_SERVO, 4); }
void servoDuty(uint32_t d) { ledcWrite(4, d); }
#endif

void moverServo(int grados) {
  uint32_t us = map(constrain(grados, 0, 180), 0, 180, 500, 2400);
  servoDuty(us * 65535UL / 20000UL);
}
void rele(int pin, bool on) { digitalWrite(pin, (on != RELE_ACTIVO_BAJO) ? HIGH : LOW); }
void buzzer(bool on) { if (on) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER); }

void autotest() {
  Serial.println("\n== VALIDACIÓN: relé 1, relé 2, LED verde, buzzer, servo ==");
  rele(PIN_RELE1, true);  delay(1000); rele(PIN_RELE1, false);
  rele(PIN_RELE2, true);  delay(1000); rele(PIN_RELE2, false);
  digitalWrite(PIN_ESTADO, HIGH); delay(1000); digitalWrite(PIN_ESTADO, LOW);
  for (int i = 0; i < 3; i++) { buzzer(true); delay(200); buzzer(false); delay(200); }
  for (int g = CERRADA; g <= ABIERTA; g++) { moverServo(g); delay(MS_POR_GRADO); }
  for (int g = ABIERTA; g >= CERRADA; g--) { moverServo(g); delay(MS_POR_GRADO); }
  Serial.println("== FIN VALIDACIÓN ==\n");
}

// ---------- Órdenes de la plataforma ----------
bool leerBool(JsonDocument &d, const char *k, bool porDefecto) {
  JsonVariant v = d[k];
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<const char *>()) { String s = v.as<const char *>(); s.toLowerCase(); return s == "true" || s == "on"; }
  return porDefecto;
}

bool luzHabilitada() {
  return leerBool(desLuz, "power", true) && (desLuz["brightness"].isNull() || desLuz["brightness"].as<float>() > 0);
}

// Clima: devuelve el estado que pide la plataforma, o el automático si nunca llegó una orden
bool climaSegunOrden(float t, bool actual, bool automatico) {
  if (desClima.size() == 0 || desClima["power"].isNull()) return automatico;
  if (!leerBool(desClima, "power", false)) return false;
  const char *modo = desClima["mode"] | "cooling";
  float sp = desClima["targetTemperature"] | 24.0f;
  if (strcmp(modo, "fan") == 0) return true;
  if (isnan(t)) return actual;
  if (strcmp(modo, "heating") == 0) return actual ? t < sp + HISTERESIS_SETPOINT : t <= sp - HISTERESIS_SETPOINT;
  return actual ? t > sp - HISTERESIS_SETPOINT : t >= sp + HISTERESIS_SETPOINT;   // cooling
}

void alRecibir(char *topic, byte *payload, unsigned int len) {
  const char *barra = strrchr(topic, '/');
  if (!barra) return;
  int id = atoi(barra + 1);
  JsonDocument orden;
  if (deserializeJson(orden, payload, len) || !orden.is<JsonObject>()) { Serial.printf("[MQTT] orden inválida en %s\n", topic); return; }
  JsonDocument *destino = id == ID_LUZ ? &desLuz : id == ID_CLIMA ? &desClima : nullptr;
  if (!destino) return;
  for (JsonPair kv : orden.as<JsonObject>()) (*destino)[kv.key()] = kv.value();
  if (id == ID_LUZ) ackLuz = true; else ackClima = true;   // se publica en loop(): el buffer MQTT es compartido
  tMuestreo = millis() - T_MUESTREO;                         // re-evalúa reglas antes del ack
  String txt; serializeJson(*destino, txt);
  Serial.printf("[APP] orden para %s: %s\n", id == ID_LUZ ? "LUZ" : "CLIMA", txt.c_str());
}

// ---------- Telemetría ----------
bool horaValida() { return time(nullptr) > 1700000000; }

String marcaTiempo() {
  struct timeval tv; gettimeofday(&tv, nullptr);
  struct tm t; gmtime_r(&tv.tv_sec, &t);
  char b[32]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t);
  char out[64]; snprintf(out, sizeof out, "%s.%03dZ", b, (int)(tv.tv_usec / 1000));
  return out;
}

void publicarTelemetria(int id) {
  bool esLuz = id == ID_LUZ;
  JsonDocument &des = esLuz ? desLuz : desClima;
  bool on = esLuz ? luzOn : climaOn;
  bool encendidoLogico = esLuz ? luzHabilitada() : (des["power"].isNull() || leerBool(des, "power", false));

  JsonDocument msg;
  msg["deviceId"]  = id;
  msg["eventId"]   = String("esp32-") + id + "-" + (uint32_t)time(nullptr) + "-" + (++secuencia);
  msg["timestamp"] = marcaTiempo();
  msg["status"]    = encendidoLogico ? "online" : "idle";
  msg["energy_kwh"]    = (on ? (esLuz ? W_LUZ : W_CLIMA) : W_REPOSO) * (T_TELEMETRIA / 3600000.0) / 1000.0;
  msg["temperature_c"] = (!esLuz && !isnan(tAct)) ? tAct : 0.0;
  msg["voltage_v"]     = VOLTAJE_NOMINAL;
  if (des.size()) msg["reported"] = des;   // igual al deseado: así el backend confirma la orden
  // Datos extra del nodo (el backend los ignora hoy; quedan listos para el dashboard)
  JsonObject n = msg["node"].to<JsonObject>();
  n["relay"] = on;
  if (!isnan(hAct)) n["humidity"] = hAct;
  n["presence"] = pir;
  n["blind_deg"] = persiana;
  n["alarm"] = alarma;

  char buf[768];
  size_t len = serializeJson(msg, buf, sizeof buf);
  char topic[32]; snprintf(topic, sizeof topic, "telemetry/%d", id);
  mqtt.publish(topic, (const uint8_t *)buf, len, false);
}

void mantenerConexion(uint32_t now) {
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (WiFi.status() != WL_CONNECTED || now - tReintento < T_REINTENTO) return;
  tReintento = now;
  String cid = "iobuild-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = strlen(MQTT_USER) ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS) : mqtt.connect(cid.c_str());
  if (!ok) { Serial.printf("[MQTT] sin conexión a %s:%u (estado %d), reintento en 5 s\n", MQTT_HOST, MQTT_PORT, mqtt.state()); return; }
  char t[32];
  snprintf(t, sizeof t, "commands/%d", ID_LUZ);   mqtt.subscribe(t, 1);
  snprintf(t, sizeof t, "commands/%d", ID_CLIMA); mqtt.subscribe(t, 1);
  Serial.printf("[MQTT] conectado a %s · escuchando commands/%d y commands/%d\n", MQTT_HOST, ID_LUZ, ID_CLIMA);
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
  Serial.println("IoBuild · nodo edge conectado");
  autotest();
  moverServo(servoPos);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);           // no bloquea: las reglas locales arrancan ya
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(alRecibir);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(3);
  Serial.printf("WiFi: conectando a \"%s\"... (las reglas locales ya funcionan)\n", WIFI_SSID);
  Serial.println("PIR calentando 30 s. No toques el DHT22: se toma el ambiente de referencia.");
}

// ---------- Loop ----------
void loop() {
  uint32_t now = millis();
  mantenerConexion(now);
  bool conectado = mqtt.connected();
  if (conectado != conectadoPrev) {
    conectadoPrev = conectado;
    Serial.printf("[%6lus] PLATAFORMA: %s\n", now / 1000, conectado ? "CONECTADO" : "sin conexión (modo local)");
  }

  // R1: PIR con calentamiento y antirrebote
  static bool pirEst = false, pirRaw = false; static uint32_t tRaw = 0;
  bool lect = digitalRead(PIN_PIR);
  if (lect != pirRaw) { pirRaw = lect; tRaw = now; }
  if (now - tRaw >= 300) pirEst = lect;
  pir = pirEst;
  if (now > PIR_CALENTAMIENTO_MS && pir && (!pirPrev || !hayMov)) { tUltMov = now; hayMov = true; }
  luzOn = luzHabilitada() && hayMov && (now - tUltMov < HOLD_MS);
  rele(PIN_RELE1, luzOn);

  if (pir != pirPrev) {
    pirPrev = pir;
    Serial.printf("[%6lus] PIR: %s%s\n", now / 1000, pir ? "MOVIMIENTO" : "sin movimiento",
                  (pir && now <= PIR_CALENTAMIENTO_MS) ? " (ignorado: calentando)" : "");
  }
  if (luzOn != luzPrev) { luzPrev = luzOn; Serial.printf("[%6lus] LUZ: %s\n", now / 1000, luzOn ? "ENCENDIDA" : "APAGADA"); }

  // DHT22: R2 clima, R3 persiana, R4 alarma
  if (now - tMuestreo >= T_MUESTREO) {
    tMuestreo = now;
    float t = dht.readTemperature(), h = dht.readHumidity();
    bool climaAuto = climaOn;
    if (isnan(t) || isnan(h)) {
      if (++fallasDht >= 3 && !dhtFalla) { dhtFalla = true; Serial.printf("[%6lus] DHT22: SIN LECTURA (cable D4)\n", now / 1000); }
    } else {
      fallasDht = 0; dhtFalla = false;
      tAct = t; hAct = h;
      if (isnan(tAmb)) { tAmb = t; hAmb = h; tRef = t; }
      if (!climaOn && !alarma) { tAmb += 0.02 * (t - tAmb); hAmb += 0.02 * (h - hAmb); }

      if (!climaOn && (t >= tAmb + CLIMA_ON_T || h >= hAmb + CLIMA_ON_H)) climaAuto = true;
      else if (climaOn && t <= tAmb + CLIMA_OFF_T && h <= hAmb + CLIMA_OFF_H) climaAuto = false;

      if (persiana == CERRADA) { if (t < tRef) tRef = t; if (t >= tRef + PASO_SERVO) { persiana = ABIERTA; tRef = t; } }
      else                { if (t > tRef) tRef = t; if (t <= tRef - PASO_SERVO) { persiana = CERRADA; tRef = t; } }

      if (!alarma && t >= tAmb + ALARMA_ON_T) alarma = true;
      else if (alarma && t <= tAmb + ALARMA_OFF_T) alarma = false;
    }
    climaOn = dhtFalla ? false : climaSegunOrden(tAct, climaOn, climaAuto);

    if (persiana != persianaPrev) { persianaPrev = persiana; Serial.printf("[%6lus] PERSIANA: %s\n", now / 1000, persiana == ABIERTA ? "ABIERTA" : "CERRADA"); }
    if (alarma != alarmaPrev) { alarmaPrev = alarma; Serial.printf("[%6lus] ALARMA: %s\n", now / 1000, alarma ? "SOBRETEMPERATURA" : "liberada"); }
    Serial.printf("[%6lus] T=%.1f°C HR=%.0f%% (amb %.1f) · luz=%s clima=%s persiana=%d° alarma=%s · %s\n",
                  now / 1000, tAct, hAct, tAmb, luzOn ? "ON" : "OFF", climaOn ? "ON" : "OFF", persiana,
                  alarma ? "SI" : "no", conectado ? "online" : "local");
  }
  rele(PIN_RELE2, climaOn);
  if (climaOn != climaPrev) { climaPrev = climaOn; Serial.printf("[%6lus] CLIMA: %s\n", now / 1000, climaOn ? "ENCENDIDO" : "APAGADO"); }

  // Telemetría: ack inmediato tras una orden y envío periódico
  if (conectado && horaValida()) {
    if (ackLuz)   { ackLuz = false;   publicarTelemetria(ID_LUZ); }
    if (ackClima) { ackClima = false; publicarTelemetria(ID_CLIMA); }
    if (now - tTelemetria >= T_TELEMETRIA) {
      tTelemetria = now;
      publicarTelemetria(ID_LUZ);
      publicarTelemetria(ID_CLIMA);
    }
  }

  // LED estado y buzzer
  if (alarma)                     digitalWrite(PIN_ESTADO, (now / 100) % 2);
  else if (dhtFalla || !conectado) digitalWrite(PIN_ESTADO, (now / 500) % 2);
  else                            digitalWrite(PIN_ESTADO, HIGH);

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
