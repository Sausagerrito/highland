#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ===================== PINS =====================
const int METH = 18, OX = 19;

const int SOL = 10, IGN = 1, AIR = 9;

const int STEP = 15, DIR = 14, OPT = 0;

const int SHLD = 41, HOT = 40, COLD = 39;

// ===================== HARDWARE =====================
Adafruit_MAX31855 tShld(SHLD);
Adafruit_MAX31855 tHot(HOT);
Adafruit_MAX31855 tCold(COLD);

AccelStepper stepper(AccelStepper::DRIVER, STEP, DIR);

// ===================== CONSTANTS =====================
const float SPEED = 4000.0;
const long posHome = 0;
const long posHeating = 6100;
const float cutoffTemp = 1250.0; 

// ===================== PID =====================
float t_Shld = 25.0, t_Hot = 25.0, t_Cold = 25.0;
float output = 0.0;

float Kp = 1, Ki = 0.01, Kd = 1;

float t_Active = 25.0;
float setP = 1200.0; 
float tempCurve[5] = {1200, 1200, 1200, 1200, 1200};


QuickPID gasPID(&t_Active, &output, &setP, Kp, Ki, Kd, QuickPID::Action::direct);

// ===================== STATE =====================
enum SystemState { STATE_IDLE, STATE_HOMING, STATE_READY, STATE_HEATING, STATE_TESTING, STATE_PURGE };
SystemState currentState = STATE_IDLE;

enum HomingPhase { PHASE_SEEK, PHASE_MOVE };
HomingPhase homingPhase = PHASE_SEEK;

int sensorDebounce = 0;
bool autoStartSequence = false;

// ===================== TIMING =====================
unsigned long last = 0;
const unsigned long sDelay = 100;

unsigned long igniterStart = 0;
unsigned long testStartTime = 0;
unsigned long testDurationMillis = 600000; 
unsigned long purgeStartTime = 0;

unsigned long gas = 5000;
unsigned long sprk = 3000;
unsigned long air_on = 1000;
unsigned long air_off = 1000;
// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(METH, OUTPUT);
  pinMode(OX, OUTPUT);
  pinMode(SOL, OUTPUT);
  pinMode(IGN, OUTPUT);
  pinMode(AIR, OUTPUT);
  pinMode(OPT, INPUT_PULLUP);

  tShld.begin();
  tHot.begin();
  tCold.begin();

  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(SPEED); 
  stepper.setAcceleration(8000);

  analogWriteFrequency(METH, 500);
  analogWriteFrequency(OX, 500);

  gasPID.SetOutputLimits(40, 255);
  gasPID.SetSampleTimeUs(100000);
  gasPID.SetTunings(Kp, Ki, Kd, QuickPID::pMode::pOnMeas, QuickPID::dMode::dOnMeas, QuickPID::iAwMode::iAwCondition);
  
  gasPID.SetMode(QuickPID::Control::manual);
}

// ===================== LOOP =====================
void loop() {
  RX();

  unsigned long now = millis();

  // Handle Stepper Homing non-blocking
  if (currentState == STATE_HOMING && homingPhase == PHASE_SEEK) {
    stepper.setSpeed(-SPEED); 
    stepper.runSpeed();
    
    if (digitalRead(OPT) == HIGH) {
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

  // Core Loop 
  if (now - last >= (unsigned long)sDelay) {
    last = now;
    tcu();
    gasPID.Compute(); 
    updateState(now);
    TX(now);
  }
}

// ===================== STATE MACHINE =====================
void updateState(unsigned long now) {
  if (t_Shld >= cutoffTemp || t_Hot >= cutoffTemp || t_Cold >= cutoffTemp) {
    stopBurner();
    currentState = STATE_IDLE;
    stepper.moveTo(posHome);
    return;
  }

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
        gasPID.Reset();
      }
      break;

    case STATE_HEATING: {
      stepper.moveTo(posHeating); 
      digitalWrite(SOL, HIGH);
      digitalWrite(AIR, LOW);      

      unsigned long heatingTime = now - igniterStart;
      
      // Ignition Sequence
      if (heatingTime < gas) {
        digitalWrite(IGN, LOW);
        digitalWrite(AIR, LOW);      
        analogWrite(METH, 255);
        analogWrite(OX, 0);
      } else if (heatingTime < gas + sprk) {
        digitalWrite(IGN, HIGH);
        digitalWrite(AIR, LOW);      
        analogWrite(METH, 255);
        analogWrite(OX, 0);
      } else {
        digitalWrite(IGN, LOW);
        output = 255;
        analogWrite(METH, 255);
        analogWrite(OX, 255);
        
        // State Transition
        if (t_Shld >= setP) {
          gasPID.SetMode(QuickPID::Control::automatic);
          currentState = STATE_TESTING;
          testStartTime = now;
        }
      }
      break;
    }

    case STATE_TESTING: {
      stepper.moveTo(posHome);
      
      digitalWrite(SOL, HIGH);
      setValves();

      unsigned long elapsed = now - testStartTime;
      
      if (elapsed <= testDurationMillis && testDurationMillis > 0) {
        float progress = (float)elapsed / testDurationMillis; 
        
        float segmentFloat = progress * 4.0; 
        int segment = (int)segmentFloat;

        if (segment >= 4) {
          setP = tempCurve[4]; 
        } else {
            float segmentProgress = segmentFloat - segment;
            setP = tempCurve[segment] + ((tempCurve[segment + 1] - tempCurve[segment]) * segmentProgress);
        }
      }

      if (elapsed % (air_on + air_off) >= air_off) {
        digitalWrite(AIR, HIGH);
      } else {
        digitalWrite(AIR, LOW);
      }
      
      if (now - testStartTime >= testDurationMillis) {
        currentState = STATE_IDLE;
      }
      break;
    }

    case STATE_PURGE: {
      stepper.moveTo(posHome);
      output = 255;
      analogWrite(METH, 255);
      analogWrite(OX, 255);
      digitalWrite(SOL, HIGH);  
      digitalWrite(IGN, LOW);  
      digitalWrite(AIR, HIGH);       
      if (now - purgeStartTime >= 30000) { 
        stopBurner();
        currentState = STATE_IDLE;
      }
      break;
    }
  }
}

