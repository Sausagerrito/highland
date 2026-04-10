#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS (Update these to match your Teensy 4.1 wiring!)
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_METHANE       = 2;  
const int PIN_PWM_OXYGEN        = 11; 
const int PIN_RELAY_SOLENOID    = 3;  
const int PIN_RELAY_IGNITER     = 4;  

// --- Actuator / Stepper ---
const int PIN_STEP              = 5;  
const int PIN_DIR               = 6;  
const int PIN_OPT_SENSOR        = 7;  

// --- Thermocouples (Software SPI - Dedicated DO Pins) ---
const int PIN_SCK_SHARED        = 13; 
const int PIN_CS_SHIELD         = 8;  
const int PIN_DO_SHIELD         = 12; 
const int PIN_CS_HOT            = 9;  
const int PIN_DO_HOT            = 24; 
const int PIN_CS_COLD           = 10; 
const int PIN_DO_COLD           = 25; 

// ============================================================================
// HARDWARE OBJECTS & CONFIGURATION
// ============================================================================

Adafruit_MAX31855 thermoShield(PIN_SCK_SHARED, PIN_CS_SHIELD, PIN_DO_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_SCK_SHARED, PIN_CS_HOT, PIN_DO_HOT);
Adafruit_MAX31855 thermoCold(PIN_SCK_SHARED, PIN_CS_COLD, PIN_DO_COLD);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
const float targetSpeed_steps   = 4000.0;
const float homingSpeed_steps   = 2000.0;

// Actuator Positions 
const long POS_HOME             = 0;       // Far left (at optical sensor)
const long POS_HEATING          = 3000;    // Center (Shield blocking flame)
const long POS_TESTING          = 0;       // Back to far left (Shield out of the way)

const float O2_METHANE_RATIO    = 2.0;     

// PID Params
float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_shield = 25.0, t_hot = 25.0, t_cold = 25.0;
float controllerOutput = 0.0;

// Dynamic PID Inputs
float active_tcu_temp = 25.0; 
float current_setpoint = 1400.0; // Dynamically updated by the curve
float tempCurve[5] = {1400.0, 1400.0, 1400.0, 1400.0, 1400.0}; // 0%, 25%, 50%, 75%, 100%

QuickPID gasPID(&active_tcu_temp, &controllerOutput, &current_setpoint, Kp, Ki, Kd, 
                QuickPID::pMode::pOnError, QuickPID::dMode::dOnError, QuickPID::Action::direct);

// ============================================================================
// SYSTEM STATE & TIMING
// ============================================================================

enum SystemState { IDLE, HOMING, READY, HEATING, TESTING };
SystemState currentState = IDLE;

unsigned long lastSampleTime    = 0;
const unsigned long sampleDelay = 100; 

float testDurationSec           = 600.0;
unsigned long testStartTime     = 0;

unsigned long igniterStartTime  = 0;
const unsigned long IGNITER_DUR = 3000; 

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_PWM_METHANE, OUTPUT);
  pinMode(PIN_PWM_OXYGEN, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_IGNITER, OUTPUT);
  pinMode(PIN_OPT_SENSOR, INPUT_PULLUP);

  digitalWrite(PIN_PWM_METHANE, LOW);
  digitalWrite(PIN_PWM_OXYGEN, LOW);
  digitalWrite(PIN_RELAY_SOLENOID, LOW);
  digitalWrite(PIN_RELAY_IGNITER, LOW);

  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();

  stepper.setMaxSpeed(targetSpeed_steps);
  stepper.setAcceleration(2000.0);
  
  float max_pid_out = 255.0 / max(1.0f, O2_METHANE_RATIO);
  gasPID.SetOutputLimits(0, max_pid_out);
  gasPID.SetSampleTimeUs(sampleDelay * 1000);
  gasPID.SetMode(QuickPID::Control::manual);
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  handleSerialCommands();
  stepper.run(); 

  unsigned long now = millis();
  if (now - lastSampleTime >= sampleDelay) {
    lastSampleTime = now;
    
    updateTargetCurve(now); 
    readTemperatures();
    runStateMachine(now);
    sendTelemetry(now);
  }
}

