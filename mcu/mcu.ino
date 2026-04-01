#include <QuickPID.h>
#include <AccelStepper.h>

// Pins
const int heatOutput = 11;
const int stepPin = 3;
const int dirPin = 4;
const int sensorPin = 7;

// --------------------------------------------------
// Actuator Variables
// --------------------------------------------------

// Linear actuator variables
const float stepsPerRevolution = 200.0;
const float microstepping = 1.0;
const float mmPerRevolution = 8.0;

// Linear actuator speed settings
const float targetSpeed_mm_s = 190.5; // Desired speed in mm/s
const float homingSpeed_mm_s = 190.5; // Desired homing speed

// mm/s to step/s
const float stepsPerMM = (stepsPerRevolution * microstepping) / mmPerRevolution;
const float maxSpeedSteps = targetSpeed_mm_s * stepsPerMM;
const float homingSpeedSteps = homingSpeed_mm_s * stepsPerMM;

AccelStepper stepper(1, stepPin, dirPin);

// Max travel steps for required clearance
const long maxSteps = 6100;

// Variable to track sensor state
int lastSensorState = -1;

// --------------------------------------------------
// Heater and PID Variables
// --------------------------------------------------

// PID Params
float Kp = 5.0;
float Ki = 0.15;
float Kd = 40.0;

// Temp Control
float t_shield = 0.0; 
float t_hot = 0.0;    
float t_cold = 0.0;   
float setpoint = 1400.0;
float testDuration = 60.0; 
float controllerOutput = 0.0;

// Fixed sample time
QuickPID myPID(&t_shield, &controllerOutput,
&setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

// Output smoothing
float smoothedOutput = 0.0;
const float smoothingFactor = 0.1;
int heatPWM = 0;

// Champion torch variables
const float championMaxBTU = 775.0; // per minute
const float championPropaneFlow = 8.0;
const float championOxygenFlow = 40.0;
const float btuToC = 0.25;
const float maxHeatingRate = championMaxBTU * btuToC;

// Champion flame simulation
const int centerFireMaxPWM = 100; // Center has 6 jets
const int outerFireThreshold = 101; // Outer has 30 jets

float lastHeatingInput = 0.0;

// --------------------------------------------------
// Timing and State Variables
// --------------------------------------------------

// State variables
bool idleActive = true;
bool heatActive = false;
bool setpointReached = false;
bool coolingActive = false;

// Debug variables
unsigned long lastSampleTime = 0;
unsigned long lastDebugTime = 0;
const unsigned long sampleTime = 500;
const unsigned long debugInterval = 250;

// --------------------------------------------------
// Setup and Loop
// --------------------------------------------------

void setup() {
  pinMode(heatOutput, OUTPUT);

  Serial.begin(9600);

  // Actuator sensor pin setup
  pinMode(sensorPin, INPUT_PULLUP);

  // Actuator library HIGH/LOW
  stepper.setPinsInverted(true, false, false);

  // Set actuator maximum speed limit
  stepper.setMaxSpeed(maxSpeedSteps);

  // Actuator homing sequence
  delay(1050); // Ensures smooth homing sequence
  stepper.setSpeed(-homingSpeedSteps); // Move left toward sensor at homing speed

  // Move actuator while sensor reads 0
  while (digitalRead(sensorPin) == 0) {
    stepper.runSpeed();
  }

  // Step when actuator sensor reads 1 (triggered) and set as absolute left (0)
  stepper.setSpeed(0);
  stepper.setCurrentPosition(0);

  myPID.SetProportionalMode(QuickPID::pMode::pOnError);
  myPID.SetDerivativeMode(QuickPID::dMode::dOnError);
  myPID.SetOutputLimits(0, 255);
  myPID.SetSampleTimeUs(sampleTime * 1000);
  myPID.SetMode(QuickPID::Control::manual);
  
  // Start in idle state
  writeHeatPWM(0);
}

void loop() {
  // Command Parsing for Rust Integration
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.replace("\r", "");
    command.trim();

    if (command == "CMD:START" || command == "CMD:START_SKIP") {
      if (!heatActive) {
        startHeating();
      }
    }
    else if (command == "CMD:STOP") {
      if (heatActive) {
        stopHeating();
      }
    }
    else if (command == "CMD:HOME") {
      moveActuatorToIdle();
    }
    else if (command.startsWith("SET_TEMP:")) {
      String valueStr = command.substring(9);
      valueStr.trim();
      float newSetPoint = valueStr.toFloat();
      if (newSetPoint > 0.0 && newSetPoint <= 1450.0) {
        setpoint = newSetPoint;
        myPID.Reset();
      }
    }
    else if (command.startsWith("SET_TIME:")) {
      String valueStr = command.substring(9);
      valueStr.trim();
      testDuration = valueStr.toFloat();
    }
  }

  unsigned long now = millis();
  updateActuator();
  
  // Run temp control at fixed intervals
  if (now - lastSampleTime >= sampleTime) {
    lastSampleTime = now;
    
    // Read temp
    t_shield = readTemperature();
    
    // Telemetry output for Rust GUI
    Serial.print("T_SHIELD=");
    Serial.println(t_shield, 2);

    Serial.print("T_HOT=");
    Serial.println(t_hot, 2);

    Serial.print("T_COLD=");  
    Serial.println(t_cold, 2);
    
    // Check setpoint reached
    if (heatActive && !setpointReached && t_shield >= setpoint - 2.0) {
      setpointReached = true;
      moveActuatorAway();
    }
    
    // Safety limit
    if (t_shield > 1451.0) {
      emergencyStop();
      return;
    }
    
    // Check if cooling should transition to idle
    if (coolingActive && t_shield <= 0.1) {
      coolingActive = false;
      idleActive = true;
    }
    
    // State machine
    if (heatActive) {
      myPID.Compute();

      // Braking to prevent overshoot
      float finalOutput = controllerOutput;
      if (t_shield > (setpoint - 30.0) && t_shield < setpoint) {
        finalOutput = constrain(controllerOutput, 0, 100); // Cap power in final approach
      }
      if (t_shield >= setpoint) {
        finalOutput = 0; // Cut power once target reached
      }
      writeHeatPWM((int)finalOutput);
    }
      
    else if (idleActive) {
      // No heating in idle
      writeHeatPWM(0);
    }
    else if (coolingActive) {
      // No heating while cooling
      writeHeatPWM(0);
    }
  }  
}