// ===================== HELPERS =====================
void setValves() {
  analogWrite(METH, (int)output);
  analogWrite(OX, (int)output);
}

void stopBurner() {
  gasPID.SetMode(QuickPID::Control::manual);
  output = 0;
  analogWrite(METH, 0);
  analogWrite(OX, 0);
  digitalWrite(SOL, LOW);
  digitalWrite(IGN, LOW);
  digitalWrite(AIR, LOW);
}

void tcu() {
  float t_rawShld = tShld.readCelsius();
  float t_rawHot = tHot.readCelsius();
  float t_rawCold = tCold.readCelsius();

  if (!isnan(t_rawShld)) t_Shld = t_rawShld;
  if (!isnan(t_rawHot)) t_Hot = t_rawHot;
  if (!isnan(t_rawCold)) t_Cold = t_rawCold;

  if (currentState == STATE_TESTING) {
    t_Active = t_Hot;
  } else {
    t_Active = t_Shld;
  }
}

// ===================== SERIAL =====================
void RX() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "CMD:HOME") {
    autoStartSequence = false;
    
    if (digitalRead(OPT) == HIGH) {
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
    
    if (digitalRead(OPT) == HIGH) {
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
    stepper.moveTo(posHome);
  }
  else if (cmd == "CMD:PURGE") {
    if (currentState == STATE_IDLE || currentState == STATE_READY) {
      currentState = STATE_PURGE;
      purgeStartTime = millis();
      gasPID.SetMode(QuickPID::Control::manual);
    }
  }
  else if (cmd.startsWith("SET_TIME:")) {
    float timeSec = cmd.substring(9).toFloat();
    testDurationMillis = (unsigned long)(timeSec * 1000.0);
  }
  else if (cmd.startsWith("SET_SPRK:")) {
    sprk = cmd.substring(9).toInt();  
  }
  else if (cmd.startsWith("SET_GAS:")) {
    gas = cmd.substring(8).toInt();  
  }
  else if (cmd.startsWith("AIR_ON:")) {
    air_on = cmd.substring(7).toInt();  
  }
  else if (cmd.startsWith("AIR_OFF:")) {
    air_off = cmd.substring(8).toInt();  
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
    setP = tempCurve[0];
  }
}

// ===================== TELEMETRY =====================
void TX(unsigned long now) {
  Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
  
  Serial.print(" TEST_TIMER:");
  if (currentState == STATE_TESTING) {
    Serial.print((now - testStartTime) / 1000.0, 2);
  } else {
    Serial.print(0.0, 2);
  }
  
  Serial.print(" T_SHIELD:"); Serial.print(t_Shld, 2);
  Serial.print(" T_HOT:"); Serial.print(t_Hot, 2);
  Serial.print(" T_COLD:"); Serial.print(t_Cold, 2);
  Serial.print(" TARGET:"); Serial.print(setP, 2);
  
  Serial.print(" OUTPUT:"); Serial.print(output, 2);

  Serial.print(" STATE:");
  switch (currentState) {
    case STATE_IDLE: Serial.println("IDLE"); break;
    case STATE_HOMING: Serial.println("HOMING"); break;
    case STATE_READY: Serial.println("READY"); break;
    case STATE_HEATING: Serial.println("HEATING"); break;
    case STATE_TESTING: Serial.println("TESTING"); break;
    case STATE_PURGE: Serial.println("PURGE"); break;
  }
}
