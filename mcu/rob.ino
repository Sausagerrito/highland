#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS (Update these to match your Teensy 4.1 wiring!)
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_METHANE       = 2;  // 24V PWM via MOSFET for Methane
const int PIN_PWM_OXYGEN        = 11; // 24V PWM via MOSFET for Oxygen
const int PIN_RELAY_SOLENOID    = 3;  // 12V Relay (Normally Closed Solenoids)
const int PIN_RELAY_IGNITER     = 4;  // 5V Relay for Spark Igniter

// --- Actuator / Stepper ---
const int PIN_STEP              = 5;  // 5V Level Shifted
const int PIN_DIR               = 6;  // 5V Level Shifted
const int PIN_OPT_SENSOR        = 7;  // Optical limit switch

// --- Thermocouples (Software SPI - Dedicated DO Pins) ---
const int PIN_SCK_SHARED        = 13; // The single Clock pin shared by all 3 sensors

const int PIN_CS_SHIELD         = 8;  // Chip Select for Shield
const int PIN_DO_SHIELD         = 12; // Dedicated Data Out for Shield

const int PIN_CS_HOT            = 9;  // Chip Select for Hot Side
const int PIN_DO_HOT            = 24; // Dedicated Data Out for Hot Side

const int PIN_CS_COLD           = 10; // Chip Select for Cold Side
const int PIN_DO_COLD           = 25; // Dedicated Data Out for Cold Side

// ============================================================================
// HARDWARE OBJECTS & CONFIGURATION
// ============================================================================

// Thermocouples initialized with Software SPI (Clock, CS, Data Out)
Adafruit_MAX31855 thermoShield(PIN_SCK_SHARED, PIN_CS_SHIELD, PIN_DO_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_SCK_SHARED, PIN_CS_HOT, PIN_DO_HOT);
Adafruit_MAX31855 thermoCold(PIN_SCK_SHARED, PIN_CS_COLD, PIN_DO_COLD);

// Actuator
AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
const float targetSpeed_steps   = 4000.0;
const float homingSpeed_steps   = 2000.0;

// Actuator Positions 
const long POS_HOME             = 0;       // Far left (at optical sensor)
const long POS_HEATING          = 3000;    // Predetermined distance to center
const long POS_TESTING          = 6000;    // Shield moved out of the way for sample

// Gas Mixture Config
const float O2_METHANE_RATIO    = 2.0;     // Multiplier for Oxygen relative to Methane

// PID Params
float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_shield = 25.0, t_hot = 25.0, t_cold = 25.0;
float setpoint = 1400.0;
float controllerOutput = 0.0;

// Dynamic PID Input (swaps between t_shield and t_hot)
float active_tcu_temp = 25.0; 

QuickPID gasPID(&active_tcu_temp, &controllerOutput, &setpoint, Kp, Ki, Kd, 
                QuickPID::pMode::pOnError, QuickPID::dMode::dOnError, QuickPID::Action::direct);

// ============================================================================
// SYSTEM STATE & TIMING
// ============================================================================

enum SystemState { IDLE, HOMING, READY, HEATING, TESTING };
SystemState currentState = IDLE;

unsigned long lastSampleTime    = 0;
const unsigned long sampleDelay = 100; // ms between PID & Temp updates

float testDurationSec           = 600.0;
unsigned long testStartTime     = 0;

