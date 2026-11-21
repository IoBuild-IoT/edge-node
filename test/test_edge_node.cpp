#include "Arduino.h"
#include "WiFi.h"
#include "PubSubClient.h"
uint32_t gT=0; int gPins[64]; int gPir=0; float gTemp=24, gHum=55;
std::vector<Pub> gPubs; bool gBrokerUp=true; std::vector<std::string> gSubs;
#include "../firmware/iobuild_edge_node/iobuild_edge_node.ino"
#include <cassert>
void run(uint32_t until){ for(;gT<until;gT+=50) loop(); }
bool rele1(){ return gPins[PIN_RELE1]==LOW; }  // activo-bajo
bool rele2(){ return gPins[PIN_RELE2]==LOW; }
void mark(const char*m){ printf("#%s\n",m); }
int main(){
  setup(); run(35000);
  assert(gSubs.size()==2 && gSubs[0]=="commands/1" && gSubs[1]=="commands/2");
  // Luz habilitada por defecto: PIR enciende
  gPir=1; run(36000); gPir=0; assert(rele1()); run(47000); assert(!rele1());
  // App apaga la luz: PIR ya no enciende
  gPubs.clear(); mqtt.inject("commands/1","{\"power\":false}"); run(47200);
  mark("ACK_LUZ_OFF"); for(auto&p:gPubs) printf("%s %s\n",p.topic.c_str(),p.payload.c_str());
  gPir=1; run(48000); gPir=0; assert(!rele1()); run(50000);
  // App la vuelve a habilitar con brillo
  mqtt.inject("commands/1","{\"power\":true,\"brightness\":80}"); run(50200);
  gPir=1; run(51000); gPir=0; assert(rele1()); run(62000); assert(!rele1());
  // Clima: sin orden -> automático relativo (T ambiente 24)
  assert(!rele2());
  // App: cooling a 24, T=25 -> ON
  gTemp=25.0; gPubs.clear();
  mqtt.inject("commands/2","{\"power\":true,\"mode\":\"cooling\",\"targetTemperature\":24}"); run(65000);
  mark("ACK_CLIMA_ON"); for(auto&p:gPubs) if(p.topic=="telemetry/2"){printf("%s %s\n",p.topic.c_str(),p.payload.c_str());break;}
  assert(rele2());
  gTemp=23.4; run(69000); assert(!rele2());       // bajo setpoint-0.5 apaga
  gTemp=24.2; run(73000); assert(!rele2());       // dentro de histéresis sigue OFF
  gTemp=24.6; run(77000); assert(rele2());        // sobre setpoint+0.5 enciende
  // Backend manda desired completo con power false
  gPubs.clear(); mqtt.inject("commands/2","{\"power\":false,\"mode\":\"cooling\",\"targetTemperature\":24}"); run(80000);
  assert(!rele2());
  mark("ACK_CLIMA_OFF"); for(auto&p:gPubs) if(p.topic=="telemetry/2"){printf("%s %s\n",p.topic.c_str(),p.payload.c_str());break;}
  // Periódica cada 5 s
  gPubs.clear(); run(100000); int n1=0,n2=0; for(auto&p:gPubs){n1+=p.topic=="telemetry/1"; n2+=p.topic=="telemetry/2";}
  printf("#PERIODIC %d %d\n",n1,n2); assert(n1>=3 && n2>=3);
  // Sin broker: reglas locales siguen
  gBrokerUp=false; gPubs.clear(); gPir=1; run(101000); gPir=0; assert(rele1()); run(112000); assert(!rele1()); assert(gPubs.empty());
  // Vuelve el broker: reconecta y resuscribe
  gBrokerUp=true; gSubs.clear(); run(120000); assert(gSubs.size()==2);
  // Fan: siempre ON
  mqtt.inject("commands/2","{\"power\":true,\"mode\":\"fan\",\"targetTemperature\":24}"); gTemp=20; run(124000); assert(rele2());
  // Heating a 26 con T=20 -> ON; T=27 -> OFF
  mqtt.inject("commands/2","{\"power\":true,\"mode\":\"heating\",\"targetTemperature\":26}"); run(128000); assert(rele2());
  gTemp=27; run(132000); assert(!rele2());
  puts("#ALL_ASSERTS_OK");
}
