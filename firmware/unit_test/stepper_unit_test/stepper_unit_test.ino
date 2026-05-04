/*
 * Motor FeatherWing Serial Speed-Control Test
 * ============================================
 * Allows interactive speed control of the NEMA-17 stepper via the
 * Adafruit DC Motor + Stepper FeatherWing (I2C 0x60) from the Serial Monitor.
 * Use this to characterise maximum achievable step rate, stall speed, and
 * coil direction before integrating into the main treadmill firmware.
 *
 * Target board : Adafruit Feather M0 (ATSAMD21G18)
 * Driver       : Adafruit DC Motor + Stepper FeatherWing (PID 2927)
 * Motor        : NEMA-17 17HS19-2004S1, M1/M2 terminals
 *
 * Open Serial Monitor at 115200 baud (send with newline).
 * Commands:
 *   <number>   – Set target speed in steps/s (e.g., "200"). 0 = stop.
 *   f          – Direction FORWARD
 *   r          – Direction REVERSE
 *   d          – Cycle coil drive mode: SINGLE → DOUBLE → INTERLEAVE → MICROSTEP
 *   x          – Release coils (motor freewheels)
 *   ?          – Print current status
 *
 * Note: uses motor->onestep() (non-blocking, no internal library delay) so that
 *       step timing is controlled entirely by the main loop's interval check.
 */

#include <Wire.h>
#include <Adafruit_MotorShield.h>

Adafruit_MotorShield   AFMS;
Adafruit_StepperMotor *motor = nullptr;

static const uint16_t STEPS_PER_REV = 200;

static uint16_t targetSPS  = 0;
static uint8_t  dir        = FORWARD;
static uint8_t  stepMode   = DOUBLE;
static bool     isRunning  = false;
static uint32_t lastStepUs = 0;

static const char *modeName(uint8_t m) {
  switch (m) {
    case SINGLE:     return "SINGLE";
    case DOUBLE:     return "DOUBLE";
    case INTERLEAVE: return "INTERLEAVE";
    case MICROSTEP:  return "MICROSTEP";
    default:         return "?";
  }
}

// Returns how many onestep() calls equal one full mechanical step.
// SINGLE/DOUBLE advance a full step each call; INTERLEAVE = half-step (2×);
// MICROSTEP = 1/16 step (16×). Used to convert sps → true RPM.
static uint8_t substepsForMode(uint8_t m) {
  switch (m) {
    case INTERLEAVE: return 2;
    case MICROSTEP:  return 16;
    default:         return 1;   // SINGLE, DOUBLE
  }
}

static void printStatus() {
  Serial.print(isRunning ? F("[GO]  ") : F("[STP] "));
  Serial.print(dir == FORWARD ? F("FWD ") : F("REV "));
  Serial.print(targetSPS);
  Serial.print(F(" stp/s  mode="));
  Serial.print(modeName(stepMode));
  float rpm = (float)targetSPS / ((float)STEPS_PER_REV * substepsForMode(stepMode)) * 60.0f;
  Serial.print(F("  RPM="));
  Serial.println(rpm, 1);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {}

  Wire.begin();
  Wire.setClock(400000);

  Serial.println(F("=== Motor FeatherWing Speed-Control Test ==="));
  Serial.println(F("  <number> = set steps/s  |  0 = stop"));
  Serial.println(F("  f=fwd  r=rev  d=cycle mode  x=release  ?=status"));
  Serial.println();

  if (!AFMS.begin(2000)) {   // 2 kHz PWM — reduces coil whine vs default 1.6 kHz
    Serial.println(F("[ERROR] Motor FeatherWing not found — check wiring"));
    while (true) {}
  }
  motor = AFMS.getStepper(STEPS_PER_REV, 1);
  motor->release();
  Serial.println(F("[OK] Motor FeatherWing ready"));
  printStatus();
}

void loop() {
  // --- Serial command handling ---
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() == 0) {
      // empty line — ignore
    } else if (input == "f") {
      dir = FORWARD;
      Serial.println(F(">> FORWARD"));
    } else if (input == "r") {
      dir = BACKWARD;
      Serial.println(F(">> REVERSE"));
    } else if (input == "d") {
      // Cycle through step modes
      switch (stepMode) {
        case SINGLE:     stepMode = DOUBLE;      break;
        case DOUBLE:     stepMode = INTERLEAVE;  break;
        case INTERLEAVE: stepMode = MICROSTEP;   break;
        default:         stepMode = SINGLE;      break;
      }
      Serial.print(F(">> Mode: "));
      Serial.println(modeName(stepMode));
    } else if (input == "x") {
      targetSPS = 0;
      isRunning = false;
      motor->release();
      Serial.println(F(">> Coils released"));
    } else if (input == "?") {
      printStatus();
    } else {
      // Try to parse as a number (steps/s)
      long val = input.toInt();
      if (val < 0) {
        Serial.println(F("[ERROR] Negative speeds not supported — use 'r' for reverse"));
      } else if (val == 0) {
        targetSPS = 0;
        isRunning = false;
        motor->release();
        Serial.println(F("[STOPPED] Coils released"));
      } else if (val > 1000) {
        Serial.println(F("[ERROR] Limit is 1000 stp/s — I2C overhead above this causes missed steps"));
      } else {
        targetSPS = (uint16_t)val;
        isRunning = true;
        lastStepUs = micros();
        Serial.print(F("[RUNNING] "));
        Serial.print(targetSPS);
        Serial.print(F(" stp/s  "));
        Serial.print((float)targetSPS / ((float)STEPS_PER_REV * substepsForMode(stepMode)) * 60.0f, 1);
        Serial.println(F(" RPM"));
      }
    }
  }

  // --- Step engine ---
  if (isRunning && targetSPS > 0) {
    uint32_t intervalUs = 1000000UL / targetSPS;
    uint32_t nowUs      = micros();
    if ((nowUs - lastStepUs) >= intervalUs) {
      motor->onestep(dir, stepMode);
      lastStepUs += intervalUs;   // drift-correcting: keeps average rate exact
    }
  }
}
