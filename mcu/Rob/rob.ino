#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ===================== PINS =====================
const int METHANE_PWM = 19;
const int OXYGEN_PWM = 18;
const int SOLENOID_RELAY = 10;
const int IGNITER_RELAY = 1;

const int STEPPER_STEP = 15;
const int STEPPER_DIR = 14;
const int OPTICAL_SENSOR = 0;

const int CS_SHIELD = 38;
const int CS_HOT = 40;
const int CS_COLD = 41;

// ===================== HARDWARE =====================
Adafruit_MAX31855 tcShield(CS_SHIELD);
Adafruit_MAX31855 tcHot(CS_HOT);
Adafruit_MAX31855 tcCold(CS_COLD);

AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP, STEPPER_DIR);

// ===================== CONSTANTS =====================
const float targetSpeed = 3000.0;
const float homingSpeed = 3000.0;
const long posHome = 0;
const long posHeating = 6100;
const float o2MethaneRatio = 6.0;

// ===================== PID =====================
float kp = 5.0, ki = 0.15, kd = 40.0;
float tempShield = 25.0, tempHot = 25.0, tempCold = 25.0;
float pidOutput = 0.0;

float activeTemp = 25.0;
float targetTemp = 1000.0;
float tempCurve[5] = {1000, 1000, 1000, 1000, 1000};

QuickPID burnerPid(&activeTemp, &pidOutput, &targetTemp, kp, ki, kd, QuickPID::Action::direct);

// ===================== STATE =====================
enum SystemState { STATE_IDLE, STATE_HOMING, STATE_READY, STATE_HEATING };
SystemState currentState = STATE_IDLE;

enum HomingPhase { PHASE_SEEK, PHASE_MOVE };
HomingPhase homingPhase = PHASE_SEEK;

int sensorDebounce = 0;
bool autoStartSequence = false;

// ===================== TIMING =====================
unsigned long lastSample = 0;
const unsigned long sampleInterval = 250;

unsigned long igniterStart = 0;
const unsigned long igniterDuration = 5000;

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(METHANE_PWM, OUTPUT);
  pinMode(OXYGEN_PWM, OUTPUT);
  pinMode(SOLENOID_RELAY, OUTPUT);
  pinMode(IGNITER_RELAY, OUTPUT);
  pinMode(OPTICAL_SENSOR, INPUT_PULLUP);

  tcShield.begin();
  tcHot.begin();
  tcCold.begin();

  stepper.setPinsInverted(false, false, false);
  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(targetSpeed); 
  stepper.setAcceleration(8000);

  burnerPid.SetOutputLimits(0, 255 / 6);
  burnerPid.SetMode(QuickPID::Control::manual);
}

// ===================== LOOP =====================
void loop() {
  processSerial();

  unsigned long now = millis();

  if (currentState == STATE_HOMING && homingPhase == PHASE_SEEK) {
    stepper.setSpeed(-homingSpeed); 
    stepper.runSpeed();

    if (digitalRead(OPTICAL_SENSOR) == HIGH) {
      sensorDebounce++;
      if (sensorDebounce >= 10) {
        stepper.setSpeed(0);             
        stepper.setCurrentPosition(0);   
        
        if (autoStartSequence) {
          stepper.moveTo(posHeating);
        } else {
          stepper.moveTo(0);
        }
        
        currentState = STATE_READY;
        sensorDebounce = 0;
      }
    } else {
      sensorDebounce = 0;
    }
  } else {
    stepper.run(); 
  }

  if (now - lastSample >= sampleInterval) {
    lastSample = now;
    readSensors();
    updateState(now);
    printTelemetry(now);
  }
}

// ===================== STATE MACHINE =====================
void updateState(unsigned long now) {
  switch (currentState) {
    case STATE_IDLE:
      stopBurner();
      break;

    case STATE_HOMING:
      stopBurner();
      break;

    case STATE_READY:
      stopBurner();
      stepper.moveTo(posHeating);
      
      if (autoStartSequence && stepper.distanceToGo() == 0) {
        autoStartSequence = false;
        currentState = STATE_HEATING;
        igniterStart = now;
        burnerPid.Reset();
      }
      break;

    case STATE_HEATING:
      stepper.moveTo(posHeating); 
      digitalWrite(SOLENOID_RELAY, HIGH);
      
      burnerPid.SetMode(QuickPID::Control::automatic);
      burnerPid.Compute();
      setValves();

      unsigned long heatingTime = now - igniterStart;
      
      if (heatingTime < 2000) {
        digitalWrite(IGNITER_RELAY, LOW);
      } else if (heatingTime < 2000 + igniterDuration) {
        digitalWrite(IGNITER_RELAY, HIGH);
      } else {
        digitalWrite(IGNITER_RELAY, LOW);
      }
      break;
  }
}

// ===================== HELPERS =====================
void setValves() {
  analogWrite(METHANE_PWM, (int)pidOutput);
  analogWrite(OXYGEN_PWM, (int)(pidOutput * o2MethaneRatio));
}

void stopBurner() {
  burnerPid.SetMode(QuickPID::Control::manual);
  pidOutput = 0;
  analogWrite(METHANE_PWM, 0);
  analogWrite(OXYGEN_PWM, 0);
  digitalWrite(SOLENOID_RELAY, LOW);
  digitalWrite(IGNITER_RELAY, LOW);
}

void readSensors() {
  tempShield = tcShield.readCelsius();
  tempHot = tcHot.readCelsius();
  tempCold = tcCold.readCelsius();
  activeTemp = tempShield;
}

// ===================== SERIAL =====================
void processSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "CMD:HOME") {
    autoStartSequence = false;
    if (digitalRead(OPTICAL_SENSOR) == HIGH) {
      stepper.setCurrentPosition(0);
      stepper.moveTo(0);
      currentState = STATE_READY;
    } else {
      delay(1050);
      currentState = STATE_HOMING;
      homingPhase = PHASE_SEEK;
      sensorDebounce = 0;
    }
  }

  else if (cmd == "CMD:START") {
    autoStartSequence = true;
    
    if (digitalRead(OPTICAL_SENSOR) == HIGH) {
      stepper.setCurrentPosition(0);
      stepper.moveTo(posHeating);
      currentState = STATE_READY;
    } else {
      currentState = STATE_HOMING;
      homingPhase = PHASE_SEEK;
      sensorDebounce = 0;
    }
  }

  else if (cmd == "CMD:STOP") {
    autoStartSequence = false; 
    currentState = STATE_IDLE;
    stepper.moveTo(posHeating);
  }

  else if (cmd.startsWith("SET_CURVE:")) {
    String values = cmd.substring(10);

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
void printTelemetry(unsigned long now) {
  Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
  Serial.print(" T_SHIELD:"); Serial.print(tempShield, 2);
  Serial.print(" T_HOT:"); Serial.print(tempHot, 2);
  Serial.print(" T_COLD:"); Serial.print(tempCold, 2);
  Serial.print(" TARGET:"); Serial.print(targetTemp, 2);

  Serial.print(" STATE:");
  switch (currentState) {
    case STATE_IDLE: Serial.println("IDLE"); break;
    case STATE_HOMING: Serial.println("HOMING"); break;
    case STATE_READY: Serial.println("READY"); break;
    case STATE_HEATING: Serial.println("HEATING"); break;
  }
}