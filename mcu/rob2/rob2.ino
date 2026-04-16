#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

const int METH = 19, OX = 18;
const int SOL = 10, IGN = 1;

const int STEP = 15, DIR = 14, OPT = 0;

const int SHLD = 41, HOT = 40, COLD = 39;

float Kp = 0.5, Ki = 0.15, Kd = 0.2;
float t_Shld = 25, t_Hot = 25, t_Cold = 25;

float output = 0, t_Active = 25, setP = 1200;

QuickPID gasPID(&t_Active, &output, &setP, Kp, Ki, Kd, QuickPID::Action::direct);

Adafruit_MAX31855 tShld(SHLD);
Adafruit_MAX31855 tHot(HOT);
Adafruit_MAX31855 tCold(COLD);

AccelStepper stepper(AccelStepper::DRIVER, STEP, DIR);

const float SPEED = 3000.0;
const int SPRK = 3000;
const int GAS = 3000;

// Changed to unsigned long to prevent millis() overflow
unsigned long last = 0; 
const int sDelay = 250;
const int duration = 30;

void setup() {
  Serial.begin(115200);

  pinMode(METH, OUTPUT);
  pinMode(OX, OUTPUT);
  pinMode(SOL, OUTPUT);
  pinMode(IGN, OUTPUT);
  pinMode(OPT, INPUT_PULLUP);

  analogWrite(METH, 0);
  analogWrite(OX, 0);
  digitalWrite(IGN, LOW);
  digitalWrite(SOL, LOW);

  tShld.begin();
  tHot.begin();
  tCold.begin();

  //stepper.setPinsInverted(false, false, false);
  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(SPEED);
  stepper.setAcceleration(8000);

  gasPID.SetOutputLimits(0, 255);
  gasPID.SetMode(QuickPID::Control::manual);
}

void loop() {
  // Changed to unsigned long to prevent millis() overflow
  unsigned long now = millis();

  stepper.run();
  RX();

  if (now - last >= sDelay) {
    last = now;
    
    tcu();
    gasPID.Compute();
    analogWrite(METH, (int)output);
    analogWrite(OX, (int)output);
    TX();
  }
}

void RX(){
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "CMD:HOME") {
    stepper.setSpeed(-3000);
    int count = 0;
    while (count < 10) {
      stepper.runSpeed();
      if (digitalRead(OPT) == HIGH) {
        count++;
      } else {
        count = 0;
      }
    }
    stepper.setCurrentPosition(0);
  }

  else if (cmd == "CMD:SHIELD") {
    stepper.moveTo(6100);
  }

  else if (cmd == "CMD:START") {
    analogWrite(METH, 255);
    analogWrite(OX, 100);
    digitalWrite(SOL, HIGH);
    delay(GAS);
    digitalWrite(IGN, HIGH);
    delay(SPRK);
    digitalWrite(IGN, LOW);
    tcu();
    output = 255;
    gasPID.SetMode(QuickPID::Control::automatic);
  }

  else if (cmd == "CMD:STOP") {
    gasPID.SetMode(QuickPID::Control::manual);
    analogWrite(METH, 0);
    analogWrite(OX, 0);
    digitalWrite(SOL, LOW);
  }
}

void tcu() {
  float t_rawShld = tShld.readCelsius();
  float t_rawHot = tHot.readCelsius();
  float t_rawCold = tCold.readCelsius();

  // Corrected the variable name here
  if (!isnan(t_rawShld)) {
    t_Shld = t_rawShld;
    t_Active = t_rawShld; 
  }
  
  if (!isnan(t_rawHot)) {
    t_Hot = t_rawHot;
  }
  
  if (!isnan(t_rawCold)) {
    t_Cold = t_rawCold;
  }
}

void TX() {
  Serial.print("SHLD:"); Serial.print(t_Shld, 2);
  Serial.print(",");
  Serial.print("HOT:"); Serial.print(t_Hot, 2);
  Serial.print(",");
  Serial.print("COLD:"); Serial.print(t_Cold, 2);
  Serial.println();
}
