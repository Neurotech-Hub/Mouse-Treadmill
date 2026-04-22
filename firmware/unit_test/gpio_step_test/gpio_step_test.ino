/*
 * GPIO Step Test — bare-metal, no AccelStepper
 * =============================================
 * Directly toggles STEP and DIR with delayMicroseconds().
 * Purpose: confirm the hardware chain (Feather → TMC2209 → motor) works
 * independently of any library before debugging higher-level code.
 *
 * Open Serial Monitor at 115200 baud.
 * Commands (send with newline):
 *   g   – Go / Stop toggle
 *   +   – Faster  (step interval -50 µs, min 100 µs)
 *   -   – Slower  (step interval +50 µs, max 2000 µs)
 *   f   – Direction FORWARD
 *   r   – Direction REVERSE
 *   ?   – Print current interval and equivalent µstep/s
 *
 * Target board : Adafruit Feather M0 (ATSAMD21G18)
 * Driver       : Adafruit TMC2209 (EN active LOW)
 * Motor        : Adafruit 4411 NEMA-8
 */

static const uint8_t STEP_PIN = 11;
static const uint8_t DIR_PIN  = 10;
static const uint8_t EN_PIN   = 12;  // active LOW

// Start at 4000 µstep/s: half-period = 125 µs → interval = 250 µs total
// (HIGH for 10 µs, LOW for remainder each step)
static uint16_t stepIntervalUs = 250;  // µs between step pulses
static const uint16_t STEP_PULSE_US = 10;

static bool    going      = false;
static int8_t  dir        = 1;   // +1 = forward, -1 = reverse
static bool    dirChanged = false; // flag: insert DIR setup delay before next STEP

static void printStatus() {
  uint16_t stepsPerSec = 1000000UL / stepIntervalUs;
  Serial.print(going ? F("[GO]  ") : F("[STP] "));
  Serial.print(dir > 0 ? F("FWD ") : F("REV "));
  Serial.print(stepIntervalUs);
  Serial.print(F(" µs/step  = "));
  Serial.print(stepsPerSec);
  Serial.println(F(" µstep/s"));
}

void setup() {
  pinMode(EN_PIN,   OUTPUT);
  digitalWrite(EN_PIN, HIGH);  // disabled at boot

  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);

  pinMode(DIR_PIN,  OUTPUT);
  digitalWrite(DIR_PIN, HIGH); // default forward

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {}

  Serial.println(F("=== GPIO Step Test ==="));
  Serial.println(F("g=go/stop  +=faster  -=slower  f=fwd  r=rev  ?=status"));
  Serial.println();
  printStatus();
}

void loop() {
  // Drain all pending serial bytes so commands aren't queued behind newlines.
  while (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'g':
        going = !going;
        digitalWrite(EN_PIN, going ? LOW : HIGH);
        Serial.println(going ? F(">> GO") : F(">> STOP"));
        break;
      case '+':
        if (stepIntervalUs > 100 + 50) stepIntervalUs -= 10;
        else stepIntervalUs = 100;
        printStatus();
        break;
      case '-':
        if (stepIntervalUs < 2000) stepIntervalUs += 10;
        printStatus();
        break;
      case 'f':
        if (dir != 1) {
          dir = 1;
          digitalWrite(DIR_PIN, HIGH);
          dirChanged = true;
          Serial.println(F(">> FWD"));
        }
        break;
      case 'r':
        if (dir != -1) {
          dir = -1;
          digitalWrite(DIR_PIN, LOW);
          dirChanged = true;
          Serial.println(F(">> REV"));
        }
        break;
      case '?':
        printStatus();
        break;
    }
  }

  if (going) {
    // Allow DIR to settle before the next STEP rising edge.
    // TMC2209 latches DIR on STEP↑; a 20 µs gap is more than enough.
    if (dirChanged) {
      delayMicroseconds(20);
      dirChanged = false;
    }
    // One step pulse: HIGH for STEP_PULSE_US, then LOW for the remainder.
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepIntervalUs - STEP_PULSE_US);
  }
}
