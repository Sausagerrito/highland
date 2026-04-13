#include <QuickPID.h>
#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

const int METH = 19, OX = 18;
const int SOL = 10, IGN = 1;

const int STEP = 15, DIR = 14, OPT = 7;

const int SHLD = 38, HOT = 40, COLD = 41;

float Kp = 5.0, Ki = 0.15, Kd = 40.0;
float t_Shld = 25, t_Hot = 25, t_Cold = 25;

float output = 0, t_Active = 25, setP = 1000;

QuickPID gasPID(&t_Active, &output, &setP, Kp, Ki, Kd, QuickPID::Action::direct);

Adafruit_MAX31855 tShld(SHLD);
Adafruit_MAX31855 tHot(HOT);
Adafruit_MAX31855 tCold(COLD);

AccelStepper stepper(AccelStepper::DRIVER, STEP, DIR);

const float SPEED = 3000.0;
const int SPRK = 5000;
int last = 0;
const int sDelay = 250;
const int duration = 30;

void setup() {
  Serial.begin(115200);

  pinMode(METH, OUTPUT);
  pinMode(OX, OUTPUT);
  pinMode(SOL, OUTPUT);
  pinMode(IGN, OUTPUT);
  pinMode(OPT, INPUT_PULLUP);

  tShld.begin();
  tHot.begin();
  tCold.begin();

  stepper.setPinsInverted(true, false, false);
  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(SPEED);
  stepper.setAcceleration(8000);

  gasPID.SetOutputLimits(0, 255 / 6);
  gasPID.SetMode(QuickPID::Control::manual);
}

void loop() {
  int now = millis();

  stepper.run();
  RX();

  if (now - last >= sDelay) {
    last = now;
    
    tcu();
    gasPID.Compute();
    analogWrite(METH, (int)output);
    analogWrite(OX, 6 * (int)output);
    TX();
  }
}

void RX(){
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "CMD:HOME") {
    stepper.moveTo(0);
  }

  else if (cmd == "CMD:SHIELD") {
    stepper.moveTo(6100);
  }
  else if (cmd == "CMD:START") {
    digitalWrite(SOL, HIGH);
    delay(2000);
    digitalWrite(IGN, HIGH);
    delay(SPRK);
    digitalWrite(IGN, LOW);

    gasPID.SetMode(QuickPID::Control::automatic);
  }
}

void tcu() {
  t_Shld = tShld.readCelsius();
  t_Hot = tHot.readCelsius();
  t_Cold = tCold.readCelsius();
  t_Active = t_Shld;
}

void TX() {
  Serial.print("SHLD:"); Serial.print(t_Shld, 2);
  Serial.print("HOT:"); Serial.print(t_Hot, 2);
  Serial.print("COLD:"); Serial.print(t_Cold, 2);
  Serial.println();
}
