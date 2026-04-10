#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS (Teensy 4.1 Placeholders)
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_GAS_VALVES    = 2;  // 24V PWM via MOSFET for proportional valves
const int PIN_RELAY_SOLENOID    = 3;  // 12V Relay (Normally Closed Solenoids)
const int PIN_RELAY_IGNITER     = 4;  // 5V Relay for Spark Igniter

// --- Actuator / Stepper ---
const int PIN_STEP              = 5;  // 5V Level Shifted
const int PIN_DIR               = 6;  // 5V Level Shifted
const int PIN_OPT_SENSOR        = 7;  // Optical limit switch

// --- Thermocouples (SPI) ---
const int PIN_CS_SHIELD         = 8;
const int PIN_CS_HOT            = 9;
const int PIN_CS_COLD           = 10;
// Note: MAX31855 uses standard hardware SPI (MISO, SCK) on the Teensy.

// ============================================================================
// HARDWARE OBJECTS & CONFIGURATION
// ============================================================================

// Thermocouples
Adafruit_MAX31855 thermoShield(PIN_CS_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_CS_HOT);
Adafruit_MAX31855 thermoCold(PIN_CS_COLD);

// Actuator
AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
const float targetSpeed_steps   = 4000.0;
const float homingSpeed_steps   = 2000.0;

// Actuator Positions (Adjust these step values to match physical distances)
const long POS_HOME             = 0;       // Far left (at optical sensor)
const long POS_HEATING          = 3000;    // Predetermined distance to center
const long POS_TESTING          = 6000;    // Shield moved out of the way for sample

// PID Params
float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_shield = 25.0, t_hot = 25.0, t_cold = 25.0;
float setpoint = 1400.0;
float controllerOutput = 0.0;
QuickPID gasPID(&t_shield, &controllerOutput, &setpoint, Kp, Ki, Kd, QuickPID::pMode::pOnError, QuickPID::dMode::dOnError, QuickPID::Action::direct);

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

bool skipPreheat = false;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Pin Modes
  pinMode(PIN_PWM_GAS_VALVES, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_IGNITER, OUTPUT);
  pinMode(PIN_OPT_SENSOR, INPUT_PULLUP);

  // Ensure everything is safely off on boot
  digitalWrite(PIN_PWM_GAS_VALVES, LOW);
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
  gasPID.SetOutputLimits(0, 255);
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
      stepper.setSpeed(-homingSpeed_steps); // Move left towards sensor
      stepper.runSpeed(); // Bypasses acceleration for constant speed homing
      
      if (digitalRead(PIN_OPT_SENSOR) == LOW) { // Triggered
        stepper.setSpeed(0);
        stepper.setCurrentPosition(POS_HOME);
        currentState = READY;
      }
      break;

    case READY:
      shutdownBurner();
      stepper.moveTo(POS_HEATING); // Move to center position
      break;

    case HEATING:
      // Ensure Stepper stays at Center position
      stepper.moveTo(POS_HEATING);

      // 1. Open NC Solenoids
      digitalWrite(PIN_RELAY_SOLENOID, HIGH);
      
      // 2. Fire Igniter for the first few seconds
      if (now - igniterStartTime < IGNITER_DUR) {
        digitalWrite(PIN_RELAY_IGNITER, HIGH);
      } else {
        digitalWrite(PIN_RELAY_IGNITER, LOW);
      }

      // 3. Run PID for Gas Control
      gasPID.SetMode(QuickPID::Control::automatic);
      gasPID.Compute();
      analogWrite(PIN_PWM_GAS_VALVES, (int)controllerOutput);

      // 4. Check if Target Reached
      if (t_shield >= setpoint - 2.0) {
        currentState = TESTING;
        testStartTime = now;
      }
      break;

    case TESTING:
      // 1. Move shield to expose sample
      stepper.moveTo(POS_TESTING);

      // 2. Maintain Temp via PID
      gasPID.Compute();
      analogWrite(PIN_PWM_GAS_VALVES, (int)controllerOutput);

      // 3. Check Test Duration
      if ((now - testStartTime) / 1000.0 >= testDurationSec) {
        currentState = IDLE;
      }
      break;
  }
}

// ============================================================================
// HARDWARE CONTROL & SENSORS
// ============================================================================
void shutdownBurner() {
  gasPID.SetMode(QuickPID::Control::manual);
  controllerOutput = 0;
  analogWrite(PIN_PWM_GAS_VALVES, 0);
  digitalWrite(PIN_RELAY_SOLENOID, LOW);
  digitalWrite(PIN_RELAY_IGNITER, LOW);
}

void readTemperatures() {
  // --- REAL HARDWARE BLOCK ---
  // t_shield = thermoShield.readCelsius();
  // t_hot = thermoHot.readCelsius();
  // t_cold = thermoCold.readCelsius();

  // --- SIMULATION BLOCK (Comment out for real hardware) ---
  simulateTemperatures(); 
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
      stepper.moveTo(POS_HOME); // Optional: send back to home on abort
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
  float heatingPower = (controllerOutput / 255.0) * 1500.0; // Simulated BTU->C max output
  
  if (currentState == HEATING || currentState == TESTING) {
    t_shield += (heatingPower - (t_shield - ambient)) * 0.05;
    t_hot += (heatingPower * 0.8 - (t_hot - ambient)) * 0.02;
    t_cold += (heatingPower * 0.1 - (t_cold - ambient)) * 0.005;
  } else {
    // Cooling
    t_shield -= (t_shield - ambient) * 0.05;
    t_hot -= (t_hot - ambient) * 0.02;
    t_cold -= (t_cold - ambient) * 0.005;
  }
  
  // Constrain to realistic boundaries
  t_shield = constrain(t_shield, ambient, 1600.0);
  t_hot = constrain(t_hot, ambient, 1600.0);
  t_cold = constrain(t_cold, ambient, 500.0);
}
