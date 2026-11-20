#include "TransparentMode.h"
#include "../infra/Rs485Driver.h"

void TransparentMode::begin() {
  Serial.setDebugOutput(false);

  RS485Serial.flush();
  RS485Serial.end();
  delay(10);
  rs485BeginCurrent();
}

void TransparentMode::loop() {
  while (Serial.available()) {
    RS485Serial.write(Serial.read());
  }

  while (RS485Serial.available()) {
    Serial.write(RS485Serial.read());
  }
}
