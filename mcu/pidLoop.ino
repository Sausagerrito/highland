#include <QuickPID.h>

//Pins
const int heatOutput = 11;

//PID Params
float Kp = 40.0;
float Ki = 3.0;
float Kd = 40.0;

//Temp Control
float currentTemp = 0.0;
float setpoint = 1200.0;
float controllerOutput = 0.0;

//Fixed sample time
QuickPID myPID(&currentTemp, &controllerOutput,
&setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

//State variables
bool idleActive = true;
bool heatActive = false;

unsigned long lastSampleTime = 0;
unsigned long lastDebugTime = 0;
const unsigned long sampleTime = 50;
const unsigned long debugInterval = 250;

unsigned long helpPause = 0;
const unsigned long helpPauseDuration = 8000;

int heatPWM = 0;

void writeHeatPWM(int value) {
  heatPWM = constrain(value, 0, 255);
  analogWrite(heatOutput, heatPWM);
}

//Temp sensor
float readTemperature() {
  static float simulatedTemp = 0.0;
  float ambient = 0.0;
  float heating = (heatPWM / 255.0) * 40.0;
  float cooling = (simulatedTemp - ambient) * 0.005;

  simulatedTemp += heating - cooling;
  simulatedTemp = constrain(simulatedTemp, 0.0, 1200.0);
  return simulatedTemp;
}

void setup() {
  pinMode(heatOutput, OUTPUT);

  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    //Wait for serial
  }

  myPID.SetProportionalMode(QuickPID::pMode::pOnError);
  myPID.SetDerivativeMode(QuickPID::dMode::dOnError);
  myPID.SetOutputLimits(0, 255);
  myPID.SetSampleTimeUs(sampleTime * 1000);
  myPID.SetMode(QuickPID::Control::manual);
  
  //Start in idle state
  writeHeatPWM(0);

  Serial.println("Commands: start, stop, set XXX, kp X, ki X, kd X, help");
  Serial.println("Current state: Idle");
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.replace("\r", "");
    command.trim();

    Serial.print("> ");
    Serial.println(command);

    if (command == "start") {
      if (idleActive && !heatActive) {
        startHeating();
      } else {
        Serial.println("Already running");
      }
    }
        else if (command == "stop") {
      if (heatActive) {
        stopHeating();
      } else {
        Serial.println("Already stopped");
      }
    }
      else if (command.startsWith("set ")) {
      float newSetpoint = command.substring(4).toFloat();
      if (newSetpoint >= 0 && newSetpoint <= 1250) {
        setpoint = newSetpoint;
        Serial.print("Target: ");
        Serial.print(setpoint);
        Serial.println("°C");

        if (heatActive) {
          myPID.Reset();
        }
      }
    }
    else if (command.startsWith("kp ")) {
      Kp = command.substring(3).toFloat();
      myPID.SetTunings(Kp, Ki, Kd);
      myPID.Reset();
      Serial.print("Kp = ");
      Serial.println(Kp);
    }
      else if (command.startsWith("ki ")) {
      Ki = command.substring(3).toFloat();
      myPID.SetTunings(Kp, Ki, Kd);
      myPID.Reset(); 
      Serial.print("Ki = ");
      Serial.println(Ki);
    }
    else if (command.startsWith("kd ")) {
      Kd = command.substring(3).toFloat();
      myPID.SetTunings(Kp, Ki, Kd);
      myPID.Reset();
      Serial.print("Kd = ");
      Serial.println(Kd);
    }

    else if (command == "help") {
      helpPause = millis() + helpPauseDuration;
      Serial.println("Terminal Paused for " + String(helpPauseDuration) + " milliseconds");
      Serial.println("Commands:");
      Serial.println("  start        - Start PID heating");
      Serial.println("  stop         - Stop heating");
      Serial.println("  set 180      - Set temperature target");
      Serial.println("  kp 35        - Change P gain");
      Serial.println("  ki 3         - Change I gain");
      Serial.println("  kd 40        - Change D gain");
      Serial.println("  help         - Show commands");
    }
    else if (command != "") {
      Serial.println("Unknown command. Type 'help' for list.");
    }
  }

  unsigned long now = millis();
  
  //Run temp control at fixed intervals
  if (now - lastSampleTime >= sampleTime) {
    lastSampleTime = now;
    
    //Read temp
    currentTemp = readTemperature();
    
    //Safety limit
    if (currentTemp > 1251.0) {
      emergencyStop();
      return;
    }
    
    //State machine
    if (heatActive) {
      myPID.Compute();
      writeHeatPWM((int)controllerOutput);
    }
    else if (idleActive) {
      //No heating in idle
      writeHeatPWM(0);
    }
  }

  //Debug output
  if (now - lastDebugTime >= debugInterval && now >= helpPause) {
    lastDebugTime = now;
    
    //Format output
    Serial.print("Temp: ");
    if (currentTemp < 100) Serial.print(" ");
    Serial.print(currentTemp, 1);
    Serial.print("°C");
    
    if (heatActive) {
      Serial.print(" / Target: ");
      Serial.print(setpoint, 0);
      Serial.print("°C");
      
      float error = currentTemp - setpoint;
      Serial.print(" (");
      if (error >= 0) Serial.print("+");
      Serial.print(error, 1);
      Serial.print("°C)");
      
      Serial.print(" | PID: ");
      if (controllerOutput < 100) Serial.print(" ");
      Serial.print((int)controllerOutput);
      Serial.print("/255");
      
      //Show approximate heating rate
      static float lastTemp = currentTemp;
      static unsigned long lastCalcTime = now;
      if (now - lastCalcTime >= 1000) {
        float rate = (currentTemp - lastTemp) / ((now - lastCalcTime) / 1000.0);
        Serial.print(" | Rate: ");
        Serial.print(rate, 1);
        Serial.print("°C/s");
        lastTemp = currentTemp;
        lastCalcTime = now;
      }
    } else {
      Serial.print("");
    }
    Serial.println();
  }
}

void startHeating() {
  idleActive = false;
  heatActive = true;
  
  //Reset PID and switch to automatic
  myPID.Reset();
  myPID.SetMode(QuickPID::Control::automatic);

  Serial.println("\n=== PID Heating Started ===");
  Serial.print("Target: ");
  Serial.print(setpoint);
  Serial.println("°C");
  Serial.println();
}

void stopHeating() {
  heatActive = false;
  idleActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);
  
  Serial.println("\n=== Heating Stopped ===");
  Serial.println("Returned to idle state");
  Serial.println();
}

void emergencyStop() {
  heatActive = false;
  idleActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);
  
  Serial.println("\n=== Emergency Stop ===");
  Serial.println("Temperature exceeded 1250°C");
  Serial.println("System reset to idle state");
}