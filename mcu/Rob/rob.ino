#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS (Update these to match your Teensy 4.1 wiring!)
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_METHANE       = 19;  
const int PIN_PWM_OXYGEN        = 18; 
const int PIN_RELAY_SOLENOID    = 10;  
const int PIN_RELAY_IGNITER     = 1;  

// --- Actuator / Stepper ---
const int PIN_STEP              = 15;  
const int PIN_DIR               = 14;  
const int PIN_OPT_SENSOR        = 7;  

// --- Thermocouples (Software SPI - Dedicated DO Pins) ---
const int PIN_CS_SHIELD         = 38;  
const int PIN_CS_HOT            = 40;  
const int PIN_CS_COLD           = 41; 

// ============================================================================
// HARDWARE OBJECTS & CONFIGURATION
// ============================================================================

Adafruit_MAX31855 thermoShield(PIN_CS_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_CS_HOT);
Adafruit_MAX31855 thermoCold(PIN_CS_COLD);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

const float targetSpeed_steps   = 3000.0; 
const float homingSpeed_steps   = 3000.0; 

// Actuator Positions 
const long POS_HOME             = 0;       // Absolute left (at optical sensor)
const long POS_HEATING          = 6100;    // Center/Right (Shield blocking flame)
const long POS_TESTING          = 0;       // Back to far left (Shield out of the way)

const float O2_METHANE_RATIO    = 1.0;    

// PID Params
float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_shield = 25.0, t_hot = 25.0, t_cold = 25.0;
float controllerOutput = 0.0;

// Dynamic PID Inputs
float active_tcu_temp = 25.0; 
float current_setpoint = 1400.0; // Dynamically updated by the curve
float tempCurve[5] = {1400.0, 1400.0, 1400.0, 1400.0, 1400.0}; // 0%, 25%, 50%, 75%, 100%

QuickPID gasPID(&active_tcu_temp, &controllerOutput, &current_setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

// ============================================================================
// SYSTEM STATE & TIMING
// ============================================================================

enum SystemState { IDLE, HOMING, READY, HEATING, TESTING };
SystemState currentState = IDLE;

// Flag to track the two phases of homing
bool isHomingRight = false; 

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

  stepper.setPinsInverted(true, false, false);
  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(targetSpeed_steps); 
  stepper.setAcceleration(8000.0); 
  
  float max_pid_out = 255.0 / max(1.0f, O2_METHANE_RATIO);
  gasPID.SetOutputLimits(0, max_pid_out);
  gasPID.SetSampleTimeUs(sampleDelay * 1000);
  gasPID.SetMode(QuickPID::Control::manual);
  gasPID.SetProportionalMode(QuickPID::pMode::pOnError);
  gasPID.SetDerivativeMode(QuickPID::dMode::dOnError);
}

// ============================================================================
// MOVEMENT HELPER
// ============================================================================
// Safely sets a new target without stuttering the motor
void setTarget(long targetPos) {
  if (stepper.targetPosition() != targetPos) {
    stepper.moveTo(targetPos);
  }
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  handleSerialCommands();
  
  // High-Frequency Hardware Control
  // Handles acceleration, deceleration, and direction automatically
  stepper.run(); 

  // 10Hz Control Loop 
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
    current_setpoint = tempCurve[0]; 
  } 
  else if (currentState == HEATING) {
    current_setpoint = tempCurve[0]; 
  } 
  else if (currentState == TESTING) {
    float progress = constrain((now - testStartTime) / (testDurationSec * 1000.0), 0.0, 1.0);
    float scaled = progress * 4.0;
    int idx = (int)scaled;
    float frac = scaled - (float)idx; 

    if (idx >= 4) {
      current_setpoint = tempCurve[4];
    } else {
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
      
      if (!isHomingRight) {
        // Phase 1: BLOCKING LOOP - Hunt for the sensor (Move Left)
        stepper.setSpeed(homingSpeed_steps); 
        while (digitalRead(PIN_OPT_SENSOR) == HIGH) {
          stepper.runSpeed(); 
        }

        // Sensor Hit! Lock in 0 coordinate.
        stepper.setCurrentPosition(0);

        // Setup Phase 2: Target 6100 (Move Right using automatic acceleration)
        setTarget(POS_HEATING);
        isHomingRight = true; 
      } else {
        // Phase 2: Wait until stepper.run() physically gets us to 6100.
        // It stays in HOMING state until it arrives.
        if (stepper.distanceToGo() == 0) {
          currentState = READY;
          isHomingRight = false; // Reset flag for next time
        }
      }
      break;

    case READY:
      shutdownBurner();
      setTarget(POS_HEATING); 
      break;

    case HEATING:
      setTarget(POS_HEATING); 

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
      setTarget(POS_TESTING); // Safely sets target to 0, run() automatically drives left

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
   t_shield = thermoShield.readCelsius();
   t_hot = thermoHot.readCelsius();
   t_cold = thermoCold.readCelsius();

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
      isHomingRight = false; // Ensure we start Phase 1
    } 
    else if (command == "CMD:START") {
      // STRICT FIX: Bypassing from IDLE is locked out. Must be READY.
      if (currentState == READY) {
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
      isHomingRight = false;
      setTarget(POS_HOME); // Target 0, stepper.run() automatically drives left
    } 
    else if (command.startsWith("SET_CURVE:")) {
      String values = command.substring(10);
      int commaIdx;
      for (int i = 0; i < 4; i++) {
        commaIdx = values.indexOf(',');
        if (commaIdx != -1) {
          tempCurve[i] = values.substring(0, commaIdx).toFloat();
          values = values.substring(commaIdx + 1);
        }
      }
      tempCurve[4] = values.toFloat();
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
  Serial.print(" TARGET:"); Serial.print(current_setpoint, 2); 
  
  Serial.print(" STATE:");
  switch(currentState) {
    case IDLE: Serial.println("IDLE"); break;
    case HOMING: Serial.println("HOMING"); break;
    case READY: Serial.println("READY"); break;
    case HEATING: Serial.println("HEATING"); break;
    case TESTING: Serial.println("TESTING"); break;
  }
}