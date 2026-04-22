# Mouse Treadmill

Firmware for a motorised mouse treadmill built around Adafruit parts. A NEMA-8 bipolar stepper drives the treadmill belt; an OLED FeatherWing provides a live RPM display and three-button speed control.

---

## Hardware


| Part                                        | Adafruit PID |
| ------------------------------------------- | ------------ |
| Feather M0 Basic Proto (ATSAMD21G18, 3.3 V) | 2772         |
| SH1107 128×64 OLED FeatherWing (assembled)  | 6313         |
| TMC2209 Stepper Driver breakout             | 6121         |
| NEMA-8 bipolar stepper — 200 step, 600 mA   | 4411         |
| 12 V DC power supply                        | —            |


The FeatherWing stacks directly on the Feather M0. Wiring details and the confirmed motor coil colour map are in `[docs/phase1-wiring-and-firmware.md](docs/phase1-wiring-and-firmware.md)`.

---

## Controls


| Button     | Action                                            |
| ---------- | ------------------------------------------------- |
| **A** (D9) | Hold to ramp speed up                             |
| **B** (D6) | Tap to start / stop (motor ramps gracefully to 0) |
| **C** (D5) | Hold to ramp speed down                           |


---

## Repository layout

```
firmware/
  mouse_treadmill/
    mouse_treadmill.ino     Main sketch — ISR stepping, ramp control, OLED UI
  unit_test/
    gpio_step_test/         Bare-metal STEP/DIR toggle, no libraries (hardware chain test)
    stepper_unit_test/      AccelStepper serial speed control (pre-display smoke test)
docs/
  phase1-wiring-and-firmware.md   Full wiring reference, pin map, bring-up checklist
  phase1-smooth-stepping.md       ISR step generation design, ramp math, UI layout
```

---

## Arduino libraries required

Install all via **Arduino Library Manager**:

- `Adafruit GFX Library`
- `Adafruit SH110X`
- `AccelStepper`
- `Adafruit Zero Timer Library`

Board support: **Adafruit SAMD Boards** package (for Feather M0).

---

## Key firmware tunables

Located at the top of `firmware/mouse_treadmill/mouse_treadmill.ino`:


| Constant           | Default        | Description                                                 |
| ------------------ | -------------- | ----------------------------------------------------------- |
| `SPEED_MAX`        | 20 000 µstep/s | Hard ceiling — tune down to just above measured stall speed |
| `TARGET_RAMP_RATE` | 4 000 µstep/s² | How fast the setpoint moves while A or C is held            |
| `MOTOR_ACCEL`      | 8 000 µstep/s² | How fast the motor chases the setpoint                      |
| `ISR_PERIOD_TICKS` | 480            | TC3 compare value (480 × 1/48 MHz ≈ 10 µs)                  |
| `MICROSTEPS`       | 8              | Must match MS1/MS2 hardware strap on TMC2209                |


---

## How jitter-free stepping works

Step pulses are generated from a SAMD21 TC3 hardware timer ISR that fires every ~~10 µs, completely decoupled from the main loop. OLED I2C redraws (~~7–25 ms) therefore cannot stutter the motor. See `[docs/phase1-smooth-stepping.md](docs/phase1-smooth-stepping.md)` for the full design.

---

## Bring-up sequence

1. Flash `mouse_treadmill.ino` with the motor **unplugged**. Confirm the OLED splash appears and buttons change the `[RUN]`/`[STP]` state.
2. Power down. Wire the motor (see wiring doc). Set the TMC2209 Vref trim pot **fully counter-clockwise**.
3. Apply +12 V to the driver `+` terminal; power the Feather over USB.
4. Tap B to start, hold A for a slow crawl. Increase the Vref pot slowly until motion is smooth.
5. Hold A until the motor stalls under load — note that speed as the practical `SPEED_MAX`.

