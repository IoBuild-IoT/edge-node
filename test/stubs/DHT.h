#pragma once
#define DHT22 22
extern float gTemp, gHum;
struct DHT{DHT(int,int){} void begin(){} float readTemperature(){return gTemp;} float readHumidity(){return gHum;}};
