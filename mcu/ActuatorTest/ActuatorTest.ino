#include <AccelStepper.h>

//Initialize pins
const int stepPin = 3;
const int dirPin = 4;
const int sensorPin = 7;

AccelStepper stepper(1, stepPin, dirPin);

const long MAX_STEPS = 3100; 

void setup() {
  Serial.begin(9600);

  //Sensor pin setup
  pinMode(sensorPin, INPUT_PULLUP);

  //Set motor speed and acceleration
  stepper.setMaxSpeed(800); //Max steps per second
  stepper.setAcceleration(400); //Steps per second squared
  
  /*---Homing sequence---
  (-XXX) moves actuator left toward sensor and decides speed, (XXX) moves actuator right */
  stepper.setSpeed(-300); 

  //Move when sensor reads 1
  while (digitalRead(sensorPin) == 1) {
    stepper.runSpeed();
  }

  //Stop when sensor reads 0
  stepper.setSpeed(0);
  stepper.setCurrentPosition(0);
  Serial.println("Homing Sequence Complete");
}

void loop() {
  
  //Read user input
  if (Serial.available() > 0) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();

    //If user input is "Left," actuator moves to left until home is reached
    if (userInput.equalsIgnoreCase("Left")) {
      stepper.moveTo(0);
      Serial.println("Actuator Moving Left");

    }

    //If user input is "Right," actuator moves right until specified limit reached
    else if (userInput.equalsIgnoreCase("Right")) {
      stepper.moveTo(MAX_STEPS);
      Serial.println("Actuator Moving Right");

    }

    //If user input is "Stop," actuator stops
    else if (userInput.equalsIgnoreCase("Stop")) {
      stepper.stop();
      Serial.println("Actuator Stopped");

    }
  }

  stepper.run();
}