// 8HY2001-10 + TMC2209 — AccelStepper Serial Speed Control
// Library: AccelStepper by Mike McCauley (Install via Library Manager)
//
// Send a number in us/step (e.g., 250) via Serial Monitor to set speed.
// Send 0 to stop.

#include <AccelStepper.h>

#define EN_PIN   12    // LOW = driver enabled
#define STEP_PIN 11
#define DIR_PIN  10

// DRIVER mode: AccelStepper handles STEP + DIR signals
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

bool isRunning = false;

void setup() {
  Serial.begin(115200);

  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);   // Start disabled

  // Set a safe max speed ceiling for 8HY2001-10
  // At 16x microsteps: 200 steps/rev * 16 = 3200 steps/rev
  // 50 us/step = 20,000 steps/sec max (absolute ceiling)
  stepper.setMaxSpeed(20000.0);
  stepper.setSpeed(0);

  Serial.println("8HY2001-10 + TMC2209 AccelStepper Speed Controller");
  Serial.println("Send us/step (e.g., 250) to run. Send 0 to stop.");
  Serial.println("Min: 50 us/step | Max: ~5000 us/step");
}

void loop() {
  // --- Handle Serial input ---
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    unsigned long usPerStep = input.toInt();

    if (usPerStep == 0) {
      // Stop motor
      stepper.setSpeed(0);
      digitalWrite(EN_PIN, HIGH);   // Disable driver
      isRunning = false;
      Serial.println("[STOPPED] Driver disabled.");

    } else if (usPerStep < 10) {
      Serial.println("[ERROR] Too fast! Minimum is 50 us/step for 8HY2001-10.");

    } else {
      // Convert us/step → steps/second
      float speedStepsPerSec = 1000000.0 / (float)usPerStep;

      stepper.setSpeed(speedStepsPerSec);
      digitalWrite(EN_PIN, LOW);    // Enable driver
      isRunning = true;

      Serial.print("[RUNNING] ");
      Serial.print(usPerStep);
      Serial.print(" us/step → ");
      Serial.print(speedStepsPerSec, 1);
      Serial.println(" steps/sec");
    }
  }

  // --- Run motor at constant speed (non-blocking) ---
  if (isRunning) {
    stepper.runSpeed();
  }
}