// --------------------------------------------------
// Machine State Control Functions
// --------------------------------------------------

void startHeating() {
  idleActive = false;
  heatActive = true;
  coolingActive = false;
  setpointReached = false;
  
  // Reset PID and switch to automatic
  myPID.Reset();
  myPID.SetMode(QuickPID::Control::automatic);
}

void stopHeating() {
  heatActive = false;
  idleActive = false;
  coolingActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);
  
  moveActuatorToIdle();
}

void emergencyStop() {
  heatActive = false;
  idleActive = false;
  coolingActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);

  moveActuatorToIdle();
}

// --------------------------------------------------
// Actuator Functions
// --------------------------------------------------

// Actuator movement
void updateActuator() {
  int currentSensorState = digitalRead(sensorPin);
  
  // If sensor is 1 while moving left, stop instantly
  if (currentSensorState == 1 && stepper.speed() < 0) {
    stepper.setSpeed(0);
    stepper.setCurrentPosition(0); // Resets distanceToGo to 0
  }

  if (currentSensorState != lastSensorState) {
    lastSensorState = currentSensorState;
  }

  if (stepper.distanceToGo() != 0) {
    stepper.runSpeed();
  }
}

// Move toward sensor (position 0)
void moveActuatorToIdle() {
  if (digitalRead(sensorPin) == 0) {
    stepper.moveTo(0);
    stepper.setSpeed(-maxSpeedSteps); // Set raw speed backward
  }
}

// Move away from sensor (to maxSteps)
void moveActuatorAway() {
  stepper.moveTo(maxSteps);
  stepper.setSpeed(maxSpeedSteps); // Set raw speed forward
}

// --------------------------------------------------
// Temperature and Heat Functions
// --------------------------------------------------

String getMachineState() {
  if (heatActive) {
    return t_shield >= setpoint - 2.0 ? "heat" : "warming";
  } else if (coolingActive) {
    return "cooling";
  } else {
    return "idle";
  }
}

String getShieldStatus() {
  if (stepper.distanceToGo() != 0) {
    return (stepper.targetPosition() == 0) ? "moving to idle" : "moving to engaged";
  } else {
    return (stepper.currentPosition() == 0) ? "idle" : "engaged";
  }
}

void writeHeatPWM(int value) {
  smoothedOutput = smoothedOutput * (1.0 - smoothingFactor) + value * smoothingFactor;
  heatPWM = constrain((int)smoothedOutput, 0, 255);
  analogWrite(heatOutput, heatPWM);
}

// Temp sensor
float readTemperature() {
  static float simulatedTemp = 0.0;
  float ambient = 0.0;
  
  // Champion torch two flame simulation
  float heating = 0.0;
  if (heatPWM <= centerFireMaxPWM) {
    heating = (heatPWM / (float)centerFireMaxPWM) * (maxHeatingRate * 0.3); // Center fire only
  } else {
    // Outer fire engaged
    float centerFireHeat = maxHeatingRate * 0.5;  // Center fire at max
    float outerFireHeat = ((heatPWM - centerFireMaxPWM) / (255.0 - centerFireMaxPWM)) * (maxHeatingRate * 0.7);
    heating = centerFireHeat + outerFireHeat;
  }  

  float cooling = (simulatedTemp - ambient) * 0.04;
  simulatedTemp += (heating - cooling) * (sampleTime / 1000.0);
  simulatedTemp = constrain(simulatedTemp, 0.0, 1450.0);
  return simulatedTemp;
}