// ============================================================================
// TEMPERATURE CURVE INTERPOLATION
// ============================================================================
void updateTargetCurve(unsigned long now) {
  if (currentState == IDLE || currentState == HOMING || currentState == READY) {
    current_setpoint = tempCurve[0]; // Rest at the initial target
  } 
  else if (currentState == HEATING) {
    current_setpoint = tempCurve[0]; // Preheat to the 0% mark
  } 
  else if (currentState == TESTING) {
    // Calculate how far into the test we are (0.0 to 1.0)
    float progress = constrain((now - testStartTime) / (testDurationSec * 1000.0), 0.0, 1.0);
    
    // Scale progress to our 4 intervals (0-1, 1-2, 2-3, 3-4)
    float scaled = progress * 4.0;
    int idx = (int)scaled;
    float frac = scaled - (float)idx; // The remainder (e.g., 50% between point 1 and 2)

    if (idx >= 4) {
      current_setpoint = tempCurve[4];
    } else {
      // Linear interpolation formula: A + (B - A) * percent
      current_setpoint = tempCurve[idx] + (tempCurve[idx + 1] - tempCurve[idx]) * frac;
    }
  }
}

// ============================================================================
// STATE MACHINE LOGIC
// ============================================================================
void runStateMachine(unsigned long now) {
  switch (currentState) {
    
    case IDLE:
      shutdownBurner();
      break;

    case HOMING:
      shutdownBurner();
      stepper.setSpeed(-homingSpeed_steps); 
      stepper.runSpeed(); 
      
      if (digitalRead(PIN_OPT_SENSOR) == LOW) { 
        stepper.setSpeed(0);
        stepper.setCurrentPosition(POS_HOME);
        currentState = READY;
      }
      break;

    case READY:
      shutdownBurner();
      stepper.moveTo(POS_HEATING); 
      break;

    case HEATING:
      stepper.moveTo(POS_HEATING); 

      digitalWrite(PIN_RELAY_SOLENOID, HIGH); 
      
      if (now - igniterStartTime < IGNITER_DUR) {
        digitalWrite(PIN_RELAY_IGNITER, HIGH);
      } else {
        digitalWrite(PIN_RELAY_IGNITER, LOW);
      }

      gasPID.SetMode(QuickPID::Control::automatic);
      gasPID.Compute();
      applyGasOutputs();

      if (t_shield >= current_setpoint - 2.0) {
        currentState = TESTING;
        testStartTime = now;
        gasPID.Reset(); 
      }
      break;

    case TESTING:
      stepper.moveTo(POS_TESTING); // Move back to the left (Home)

      gasPID.Compute();
      applyGasOutputs();

      if ((now - testStartTime) / 1000.0 >= testDurationSec) {
        currentState = IDLE;
      }
      break;
  }
}

// ============================================================================
// HARDWARE CONTROL & SENSORS
// ============================================================================
void applyGasOutputs() {
  int methane_pwm = constrain((int)controllerOutput, 0, 255);
  int oxygen_pwm  = constrain((int)(controllerOutput * O2_METHANE_RATIO), 0, 255);

  analogWrite(PIN_PWM_METHANE, methane_pwm);
  analogWrite(PIN_PWM_OXYGEN, oxygen_pwm);
}

void shutdownBurner() {
  gasPID.SetMode(QuickPID::Control::manual);
  controllerOutput = 0;
  analogWrite(PIN_PWM_METHANE, 0);
  analogWrite(PIN_PWM_OXYGEN, 0);
  digitalWrite(PIN_RELAY_SOLENOID, LOW);
  digitalWrite(PIN_RELAY_IGNITER, LOW);
}

