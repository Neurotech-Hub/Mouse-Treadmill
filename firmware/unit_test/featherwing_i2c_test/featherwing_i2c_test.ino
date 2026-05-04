/*
 * Motor FeatherWing I2C Smoke Test
 * ================================
 * Verifies the I2C chain: Feather M0 → Motor FeatherWing (0x60) and,
 * if present, OLED FeatherWing (0x3C). Also steps the motor through a
 * short forward / reverse sequence to confirm coil wiring.
 *
 * Target board : Adafruit Feather M0 (ATSAMD21G18)
 * Driver       : Adafruit DC Motor + Stepper FeatherWing (PID 2927)
 * Motor        : NEMA-17 17HS19-2004S1 (M1/M2 terminals)
 *
 * Wire colour assignment (verify with multimeter before powering):
 *   M1 terminal 1 (1A) <- Black     M1 terminal 2 (2A) <- Green
 *   M2 terminal 1 (1B) <- Red       M2 terminal 2 (2B) <- Blue
 *   Black ↔ Green ≈ 1.4 Ω  (Coil A)
 *   Red   ↔ Blue  ≈ 1.4 Ω  (Coil B)
 *
 * Open Serial Monitor at 115200 baud.
 * Commands (send with newline):
 *   s   – I2C scan (prints all detected addresses)
 *   f   – Step 200 steps FORWARD at 200 sps (one full revolution, non-blocking timer loop)
 *   r   – Step 200 steps REVERSE at 200 sps
 *   x   – Release coils (motor freewheels)
 *   ?   – Print this help
 *
 * Note: uses motor->onestep() directly for timing control; motor->step() includes
 *       a library-internal blocking delay that would fight our own timing loop.
 */

#include <Wire.h>
#include <Adafruit_MotorShield.h>

Adafruit_MotorShield    AFMS;
Adafruit_StepperMotor  *motor = nullptr;

static const uint16_t STEPS_PER_REV = 200;
static const uint16_t TEST_SPEED_SPS = 200;   // steps/s during test run

static void printHelp() {
  Serial.println(F("=== Motor FeatherWing I2C Smoke Test ==="));
  Serial.println(F("  s  = I2C scan"));
  Serial.println(F("  f  = 200 steps FORWARD (1 rev at 200 sps)"));
  Serial.println(F("  r  = 200 steps REVERSE (1 rev at 200 sps)"));
  Serial.println(F("  x  = Release coils"));
  Serial.println(F("  ?  = Help"));
}

static void i2cScan() {
  Serial.println(F("--- I2C scan ---"));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("  Found device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      if (addr == 0x3C) Serial.print(F("  <- SH1107 OLED"));
      if (addr == 0x60) Serial.print(F("  <- Motor FeatherWing"));
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println(F("  No I2C devices found!"));
  Serial.println(F("--- scan done ---"));
}

static void runSteps(uint16_t n, uint8_t direction) {
  if (!motor) {
    Serial.println(F("[ERROR] Motor not initialised"));
    return;
  }
  Serial.print(direction == FORWARD ? F(">> FWD ") : F(">> REV "));
  Serial.print(n);
  Serial.println(F(" steps..."));

  uint32_t intervalUs = 1000000UL / TEST_SPEED_SPS;
  uint32_t lastUs     = micros();

  for (uint16_t i = 0; i < n; i++) {
    // Blocking wait for step interval
    while ((micros() - lastUs) < intervalUs) {}
    lastUs = micros();
    motor->onestep(direction, DOUBLE);
  }
  Serial.println(F("   done."));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {}

  Wire.begin();
  Wire.setClock(400000);

  printHelp();
  Serial.println();

  // Initialise Motor FeatherWing
  if (!AFMS.begin()) {
    Serial.println(F("[ERROR] Motor FeatherWing not found on I2C — check wiring and address"));
    while (true) {}   // halt
  }
  motor = AFMS.getStepper(STEPS_PER_REV, 1);   // 200 steps/rev, port M1/M2
  motor->release();
  Serial.println(F("[OK] Motor FeatherWing at 0x60 — coils released"));
}

void loop() {
  if (!Serial.available()) return;

  char c = (char)Serial.read();
  switch (c) {
    case 's':
      i2cScan();
      break;
    case 'f':
      runSteps(STEPS_PER_REV, FORWARD);
      break;
    case 'r':
      runSteps(STEPS_PER_REV, BACKWARD);
      break;
    case 'x':
      motor->release();
      Serial.println(F(">> Coils released"));
      break;
    case '?':
      printHelp();
      break;
    default:
      break;
  }
}
