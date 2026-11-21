#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>
using std::isnan;
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLDOWN 2
#define HEX 16
#define ESP_ARDUINO_VERSION_MAJOR 3
struct String : std::string {
  String() {}
  String(const char*s):std::string(s?s:"") {}
  String(const std::string&s):std::string(s) {}
  String(uint32_t v,int base){char b[16];snprintf(b,16,base==16?"%x":"%u",v);assign(b);}
  String(int v){assign(std::to_string(v));}
  void toLowerCase(){std::transform(begin(),end(),begin(),::tolower);}
  bool concat(const char*s){append(s);return true;} bool concat(char c){push_back(c);return true;}
  bool concat(const char*s,size_t n){append(s,n);return true;}
  String& operator+=(const char*s){append(s);return *this;}
};
inline String operator+(const String&a,const char*b){return String(std::string(a)+b);}
inline String operator+(const String&a,const String&b){return String(std::string(a)+std::string(b));}
inline String operator+(const String&a,int b){return String(std::string(a)+std::to_string(b));}
inline String operator+(const String&a,uint32_t b){return String(std::string(a)+std::to_string(b));}
inline String operator+(const char*a,const String&b){return String(std::string(a)+std::string(b));}
struct S{void begin(int){} template<class...A> void printf(const char*f,A...a){if(getenv("V"))::printf(f,a...);} void println(const char*s=""){if(getenv("V"))puts(s);} };
static S Serial;
extern uint32_t gT; inline uint32_t millis(){return gT;} inline void delay(int){}
extern int gPins[64]; extern int gPir;
inline void pinMode(int,int){} inline void digitalWrite(int p,int v){gPins[p]=v;} inline int digitalRead(int){return gPir;}
inline void ledcAttach(int,int,int){} inline void ledcWrite(int,uint32_t){}
inline void tone(int,int){} inline void noTone(int){}
inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
template<class T> T constrain(T x,T a,T b){return x<a?a:x>b?b:x;}
struct EspC{uint64_t getEfuseMac(){return 0xabcdef12;}}; static EspC ESP;
inline void configTime(int,int,const char*,const char*){}
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
