/*
 * gpio_step_test — RETIRED
 * ========================
 * This sketch directly toggled STEP/DIR pins for the TMC2209 stepper driver.
 * That driver has been replaced by the Adafruit DC Motor + Stepper FeatherWing
 * (I2C-based, PID 2927), which requires no STEP/DIR GPIO.
 *
 * Use the replacement test sketch instead:
 *   firmware/unit_test/featherwing_i2c_test/featherwing_i2c_test.ino
 *     -- I2C bus scan, forward/reverse single-step test
 *
 *   firmware/unit_test/stepper_unit_test/stepper_unit_test.ino
 *     -- Serial speed-control test via Adafruit Motor Shield V2 library
 *
 * This file is kept for git history only and will not compile meaningfully
 * against the new hardware configuration.
 */

void setup() {}
void loop()  {}
