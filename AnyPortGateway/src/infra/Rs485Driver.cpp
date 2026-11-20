#include "Rs485Driver.h"

HardwareSerial RS485Serial(1);
uint32_t g_rs485CurrentBaud = RS485_DEFAULT_BAUDRATE;
uint32_t g_rs485CurrentConfig = SERIAL_8N1;
volatile bool g_rs485Busy = false;

const Rs485PortConfig &getCurrentRs485Config() {
  return (g_rs485Channel == 1) ? g_rs485_2Config : g_rs485_1Config;
}

void rs485Begin(uint32_t baud, uint32_t config) {
  const bool ch2 = (g_rs485Channel == 1);
  RS485Serial.begin(baud, config, ch2 ? PIN_RS485_2_RX : PIN_RS485_1_RX,
                    ch2 ? PIN_RS485_2_TX : PIN_RS485_1_TX);
}

void rs485BeginCurrent() {
  const Rs485PortConfig &cfg = getCurrentRs485Config();
  uint32_t conf = getSerialConfig(cfg.dataBits, cfg.parity, cfg.stopBits);
  rs485Begin(cfg.baud, conf);
}

void initRs485Port() {
  const Rs485PortConfig &cfg = getCurrentRs485Config();
  g_rs485CurrentBaud = cfg.baud;
  g_rs485CurrentConfig = getSerialConfig(cfg.dataBits, cfg.parity, cfg.stopBits);
  rs485BeginCurrent();
  APP_PRINTLN("[RS485] hardware serial initialized with global port config");
}
