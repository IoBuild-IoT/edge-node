# IoBuild · Edge Node (ESP32)

Firmware del prototipo físico de IoBuild. El ESP32 lee los sensores, decide en el borde y se conecta a la plataforma [IoBuild-IoT/web-app](https://github.com/IoBuild-IoT/web-app) como **dispositivo real**. El dashboard desplegado muestra sus datos, y la app puede darle órdenes desde cualquier red.

```
 ESP32 (cualquier WiFi)                 VPS / Dokploy                       Navegador
┌──────────────────────┐   MQTT 1883   ┌──────────────┐   ┌───────────┐   HTTPS   ┌──────────────┐
│ DHT22 · PIR          │──telemetry──▶ │  Mosquitto   │◀─▶│  Backend  │◀────────▶ │ Front (Vue)  │
│ Relés · Servo · Buzzer│◀──commands─── │ (usuario+clave)│   │  .NET API │           │ Hostinger    │
└──────────────────────┘               └──────────────┘   └───────────┘           └──────────────┘
```

El ESP32 **no** habla con el front. Habla con el broker MQTT del backend, así que el broker tiene que ser accesible desde internet.

## Contenido

| Carpeta | Qué es |
|---|---|
| `firmware/iobuild_edge_node/` | Firmware **conectado** a la plataforma (WiFi + MQTT + reglas locales) |
| `firmware/iobuild_local/` | Firmware **sin internet**, solo reglas locales. Sirve para demos y para probar el cableado |
| `wokwi/` | Diagrama del circuito para simular en wokwi.com |
| `web-app-patch/` | Parche para `web-app`: el simulador ignora los IDs que atiende el ESP32 |
| `test/` | Pruebas en PC, sin placa |

## Hardware y pines

| Componente | Pin | Rol en la plataforma |
|---|---|---|
| DHT22 (temperatura y humedad) | D4 | `temperature_c` del clima |
| PIR HC-SR501 | D27 | Presencia para la luz |
| Relé 1 + LED amarillo | D26 | Dispositivo **Smart Light** (luz de pasillo) |
| Relé 2 + LED azul | D25 | Dispositivo **Air Conditioner** (clima) |
| LED verde | D18 | Estado del nodo |
| Buzzer | D23 | Alarma de sobretemperatura |
| Servo SG90 | D19 | Persiana (0° cerrada ↔ 180° abierta) |

Los relés del prototipo se activan con LOW (`RELE_ACTIVO_BAJO = true`). En Wokwi se activan con HIGH, así que ahí va `false`.

## Reglas

| Regla | Sin órdenes de la app | Con órdenes de la app |
|---|---|---|
| **Luz** | El PIR la enciende; se apaga 10 s después del último movimiento | `power=false` o `brightness=0`: apagada aunque haya movimiento. `power=true`: vuelve a mandar el PIR |
| **Clima** | Enciende a ambiente +1.5 °C o humedad +10 %; apaga bajo +0.5 °C y +5 % | `power`, `mode` (cooling/heating/fan) y `targetTemperature`, con histéresis de 0.5 °C |
| **Persiana** | Abre al subir 1 °C y cierra al bajar 1 °C; gira lento (~3.6 s) | Solo local |
| **Alarma** | Buzzer y LED rápido a ambiente +3 °C; se libera bajo +2 °C | Solo local |

| LED verde | Significado |
|---|---|
| Fijo | Conectado a la plataforma |
| Parpadeo lento | Sin conexión (las reglas siguen funcionando) o DHT22 sin lectura |
| Parpadeo rápido | Alarma |

Sin internet, el nodo sigue aplicando sus reglas y reintenta la conexión cada 5 s.

## Conectarlo a la plataforma desplegada

### 1. Servidor (una vez, quien administre el VPS)

El backend y Mosquitto se despliegan con `docker-compose.dokploy.yml` del repo `web-app`. Ese archivo ya publica Mosquitto en el puerto **1883 con usuario y contraseña**.

1. En las variables de Dokploy, define `MQTT_PASSWORD` (y opcionalmente `MQTT_USERNAME`, que por defecto es `iobuild`).
2. Abre el puerto **1883/TCP** en el firewall del VPS (panel de Hostinger → VPS → Firewall, o `ufw allow 1883/tcp`).
3. Aplica `web-app-patch/iobuild-hardware.patch` en `web-app` (`git apply web-app-patch/iobuild-hardware.patch`), haz commit y vuelve a desplegar.
4. Comprueba desde otra red, con [MQTT Explorer](https://mqtt-explorer.com/) o con este comando, que se ven los mensajes:
   ```
   mosquitto_sub -h <IP-o-dominio-del-VPS> -p 1883 -u iobuild -P <MQTT_PASSWORD> -t 'telemetry/#' -v
   ```

> El front publicado en Hostinger solo es la parte visual. Lo que importa es dónde corre el **backend**: el broker debe estar en ese mismo servidor. Si el backend todavía corre solo en una PC, primero despliégalo en el VPS.

### 2. Dispositivos en la app

1. Entra como propietario a la unidad y crea un dispositivo **Smart Light** ("Luz pasillo") y otro **Air Conditioner** ("Clima").
2. Anota sus **IDs**.
3. En Dokploy, define `HARDWARE_DEVICE_IDS=<idLuz>,<idClima>` y vuelve a desplegar. Si te saltas este paso, el simulador también publicará datos falsos para esos dispositivos.

### 3. Firmware

1. Arduino IDE → Gestor de tarjetas → **esp32 by Espressif Systems**. Usa la URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json` en Preferencias si no aparece. Placa: **ESP32 Dev Module**.
2. Instala estas librerías: **DHT sensor library** (Adafruit), **Adafruit Unified Sensor**, **PubSubClient** (Nick O'Leary) y **ArduinoJson** 7.x.
3. En `firmware/iobuild_edge_node/`, copia `secrets.example.h` como **`secrets.h`** y complétalo:
   ```cpp
   const char *WIFI_SSID = "...";            // WiFi 2.4 GHz
   const char *MQTT_HOST = "IP-o-dominio-del-VPS";
   const char *MQTT_USER = "iobuild";
   const char *MQTT_PASS = "<MQTT_PASSWORD>";
   const int ID_LUZ = 7, ID_CLIMA = 8;       // los IDs del paso 2
   ```
   `secrets.h` está en `.gitignore`. **No subas contraseñas al repositorio.**
4. Sube `iobuild_edge_node.ino` y abre el monitor serie a **115200**. Debe aparecer:
   ```
   [MQTT] conectado a ... · escuchando commands/7 y commands/8
   PLATAFORMA: CONECTADO
   ```
   El LED verde queda fijo y los datos aparecen en el dashboard del propietario.

### Plataforma local (desarrollo)

Sigue los mismos pasos, pero con `docker compose up -d` en tu PC, `MQTT_HOST` = IP de la PC en la misma red, y `MQTT_USER`/`MQTT_PASS` vacíos (el broker local es anónimo). `HARDWARE_DEVICE_IDS` va en el `.env` del repo `web-app`.

## Contrato MQTT

Es el mismo que usa `iot-simulator/simulator.py`, así que el backend no necesita cambios.

| Tópico | Dirección | Contenido |
|---|---|---|
| `commands/{id}` | backend → ESP32 (QoS 1, retenido) | Estado deseado completo, p. ej. `{"power":true,"mode":"cooling","targetTemperature":24}` |
| `telemetry/{id}` | ESP32 → backend, cada 5 s y justo después de cada orden | Ver abajo |

```json
{
  "deviceId": 8,
  "eventId": "esp32-8-1795286335-27",
  "timestamp": "2026-11-21T18:38:55.079Z",
  "status": "online",
  "reported": { "power": true, "mode": "cooling", "targetTemperature": 24 },
  "energy_kwh": 0.0017,
  "temperature_c": 25.0,
  "voltage_v": 220,
  "node": { "relay": true, "humidity": 55, "presence": false, "blind_deg": 180, "alarm": false }
}
```

- **`reported`** repite el estado deseado. Así el backend marca la orden como confirmada.
- **`energy_kwh`** es una estimación con consumo nominal (luz 10 W, clima 1200 W), porque el prototipo no tiene medidor.
- **`voltage_v`** es el valor nominal de 220 V.
- **`node`** lleva humedad, presencia, persiana y alarma. El backend todavía no lo guarda: es la base para un panel "Nodo IoBuild Edge".

## Problemas comunes

| Síntoma | Causa probable |
|---|---|
| `[MQTT] sin conexión ... (estado -2)` | El puerto 1883 está cerrado en el firewall del VPS, el host está mal o la red bloquea el puerto (prueba con el hotspot del celular) |
| `(estado 4)` o `(estado 5)` | Usuario o contraseña MQTT incorrectos |
| Conectado, pero el dashboard no muestra datos | Los IDs de `secrets.h` no coinciden con los de la app |
| Los datos saltan o se ven valores raros | Falta `HARDWARE_DEVICE_IDS`: el simulador también publica para esos IDs |
| No compila: `secrets.h: No such file` | Copia `secrets.example.h` como `secrets.h` |
| Relés al revés | Cambia `RELE_ACTIVO_BAJO` |

## Simulación en Wokwi

1. Crea un proyecto ESP32 en wokwi.com.
2. Pega `wokwi/diagram.json` y `wokwi/libraries.txt`.
3. Pega `firmware/iobuild_local/iobuild_local.ino` como `sketch.ino`, con `RELE_ACTIVO_BAJO = false`.

## Pruebas

```
./test/run.sh
```

Compila ambos firmwares en PC con stubs de Arduino y ArduinoJson real, y simula sensores y broker:

- órdenes de la app → relés → confirmación con `reported == desired`
- modos cooling, heating y fan con histéresis
- luz deshabilitada desde la app
- funcionamiento sin red y reconexión
- recorrido completo de la persiana

No reemplazan la prueba en placa real conectada al VPS.

## Seguridad

MQTT en el puerto 1883 viaja **sin cifrar**: alcanza para la demo con usuario y contraseña. Para producción hace falta TLS (puerto 8883) en Mosquitto y `WiFiClientSecure` en el ESP32.
