// Prueba del firmware local: la persiana abre y cierra completo, grado a grado
#include "Arduino.h"
uint32_t gT = 0; int gPins[64]; int gPir = 0; float gTemp = 24, gHum = 55;
#include "../firmware/iobuild_local/iobuild_local.ino"
#include <cassert>
int main() {
  setup();
  auto run = [&](uint32_t u) { for (; gT < u; gT += 10) loop(); };
  run(10000); assert(servoPos == CERRADA);
  gTemp = 25.2; run(12100); assert(persiana == ABIERTA);
  run(13000); assert(servoPos > CERRADA && servoPos < ABIERTA);   // en movimiento
  run(17000); assert(servoPos == ABIERTA);
  gTemp = 24.0; run(24000); assert(servoPos == CERRADA);
  puts("#LOCAL_OK");
}