void readTemperatures() {
  // --- REAL HARDWARE ---
  // t_shield = thermoShield.readCelsius();
  // t_hot = thermoHot.readCelsius();
  // t_cold = thermoCold.readCelsius();

  // --- SIMULATION ---
  simulateTemperatures(); 

  // --- DYNAMIC PID SENSOR ROUTING ---
  if (currentState == TESTING) {
    active_tcu_temp = t_hot;     
  } else {
    active_tcu_temp = t_shield;  
  }
}

// ============================================================================
// SERIAL COMMUNICATIONS (Interfaces with Rust eframe)
// ============================================================================
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "CMD:HOME") {
      currentState = HOMING;
    } 
    else if (command == "CMD:START") {
      if (currentState == READY || currentState == IDLE) {
        currentState = HEATING;
        igniterStartTime = millis();
        gasPID.Reset();
      }
    } 
    else if (command == "CMD:START_SKIP") {
      currentState = TESTING;
      igniterStartTime = millis();
      testStartTime = millis();
      gasPID.Reset();
    } 
    else if (command == "CMD:STOP") {
      currentState = IDLE;
      stepper.moveTo(POS_HOME); 
    } 
    else if (command.startsWith("SET_CURVE:")) {
      // Expected format: SET_CURVE:1000,1200,1400,1200,1000
      String values = command.substring(10);
      int commaIdx;
      for (int i = 0; i < 4; i++) {
        commaIdx = values.indexOf(',');
        if (commaIdx != -1) {
          tempCurve[i] = values.substring(0, commaIdx).toFloat();
          values = values.substring(commaIdx + 1);
        }
      }
      tempCurve[4] = values.toFloat(); // Last value
    } 
    else if (command.startsWith("SET_TIME:")) {
      testDurationSec = command.substring(9).toFloat();
    }
  }
}

void sendTelemetry(unsigned long now) {
  Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
  Serial.print(" TEST_TIMER:"); Serial.print(currentState == TESTING ? (now - testStartTime) / 1000.0 : 0.0, 2);
  Serial.print(" T_SHIELD:"); Serial.print(t_shield, 2);
  Serial.print(" T_HOT:"); Serial.print(t_hot, 2);
  Serial.print(" T_COLD:"); Serial.print(t_cold, 2);
  Serial.print(" TARGET:"); Serial.print(current_setpoint, 2); // Now sends dynamic target
  
  Serial.print(" STATE:");
  switch(currentState) {
    case IDLE: Serial.println("IDLE"); break;
    case HOMING: Serial.println("HOMING"); break;
    case READY: Serial.println("READY"); break;
    case HEATING: Serial.println("HEATING"); break;
    case TESTING: Serial.println("TESTING"); break;
  }
}

// ============================================================================
// SIMULATION ENVIRONMENT
// ============================================================================
void simulateTemperatures() {
  float ambient = 25.0;
  float heatingPower = (controllerOutput / 255.0) * 1500.0; 
  
  if (currentState == HEATING) {
    t_shield += (heatingPower - (t_shield - ambient)) * 0.05;
    t_hot += (heatingPower * 0.1 - (t_hot - ambient)) * 0.01; 
    t_cold += (heatingPower * 0.05 - (t_cold - ambient)) * 0.005;
  } 
  else if (currentState == TESTING) {
    t_shield -= (t_shield - ambient) * 0.05; 
    t_hot += (heatingPower * 0.8 - (t_hot - ambient)) * 0.05;
    t_cold += (heatingPower * 0.2 - (t_cold - ambient)) * 0.01;
  } 
  else {
    t_shield -= (t_shield - ambient) * 0.05;
    t_hot -= (t_hot - ambient) * 0.02;
    t_cold -= (t_cold - ambient) * 0.005;
  }
  
  t_shield = constrain(t_shield, ambient, 1600.0);
  t_hot = constrain(t_hot, ambient, 1600.0);
  t_cold = constrain(t_cold, ambient, 500.0);
}