unsigned long igniterStartTime  = 0;
const unsigned long IGNITER_DUR = 3000; // Spark for 3 seconds during ignition

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Pin Modes
  pinMode(PIN_PWM_METHANE, OUTPUT);
  pinMode(PIN_PWM_OXYGEN, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_IGNITER, OUTPUT);
  pinMode(PIN_OPT_SENSOR, INPUT_PULLUP);

  // Ensure everything is safely off on boot
  digitalWrite(PIN_PWM_METHANE, LOW);
  digitalWrite(PIN_PWM_OXYGEN, LOW);
  digitalWrite(PIN_RELAY_SOLENOID, LOW);
  digitalWrite(PIN_RELAY_IGNITER, LOW);

  // Thermocouple Initialization
  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();

  // Stepper Initialization
  stepper.setMaxSpeed(targetSpeed_steps);
  stepper.setAcceleration(2000.0);
  
  // PID Initialization
  // Cap controllerOutput so the O2 multiplier doesn't exceed max 8-bit PWM (255)
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
  stepper.run(); // Must be called frequently to move actuator smoothly

  unsigned long now = millis();
  if (now - lastSampleTime >= sampleDelay) {
    lastSampleTime = now;
    
    readTemperatures();
    runStateMachine(now);
    sendTelemetry(now);
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
      stepper.moveTo(POS_HEATING); // Hold center position

      digitalWrite(PIN_RELAY_SOLENOID, HIGH); // Open NC Solenoids
      
      // Fire Igniter 
      if (now - igniterStartTime < IGNITER_DUR) {
        digitalWrite(PIN_RELAY_IGNITER, HIGH);
      } else {
        digitalWrite(PIN_RELAY_IGNITER, LOW);
      }

      // Run PID (Targeting Shield)
      gasPID.SetMode(QuickPID::Control::automatic);
      gasPID.Compute();
      applyGasOutputs();

      // Check if Target Reached
      if (t_shield >= setpoint - 2.0) {
        currentState = TESTING;
        testStartTime = now;
        // CRITICAL: Reset PID history so the sudden drop to t_hot doesn't cause a massive gas spike
        gasPID.Reset(); 
      }
      break;

    case TESTING:
      stepper.moveTo(POS_TESTING); // Move shield away

      // Run PID (Now targeting Hot Side via the routing in readTemperatures)
      gasPID.Compute();
      applyGasOutputs();

      // Check Test Duration
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
  // Convert the unified PID output into proportioned Methane and Oxygen outputs
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
  
  // =========================================================
  // --- REAL HARDWARE BLOCK (Uncomment when ready) ---
  // =========================================================
  /*
  t_shield = thermoShield.readCelsius();
  t_hot = thermoHot.readCelsius();
  t_cold = thermoCold.readCelsius();
  */

  // =========================================================
  // --- SIMULATION BLOCK (Comment out for real hardware) ---
  // =========================================================
  simulateTemperatures(); 

  // --- DYNAMIC PID SENSOR ROUTING ---
  // Feed the active_tcu_temp variable the correct sensor based on the state
  if (currentState == TESTING) {
    active_tcu_temp = t_hot;     // Follow sample once shield drops
  } else {
    active_tcu_temp = t_shield;  // Follow shield during pre-heat
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
    else if (command.startsWith("SET_TEMP:")) {
      setpoint = command.substring(9).toFloat();
    } 
    else if (command.startsWith("SET_TIME:")) {
      testDurationSec = command.substring(9).toFloat();
    }
  }
}

void sendTelemetry(unsigned long now) {
  // Format matching the expected Rust struct
  Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
  Serial.print(" TEST_TIMER:"); Serial.print(currentState == TESTING ? (now - testStartTime) / 1000.0 : 0.0, 2);
  Serial.print(" T_SHIELD:"); Serial.print(t_shield, 2);
  Serial.print(" T_HOT:"); Serial.print(t_hot, 2);
  Serial.print(" T_COLD:"); Serial.print(t_cold, 2);
  Serial.print(" TARGET:"); Serial.print(setpoint, 2);
  
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
// SIMULATION ENVIRONMENT (TO BE COMMENTED OUT IN PRODUCTION)
// ============================================================================
void simulateTemperatures() {
  float ambient = 25.0;
  // Use Methane output as the base for simulation heat generation
  float heatingPower = (controllerOutput / 255.0) * 1500.0; 
  
  if (currentState == HEATING) {
    t_shield += (heatingPower - (t_shield - ambient)) * 0.05;
    t_hot += (heatingPower * 0.1 - (t_hot - ambient)) * 0.01; // Hot side protected by shield
    t_cold += (heatingPower * 0.05 - (t_cold - ambient)) * 0.005;
  } 
  else if (currentState == TESTING) {
    // Shield moves away, starts cooling
    t_shield -= (t_shield - ambient) * 0.05; 
    // Hot side takes the brunt of the flame
    t_hot += (heatingPower * 0.8 - (t_hot - ambient)) * 0.05;
    t_cold += (heatingPower * 0.2 - (t_cold - ambient)) * 0.01;
  } 
  else {
    // Everything cools
    t_shield -= (t_shield - ambient) * 0.05;
    t_hot -= (t_hot - ambient) * 0.02;
    t_cold -= (t_cold - ambient) * 0.005;
  }
  
  t_shield = constrain(t_shield, ambient, 1600.0);
  t_hot = constrain(t_hot, ambient, 1600.0);
  t_cold = constrain(t_cold, ambient, 500.0);
}
