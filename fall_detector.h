#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

class FallDetector {
 public:
  enum State : uint8_t { IDLE, FREE_FALL, SETTLE, STILLNESS };

  bool update(float g, uint32_t now) {
    switch (_state) {
      case IDLE:
        if (g < FREE_FALL_G) {
          if (++_lowCount >= FREE_FALL_MIN_SAMPLES) {
            _state = FREE_FALL;
            _since = now;
            _peak = g;
          }
        } else {
          _lowCount = 0;
        }
        break;

      case FREE_FALL:
        if (g > IMPACT_G) {
          _state = SETTLE;
          _since = now;
          _peak = g;
        } else if (now - _since > IMPACT_WINDOW_MS) {
          reset();
        }
        break;

      case SETTLE:
        if (g > _peak && now - _since < PEAK_TRACK_MS) {
          _peak = g;
        }
        if (now - _since >= SETTLE_MS) {
          _state = STILLNESS;
          _since = now;
        }
        break;

      case STILLNESS:
        if (fabsf(g - 1.0f) > MOVE_DEV_G) {
          reset();
        } else if (now - _since >= STILLNESS_MS) {
          reset();
          return true;
        }
        break;
    }
    return false;
  }

  void reset() {
    _state = IDLE;
    _lowCount = 0;
  }

  State state() const { return _state; }
  float peakG() const { return _peak; }

  static const char* stateName(State s) {
    switch (s) {
      case IDLE: return "IDLE";
      case FREE_FALL: return "FREE_FALL";
      case SETTLE: return "SETTLE";
      default: return "STILLNESS";
    }
  }

 private:
  State _state = IDLE;
  uint8_t _lowCount = 0;
  uint32_t _since = 0;
  float _peak = 0.0f;
};
