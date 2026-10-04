#pragma once
#include <Arduino.h>
#include <Wire.h>

constexpr float MPU_LSB_PER_G = 4096.0f;

struct AccelRaw {
  int16_t x;
  int16_t y;
  int16_t z;
};

class MPU6050 {
 public:
  bool begin(TwoWire& wire, uint8_t addr);
  bool readAccel(AccelRaw& out);
  uint8_t whoAmI() const { return _id; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t len);

  TwoWire* _wire = nullptr;
  uint8_t _addr = 0x68;
  uint8_t _id = 0;
};
