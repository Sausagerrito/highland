#include <AccelStepper.h>

// Initialize pins
const int stepPin = 3;
const int dirPin = 4;
const int sensorPin = 7;

AccelStepper stepper(1, stepPin, dirPin);

const long MAX_STEPS = 3100; 

// Variable to track sensor state
int lastSensorState = -1; 

void setup() {
  Serial.begin(9600);

  // Sensor pin setup
  pinMode(sensorPin, INPUT_PULLUP);

  stepper.setPinsInverted(true, false, false);

  // Set motor speed and acceleration
  stepper.setMaxSpeed(800);     // Max steps per second
  stepper.setAcceleration(400); // Steps per second squared
  
  /*Homing sequence
  (-300) moves actuator Left toward the sensor */
  stepper.setSpeed(-300); 

  // Move while sensor reads 0
  while (digitalRead(sensorPin) == 0) {
    stepper.runSpeed();
  }

  //Stop when sensor reads 1 and set as absolute left (0)
  stepper.setSpeed(0);
  stepper.setCurrentPosition(0);
  Serial.println("Homing Sequence Complete");
}

void loop() {
  
  //Sensor print
  int currentSensorState = digitalRead(sensorPin);
  
  //Only print if state has changed
  if (currentSensorState != lastSensorState) {
    Serial.print("Sensor Data: ");
    Serial.println(currentSensorState);
    lastSensorState = currentSensorState; 
  }

  // If sensor is 1, stop
  if (currentSensorState == 1 && stepper.distanceToGo() < 0) {
    stepper.setCurrentPosition(0); 
    stepper.moveTo(0); // Halts movement
  }

  if (Serial.available() > 0) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();

    //"Left" moves toward position 0
    if (userInput.equalsIgnoreCase("Left")) {
      if (currentSensorState == 0) {
        stepper.moveTo(0);
        Serial.println("Actuator Moving Left");
      } else {
        Serial.println("At Left limit. Cannot move further Left.");
      }
    }

    //"Right" moves away from sensor to MAX_STEPS
    else if (userInput.equalsIgnoreCase("Right")) {
      stepper.moveTo(MAX_STEPS);
      Serial.println("Actuator Moving Right");
    }

    //Stop command
    else if (userInput.equalsIgnoreCase("Stop")) {
      stepper.stop(); 
      Serial.println("Actuator Stopped");
    }
  }

  stepper.run();
}