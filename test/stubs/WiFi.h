#pragma once
#define WL_CONNECTED 3
#define WIFI_STA 1
struct WiFiC{int status(){return WL_CONNECTED;} void mode(int){} void begin(const char*,const char*){}}; static WiFiC WiFi;
struct WiFiClient{};
