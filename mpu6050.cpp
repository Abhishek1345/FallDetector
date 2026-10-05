#include "mpu6050.h"

namespace {
constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_INT_PIN_CFG = 0x37;
constexpr uint8_t REG_INT_ENABLE = 0x38;
constexpr uint8_t REG_INT_STATUS = 0x3A;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr uint8_t PWR_DEVICE_RESET = 0x80;
constexpr uint8_t PWR_CLKSEL_PLL_X = 0x01;
constexpr uint8_t CONFIG_DLPF_44HZ = 0x03;
constexpr uint8_t SMPLRT_DIV_100HZ = 9;
constexpr uint8_t ACCEL_RANGE_8G = 0x10;
constexpr uint8_t INT_LATCH_AND_CLEAR_ON_READ = 0x30;
constexpr uint8_t INT_DATA_READY_EN = 0x01;
}  // namespace

bool MPU6050::writeReg(uint8_t reg, uint8_t value) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(value);
  return _wire->endTransmission() == 0;
}

bool MPU6050::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;

  uint8_t got = _wire->requestFrom(_addr, (uint8_t)len);
  if (got != len) return false;

  for (size_t i = 0; i < len; i++) {
    buf[i] = _wire->read();
  }
  return true;
}

bool MPU6050::begin(TwoWire& wire, uint8_t addr) {
  _wire = &wire;
  _addr = addr;

  if (!readRegs(REG_WHO_AM_I, &_id, 1)) return false;

  if (!writeReg(REG_PWR_MGMT_1, PWR_DEVICE_RESET)) return false;
  delay(100);
  if (!writeReg(REG_PWR_MGMT_1, PWR_CLKSEL_PLL_X)) return false;
  delay(10);

  bool ok = writeReg(REG_CONFIG, CONFIG_DLPF_44HZ) &&
            writeReg(REG_SMPLRT_DIV, SMPLRT_DIV_100HZ) &&
            writeReg(REG_ACCEL_CONFIG, ACCEL_RANGE_8G) &&
            writeReg(REG_INT_PIN_CFG, INT_LATCH_AND_CLEAR_ON_READ) &&
            writeReg(REG_INT_ENABLE, INT_DATA_READY_EN);

  uint8_t status;
  readRegs(REG_INT_STATUS, &status, 1);
  return ok;
}

bool MPU6050::readAccel(AccelRaw& out) {
  uint8_t buf[6];
  if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf))) return false;

  out.x = (int16_t)((buf[0] << 8) | buf[1]);
  out.y = (int16_t)((buf[2] << 8) | buf[3]);
  out.z = (int16_t)((buf[4] << 8) | buf[5]);
  return true;
}
