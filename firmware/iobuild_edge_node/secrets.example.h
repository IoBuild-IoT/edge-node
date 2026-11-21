#pragma once
// Copia este archivo como secrets.h (misma carpeta) y completa tus datos. NO subas secrets.h a git.

// WiFi 2.4 GHz con salida a internet (casa, universidad, hotspot del celular)
const char *WIFI_SSID = "TU_WIFI";
const char *WIFI_PASS = "TU_CLAVE";

// Broker MQTT de la plataforma IoBuild
//   Plataforma desplegada (VPS/Dokploy): dominio o IP pública del VPS, usuario "iobuild" y MQTT_PASSWORD del despliegue
//   Plataforma local (docker compose en tu PC): IP de la PC en la misma red, usuario y clave vacíos
const char *MQTT_HOST = "mqtt.tu-dominio.com";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_USER = "iobuild";
const char *MQTT_PASS = "CAMBIAR";

// IDs de los dispositivos creados en la app (pantalla de dispositivos de la unidad)
const int ID_LUZ   = 1;   // tipo Smart Light
const int ID_CLIMA = 2;   // tipo Air Conditioner

// Tus relés físicos se activan con LOW. En Wokwi usa false.
const bool RELE_ACTIVO_BAJO = true;
