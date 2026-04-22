/*
 * Mouse-Treadmill Phase 1 - Smooth Speed Control
 * ================================================
 *
 * Target board: Adafruit Feather M0 Basic Proto (ATSAMD21G18, 3.3 V logic)
 * Display:      Adafruit SH1107 128x64 OLED FeatherWing (PID 6313)
 * Driver:       Adafruit TMC2209 Stepper Driver breakout (PID 6121)
 * Motor:        Adafruit 4411 NEMA-8 bipolar stepper (200 step, 600 mA/phase)
 *
 * Controls (buttons on the OLED wing):
 *   A (D9)  -> Hold to ramp speed UP
 *   B (D6)  -> Tap to Start / Stop (graceful ramp to 0 on stop)
 *   C (D5)  -> Hold to ramp speed DOWN
 *
 * Libraries (install via Arduino Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit SH110X
 *   - AccelStepper
 *   - Adafruit Zero Timer Library  <-- NEW: needed for jitter-free ISR stepping
 *
 * ---------------------------------------------------------------------------
 * HOW JITTER-FREE STEPPING WORKS
 * ---------------------------------------------------------------------------
 *  Step pulses are generated from a SAMD21 TC3 hardware timer ISR firing
 *  every ~10 us (100 kHz). AccelStepper::runSpeed() runs inside the ISR and
 *  is completely decoupled from the main loop — OLED I2C redraws, which can
 *  take 7-25 ms, no longer starve the pulse train.
 *
 *  The main loop only manages:
 *    - Button debounce and userTarget ramping
 *    - Soft speed ramp (currentSpeed chases userTarget)
 *    - Calling stepper.setSpeed() under noInterrupts() to avoid race on the
 *      AccelStepper float state that the ISR is also reading.
 *    - 10 Hz OLED refresh
 *
 * ---------------------------------------------------------------------------
 * TUNING KNOBS (change these to adjust feel)
 * ---------------------------------------------------------------------------
 *   SPEED_MAX        Hard ceiling in usteps/s. Motor stall speed is well below
 *                    this; find it experimentally and tune down if needed.
 *   TARGET_RAMP_RATE How fast the speed setpoint moves while A or C is held,
 *                    in usteps/s per second.
 *   MOTOR_ACCEL      How fast the actual motor speed chases the setpoint, in
 *                    usteps/s^2. Set higher than TARGET_RAMP_RATE so the motor
 *                    keeps up with held-button changes without lag.
 *   ISR_PERIOD_TICKS Timer compare value. 480 ticks at 48 MHz GCLK = ~10 us.
 *                    Raise for slightly lower CPU load; 480 is comfortable.
 *
 * ---------------------------------------------------------------------------
 * BRING-UP CHECKLIST - do this before powering the 12 V motor bus:
 * ---------------------------------------------------------------------------
 *  1) WIRING SANITY (multimeter, motor disconnected from driver):
 *       - Black  <-> Red     ~= 7 ohm  (Coil A)
 *       - Green  <-> Yellow  ~= 7 ohm  (Coil B)
 *       - any Coil-A wire    <-> any Coil-B wire = open circuit
 *     Driver connections (from Adafruit 4411 product page):
 *       OA1 (1A) <- Black   OA2 (2A) <- Red
 *       OB1 (1B) <- Green   OB2 (2B) <- Yellow
 *
 *  2) POWER - two separate supplies required:
 *       - Feather 3.3 V   -> TMC2209 VDD header pin (logic supply)
 *       - +12 V PSU       -> TMC2209 '+' terminal block (motor supply)
 *       - Feather GND, TMC2209 GND header, TMC2209 '-' terminal, PSU GND
 *         must all share a common ground.
 *       - Place 100 uF electrolytic across '+' and '-' terminal block pins.
 *
 *  3) Vref TRIM POT:
 *       - Start fully counter-clockwise (minimum current).
 *       - Flash this sketch, press B to start, hold A for a slow crawl.
 *       - Increase pot slowly until motor runs smoothly without the driver
 *         IC or motor getting hot. Target ~0.6 A RMS for the 4411.
 *
 *  4) MICROSTEP STRAP:
 *       - MS1 and MS2 left open (or tied to GND) = 1/8 microstep.
 *         This sketch assumes MICROSTEPS = 8 (1600 usteps/rev).
 *
 *  5) DIRECTION:
 *       - Runs forward only in Phase 1. If the treadmill belt goes the
 *         wrong way, power down and swap JUST OA1<->OA2 (or OB1<->OB2).
 *         Never cross wires between the two coils.
 * ---------------------------------------------------------------------------
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <AccelStepper.h>
#include <Adafruit_ZeroTimer.h>

// ======================== Pin map ========================================
static const uint8_t STEP_PIN = 11;
static const uint8_t DIR_PIN  = 10;
static const uint8_t EN_PIN   = 12;   // active LOW on TMC2209

// SH1107 FeatherWing buttons — active LOW, INPUT_PULLUP
// (same on all M0 / 32u4 / M4 Feathers per Adafruit's official example)
static const uint8_t BTN_A = 9;   // Hold = ramp speed up
static const uint8_t BTN_B = 6;   // Tap  = Start / Stop
static const uint8_t BTN_C = 5;   // Hold = ramp speed down

// ======================== Motion & ramp constants ========================
static const uint16_t STEPS_PER_REV  = 200;
static const uint8_t  MICROSTEPS     = 8;        // MS1/MS2 strapped for 1/8

static const float    SPEED_MAX      = 20000.0f; // usteps/s ceiling (50 us/step)
static const float    SPEED_MIN      = 0.0f;

// Hold-to-ramp: how fast the user's speed setpoint moves (usteps/s per second)
static const float    TARGET_RAMP_RATE = 4000.0f;

// Soft motor ramp: how fast currentSpeed chases userTarget (usteps/s^2).
// Keep > TARGET_RAMP_RATE so the motor tracks held-button changes without lag.
static const float    MOTOR_ACCEL    = 8000.0f;

// Timer ISR period. 480 ticks * (1/48 MHz) = 10 us -> 100 kHz ISR.
// AccelStepper::runSpeed() is called each tick; it emits a STEP pulse only
// when the elapsed time since the last step equals the current step interval.
static const uint16_t ISR_PERIOD_TICKS = 480;

// TMC2209 EN polarity
static const uint8_t DRIVER_ENABLE_LEVEL  = LOW;
static const uint8_t DRIVER_DISABLE_LEVEL = HIGH;

// ======================== Peripherals ====================================
// SH1107 internal buffer is 64 wide x 128 tall; setRotation(1) = 128 x 64
Adafruit_SH1107    display = Adafruit_SH1107(64, 128, &Wire);
AccelStepper       stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
Adafruit_ZeroTimer zt      = Adafruit_ZeroTimer(3);

// ======================== Button debounce ================================
struct Button {
  uint8_t  pin;
  uint8_t  lastReading;
  bool     pressedStable;
  uint32_t lastChangeMs;
};

static Button btnA = { BTN_A, HIGH, false, 0 };
static Button btnB = { BTN_B, HIGH, false, 0 };
static Button btnC = { BTN_C, HIGH, false, 0 };

static const uint16_t DEBOUNCE_MS = 20;

// ======================== Timer ISR ======================================
// TC3_Handler dispatches to the ZeroTimer library, which then calls stepperISR.
void TC3_Handler() {
  Adafruit_ZeroTimer::timerHandler(3);
}

void stepperISR() {
  stepper.runSpeed();   // cheap: checks micros() then toggles STEP if due
}

// Update debounce state. Returns the stable pressed level (true = held down).
static bool updateButton(Button &b) {
  uint8_t  raw = digitalRead(b.pin);
  uint32_t now = millis();
  if (raw != b.lastReading) {
    b.lastReading  = raw;
    b.lastChangeMs = now;
  }
  if ((now - b.lastChangeMs) >= DEBOUNCE_MS) {
    b.pressedStable = (raw == LOW);
  }
  return b.pressedStable;
}

// Returns true once on each stable press-down edge.
static bool pressedEdge(Button &b) {
  bool wasPressed = b.pressedStable;
  bool isNow      = updateButton(b);
  return (!wasPressed && isNow);
}

// Returns true while the button is stably held down.
static bool isHeld(Button &b) {
  updateButton(b);
  return b.pressedStable;
}

// ======================== Control state ==================================
static float    userTarget   = 0.0f;  // setpoint commanded by held A/C
static float    currentSpeed = 0.0f;  // actual speed sent to AccelStepper
static bool     running      = false;

static uint32_t lastLoopUs   = 0;     // for dt calculation
static uint32_t lastDisplayMs = 0;

static const uint16_t DISPLAY_PERIOD_MS = 100;  // 10 Hz OLED refresh

static inline void setDriverEnabled(bool en) {
  digitalWrite(EN_PIN, en ? DRIVER_ENABLE_LEVEL : DRIVER_DISABLE_LEVEL);
}

// Push a new speed to AccelStepper under interrupt lock (ISR reads same state).
static inline void applySpeed(float spd) {
  noInterrupts();
  stepper.setSpeed(spd);
  interrupts();
}

// ======================== OLED UI ========================================
static void drawSplash() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Mouse Treadmill"));
  display.setCursor(0, 12);
  display.print(F("Phase 1  ISR stepping"));
  display.setCursor(0, 28);
  display.print(F("B  = start / stop"));
  display.setCursor(0, 38);
  display.print(F("Hold A = ramp up"));
  display.setCursor(0, 48);
  display.print(F("Hold C = ramp down"));
  display.display();
}

static void drawUI() {
  // Compute RPM from currentSpeed (which is what the motor is actually doing).
  float rpm = (currentSpeed / (float)MICROSTEPS) /
              (float)STEPS_PER_REV * 60.0f;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // --- Row 0: title + state badge ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Mouse Treadmill"));
  display.setCursor(92, 0);
  display.print(running ? F("[RUN]") : F("[STP]"));

  display.drawFastHLine(0, 9, 128, SH110X_WHITE);

  // --- Rows 12-39: large RPM readout (text size 3 = 18 px tall chars) ---
  // Avoid float in snprintf/printf: SAMD21 Arduino does not support %f
  // without a linker flag. Split into integer + one decimal digit instead.
  display.setTextSize(3);
  char rpmBuf[8];
  float rpmClamped = rpm < 0.0f ? 0.0f : rpm;
  int   rpmInt     = (int)rpmClamped;
  int   rpmFrac    = (int)((rpmClamped - rpmInt) * 10.0f + 0.5f);
  if (rpmFrac >= 10) { rpmInt++; rpmFrac = 0; }
  snprintf(rpmBuf, sizeof(rpmBuf), "%4d.%1d", rpmInt, rpmFrac);
  // Measure pixel width of the string so we can right-align it
  int16_t  tx, ty;
  uint16_t tw, th;
  display.getTextBounds(rpmBuf, 0, 0, &tx, &ty, &tw, &th);
  display.setCursor(max(0, 128 - (int)tw - 4), 12);
  display.print(rpmBuf);

  // RPM unit label at text size 1, baseline-aligned beside big number
  display.setTextSize(1);
  display.setCursor(0, 32);
  display.print(F("RPM"));

  display.drawFastHLine(0, 42, 128, SH110X_WHITE);

  // --- Row 44: raw usteps/s ---
  display.setCursor(0, 44);
  display.print((long)currentSpeed);
  display.print(F(" st/s"));

  // --- Row 54: button hint ---
  display.setCursor(0, 54);
  display.print(F("A=+  C=-  B=run  1/"));
  display.print(MICROSTEPS);

  display.display();
}

// ======================== Arduino entry points ============================
void setup() {
  // Disable driver outputs before touching anything.
  pinMode(EN_PIN,   OUTPUT);
  setDriverEnabled(false);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN,  LOW);

  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);

  // OLED init (I2C at 400 kHz so frame writes are as fast as possible)
  display.begin(0x3C, true);
  Wire.setClock(400000);
  display.setRotation(1);   // 128 wide x 64 tall
  drawSplash();

  // AccelStepper constant-speed mode — acceleration is handled in software
  // by the ramp math below; runSpeed() just needs setMaxSpeed() as a guard.
  stepper.setMaxSpeed(SPEED_MAX);
  stepper.setSpeed(0.0f);

  // --- Hardware timer TC3: fires stepperISR every ISR_PERIOD_TICKS ticks ---
  // Clock source: GCLK0 = 48 MHz, prescaler DIV1 => 1 tick = ~20.83 ns
  // 480 ticks * 20.83 ns = 10 us => 100 kHz ISR rate.
  zt.configure(TC_CLOCK_PRESCALER_DIV1,
               TC_COUNTER_SIZE_16BIT,
               TC_WAVE_GENERATION_MATCH_PWM);
  zt.setCompare(0, ISR_PERIOD_TICKS);
  zt.setCallback(true, TC_CALLBACK_CC_CHANNEL0, stepperISR);
  zt.enable(true);

  delay(800);   // hold splash briefly; timer is already running but speed=0
  lastLoopUs = micros();
}

void loop() {
  // ---- 1) Delta time for smooth, display-rate-independent ramps ----------
  uint32_t nowUs = micros();
  float    dt    = (float)(nowUs - lastLoopUs) * 1.0e-6f;
  lastLoopUs = nowUs;
  // Guard against unreasonably large dt on the first pass or after wrap
  if (dt > 0.1f) dt = 0.1f;

  // ---- 2) Button A/C: ramp the user setpoint while held ------------------
  // updateButton() is called inside isHeld(); no separate call needed.
  if (isHeld(btnA)) {
    userTarget += TARGET_RAMP_RATE * dt;
    if (userTarget > SPEED_MAX) userTarget = SPEED_MAX;
  }
  if (isHeld(btnC)) {
    userTarget -= TARGET_RAMP_RATE * dt;
    if (userTarget < SPEED_MIN) userTarget = SPEED_MIN;
  }

  // ---- 3) Button B: start / stop on press edge ---------------------------
  if (pressedEdge(btnB)) {
    running = !running;
    if (running) {
      setDriverEnabled(true);   // enable driver before ramp starts
    }
    // On stop: let the ramp math bring currentSpeed to 0; driver disabled there
  }

  // ---- 4) Soft motor ramp (currentSpeed chases effectiveTarget) ----------
  float effectiveTarget = running ? userTarget : 0.0f;
  float delta           = MOTOR_ACCEL * dt;

  if (currentSpeed < effectiveTarget) {
    currentSpeed += delta;
    if (currentSpeed > effectiveTarget) currentSpeed = effectiveTarget;
  } else if (currentSpeed > effectiveTarget) {
    currentSpeed -= delta;
    if (currentSpeed < effectiveTarget) currentSpeed = effectiveTarget;
  }

  // Disable driver only once the motor has fully ramped to 0.
  if (!running && currentSpeed <= 0.0f) {
    currentSpeed = 0.0f;
    setDriverEnabled(false);
  }

  // ---- 5) Push new speed to AccelStepper (under interrupt lock) ----------
  applySpeed(currentSpeed);

  // ---- 6) Throttled OLED redraw (10 Hz) ----------------------------------
  uint32_t nowMs = millis();
  if ((nowMs - lastDisplayMs) >= DISPLAY_PERIOD_MS) {
    lastDisplayMs = nowMs;
    drawUI();
  }
}
