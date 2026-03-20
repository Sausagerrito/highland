#include <AccelStepper.h>

const float stepsPerRevolution = 200.0;
const float microstepping = 1.0;      
const float mmPerRevolution = 8.0;  

// Speed settings
const float targetSpeed_mm_s = 190.5;   // Desired speed in mm/s
const float homingSpeed_mm_s = 190.5;   // Desired speed for homing

// mm/s to step/s
const float stepsPerMM = (stepsPerRevolution * microstepping) / mmPerRevolution;
const float maxSpeedSteps = targetSpeed_mm_s * stepsPerMM;
const float homingSpeedSteps = homingSpeed_mm_s * stepsPerMM;

// Initialize pins
const int stepPin = 3;
const int dirPin = 4;
const int sensorPin = 7;

AccelStepper stepper(1, stepPin, dirPin);

const long maxSteps = 6100; 

// Variable to track sensor state
int lastSensorState = -1; 

void setup() {
  Serial.begin(9600);

  // Sensor pin setup
  pinMode(sensorPin, INPUT_PULLUP);

  // Library HIGH/LOW
  stepper.setPinsInverted(true, false, false);

  // Set maximum speed limit
  stepper.setMaxSpeed(maxSpeedSteps);     
    
  // Homing sequence
  stepper.setSpeed(-homingSpeedSteps); // Move Left toward sensor at homing speed

  // Move while sensor reads 0
  while (digitalRead(sensorPin) == 0) {
    stepper.runSpeed();
  }

  // Stop when sensor reads 1 and set as absolute left (0)
  stepper.setSpeed(0);
  stepper.setCurrentPosition(0);
  stepper.moveTo(maxSteps);
  stepper.setSpeed(maxSpeedSteps); // Set raw speed forward
}

void loop() {
  
  // Sensor print
  int currentSensorState = digitalRead(sensorPin);
  
  // Only print if state has changed
  if (currentSensorState != lastSensorState) {
    lastSensorState = currentSensorState; 
  }

  // If sensor is 1 while moving left, stop instantly
  if (currentSensorState == 1 && stepper.speed() < 0) {
    stepper.setSpeed(0);
    stepper.setCurrentPosition(0); // Resets distanceToGo to 0
  }

  if (Serial.available() > 0) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();

    // "Left" moves toward position 0
    if (userInput.equalsIgnoreCase("Left")) {
      if (currentSensorState == 0) {
        stepper.moveTo(0);
        stepper.setSpeed(-maxSpeedSteps); // Set raw speed backward
      }
    }

    // "Right" moves away from sensor to maxSteps
    else if (userInput.equalsIgnoreCase("Right")) {
      stepper.moveTo(maxSteps);
      stepper.setSpeed(maxSpeedSteps); // Set raw speed forward
    }
  }

  if (stepper.distanceToGo() != 0) {
    stepper.runSpeed(); 
  }
}