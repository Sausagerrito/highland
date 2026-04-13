#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ===================== PINS =====================
const int PIN_PWM_METHANE = 19;
const int PIN_PWM_OXYGEN = 18;
const int PIN_RELAY_SOLENOID = 10;
const int PIN_RELAY_IGNITER = 1;

const int PIN_STEP = 15;
const int PIN_DIR = 14;
const int PIN_OPT_SENSOR = 7;

const int PIN_CS_SHIELD = 38;
const int PIN_CS_HOT = 40;
const int PIN_CS_COLD = 41;

// ===================== HARDWARE =====================
Adafruit_MAX31855 thermoShield(PIN_CS_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_CS_HOT);
Adafruit_MAX31855 thermoCold(PIN_CS_COLD);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

// ===================== CONSTANTS =====================
const float targetSpeed = 3000.0;
const float homingSpeed = 3000.0;

const long POS_HOME = 0;
const long POS_HEATING = 6100;
const long POS_TESTING = 0;

const float POSITION_TOLERANCE = 5;

const float O2_METHANE_RATIO = 1.0;

// ===================== PID =====================
float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_shield = 25, t_hot = 25, t_cold = 25;
float controllerOutput = 0;

float active_tcu_temp = 25;
float current_setpoint = 1400.0;
float tempCurve[5] = {1400, 1400, 1400, 1400, 1400};

QuickPID gasPID(&active_tcu_temp, &controllerOutput, &current_setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

// ===================== STATE =====================
enum SystemState { IDLE, HOMING, READY, HEATING, TESTING };
SystemState currentState = IDLE;

enum HomingPhase { SEEK_SENSOR, MOVE_TO_HEATING };
HomingPhase homingPhase = SEEK_SENSOR;

// ===================== TIMING =====================
unsigned long lastSampleTime = 0;
const unsigned long sampleDelay = 100;

float testDurationSec = 600;
unsigned long testStartTime = 0;

unsigned long igniterStartTime = 0;
const unsigned long IGNITER_DUR = 3000;

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_PWM_METHANE, OUTPUT);
  pinMode(PIN_PWM_OXYGEN, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_IGNITER, OUTPUT);
  pinMode(PIN_OPT_SENSOR, INPUT_PULLUP);

  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();

  stepper.setPinsInverted(true, false, false);
  stepper.setMinPulseWidth(20);
  
  stepper.setMaxSpeed(targetSpeed); 

  gasPID.SetOutputLimits(0, 255);
  gasPID.SetMode(QuickPID::Control::manual);
}

// ===================== LOOP =====================
void loop() {
  handleSerialCommands();

  unsigned long now = millis();

  if (currentState == HOMING && homingPhase == SEEK_SENSOR) {

    delay(1050); // Ensures smooth homing sequence
    stepper.setSpeed(-homingSpeed); 
    stepper.runSpeed();           

    if (digitalRead(PIN_OPT_SENSOR) == HIGH) {
      stepper.setSpeed(0);             
      stepper.setCurrentPosition(0);   
      stepper.moveTo(POS_HEATING);     
      homingPhase = MOVE_TO_HEATING;
    }
  } else {
    if (stepper.distanceToGo() > 0) {
      stepper.setSpeed(targetSpeed); // Moving forward
      stepper.runSpeed();
    } else if (stepper.distanceToGo() < 0) {
      stepper.setSpeed(-targetSpeed); // Moving backward
      stepper.runSpeed();
    }
  }

  if (now - lastSampleTime >= sampleDelay) {
    lastSampleTime = now;

    readTemperatures();
    runStateMachine(now);
    sendTelemetry(now);
  }
}

// ===================== STATE MACHINE =====================
void runStateMachine(unsigned long now) {

  switch (currentState) {

    case IDLE:
      shutdownBurner();
      break;

    case HOMING:
      shutdownBurner();

      if (homingPhase == MOVE_TO_HEATING) {
        if (abs(stepper.currentPosition() - POS_HEATING) < POSITION_TOLERANCE) {
          currentState = READY;
          homingPhase = SEEK_SENSOR;
        }
      }
      break;

    case READY:
      shutdownBurner();
      stepper.moveTo(POS_HEATING);
      break;

    case HEATING:
      stepper.moveTo(POS_HEATING); 

      digitalWrite(PIN_RELAY_SOLENOID, HIGH);

      if (now - igniterStartTime < IGNITER_DUR)
        digitalWrite(PIN_RELAY_IGNITER, HIGH);
      else
        digitalWrite(PIN_RELAY_IGNITER, LOW);

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
      stepper.moveTo(POS_TESTING); 

      gasPID.Compute();
      applyGasOutputs();

      if ((now - testStartTime) / 1000.0 >= testDurationSec) {
        currentState = IDLE;
      }
      break;
  }
}

// ===================== HELPERS =====================
void applyGasOutputs() {
  analogWrite(PIN_PWM_METHANE, (int)controllerOutput);
  analogWrite(PIN_PWM_OXYGEN, (int)(controllerOutput * O2_METHANE_RATIO));
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

  active_tcu_temp = (currentState == TESTING) ? t_hot : t_shield;
}

// ===================== SERIAL =====================
void handleSerialCommands() {
  if (!Serial.available()) return;

  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command == "CMD:HOME") {
    currentState = HOMING;
    homingPhase = SEEK_SENSOR;
  }

  else if (command == "CMD:START") {
    if (currentState == READY &&
        abs(stepper.currentPosition() - POS_HEATING) < 10) {

      currentState = HEATING;
      igniterStartTime = millis();
      gasPID.Reset();
    }
  }

  else if (command == "CMD:START_SKIP") {
    currentState = TESTING;
    testStartTime = millis();
    igniterStartTime = millis();
  }

  else if (command == "CMD:STOP") {
    currentState = IDLE;
    stepper.moveTo(POS_HOME);
  }

  else if (command.startsWith("SET_TIME:")) {
    testDurationSec = command.substring(9).toFloat();
  }

  else if (command.startsWith("SET_CURVE:")) {
    String values = command.substring(10);
    for (int i = 0; i < 5; i++) {
      int idx = values.indexOf(',');
      if (idx == -1) {
        tempCurve[i] = values.toFloat();
        break;
      }
      tempCurve[i] = values.substring(0, idx).toFloat();
      values = values.substring(idx + 1);
    }
  }
}

// ===================== TELEMETRY =====================
void sendTelemetry(unsigned long now) {
  Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
  Serial.print(" TEST_TIMER:"); Serial.print(currentState == TESTING ? (now - testStartTime) / 1000.0 : 0.0, 2);
  Serial.print(" T_SHIELD:"); Serial.print(t_shield, 2);
  Serial.print(" T_HOT:"); Serial.print(t_hot, 2);
  Serial.print(" T_COLD:"); Serial.print(t_cold, 2);
  Serial.print(" TARGET:"); Serial.print(current_setpoint, 2);

  Serial.print(" STATE:");
  switch (currentState) {
    case IDLE: Serial.println("IDLE"); break;
    case HOMING: Serial.println("HOMING"); break;
    case READY: Serial.println("READY"); break;
    case HEATING: Serial.println("HEATING"); break;
    case TESTING: Serial.println("TESTING"); break;
  }
}