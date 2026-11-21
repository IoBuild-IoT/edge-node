#pragma once
#include <functional>
struct Pub{std::string topic, payload;};
extern std::vector<Pub> gPubs; extern bool gBrokerUp; extern std::vector<std::string> gSubs;
struct PubSubClient{
  bool c=false; std::function<void(char*,uint8_t*,unsigned)> cb;
  PubSubClient(WiFiClient&){}
  void setServer(const char*,uint16_t){} void setBufferSize(int){} void setSocketTimeout(int){}
  void setCallback(void(*f)(char*,uint8_t*,unsigned)){cb=f;}
  bool connected(){ if(!gBrokerUp) c=false; return c;}
  bool connect(const char*){c=gBrokerUp;return c;} bool connect(const char*,const char*,const char*){return connect("");}
  int state(){return -2;} void loop(){}
  bool subscribe(const char*t,int){gSubs.push_back(t);return true;}
  bool publish(const char*t,const uint8_t*p,size_t n,bool){gPubs.push_back({t,std::string((const char*)p,n)});return true;}
  void inject(const char*t,const char*p){std::string tt=t; cb((char*)tt.c_str(),(uint8_t*)p,strlen(p));}
};
