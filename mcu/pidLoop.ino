#include <QuickPID.h>

//Pins
const int heatOutput = 11;

//PID Params
float Kp = 15.0;
float Ki = 0.5;
float Kd = 20.0;

//Temp Control
float currentTemp = 0.0;
float setpoint = 1200.0;
float controllerOutput = 0.0;

//Fixed sample time
QuickPID myPID(&currentTemp, &controllerOutput,
&setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

//Output smoothing
float smoothedOutput = 0.0;
const float smoothingFactor = 0.3;

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

const float championMaxBTU = 775.0; //per minute
const float championPropaneFlow = 8.0;
const float championOxygenFlow = 40.0;
const float btuToC = 0.0020;
const float maxHeatingRate = championMaxBTU * btuToC;

//Champion flame simulation
const int centerFireMaxPWM = 100; //Center has 6 jets
const int outerFireThreshold = 101; //Outer has 30 jets

void writeHeatPWM(int value) {
  smoothedOutput = smoothedOutput * (1.0 - smoothingFactor) + value * smoothingFactor;
  heatPWM = constrain((int)smoothedOutput, 0, 255);
  analogWrite(heatOutput, heatPWM);
}

//Temp sensor
float readTemperature() {
  static float simulatedTemp = 0.0;
  float ambient = 0.0;
  
  //Chamption torch two flame simulation
  float heating = 0.0;
  if (heatPWM <= centerFireMaxPWM) {
    heating = (heatPWM / (float)centerFireMaxPWM) * (maxHeatingRate * 0.3); //Center fire only
  } else {
    //Outer fire engaged
    float centerFireHeat = maxHeatingRate * 0.3;  //Center fire at max
    float outerFireHeat = ((heatPWM - centerFireMaxPWM) / (255.0 - centerFireMaxPWM)) * (maxHeatingRate * 0.7);
    heating = centerFireHeat + outerFireHeat;
  }  
  float cooling = (simulatedTemp - ambient) * 0.001;
  simulatedTemp += heating - cooling;
  simulatedTemp = constrain(simulatedTemp, 0.0, 1250.0);
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
      Serial.println("Terminal Paused for " + String(helpPauseDuration / 1000) + " seconds");
      Serial.println("Commands:");
      Serial.println("  start        - Start PID heating");
      Serial.println("  stop         - Stop heating");
      Serial.println("  set 1200      - Set temperature target");
      Serial.println("  kp 15        - Change P gain");
      Serial.println("  ki 0.5         - Change I gain");
      Serial.println("  kd 20        - Change D gain");
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
    
    Serial.print("Temp: ");
    if (currentTemp < 1000) Serial.print(" ");
    Serial.print(currentTemp, 1);
    Serial.print("°C");
    
    if (heatActive) {
      //Show which flame stage is active
      if (heatPWM <= centerFireMaxPWM) {
        Serial.print(" [Center Fire]");
      } else {
        Serial.print(" [Center+Outer]");
      }
      
      Serial.print(" / Target: ");
      Serial.print(setpoint, 0);
      Serial.print("°C");
      
      float error = currentTemp - setpoint;
      Serial.print(" (");
      if (error >= 0) Serial.print("+");
      Serial.print(error, 1);
      Serial.print("°C)");
      
      Serial.print(" | PWM: ");
      if (heatPWM < 100) Serial.print(" ");
      Serial.print(heatPWM);
      Serial.print("/255");
      
      //Estimated BTU output
      float estimatedBTU = (heatPWM / 255.0) * championMaxBTU;
      Serial.print(" | ~");
      Serial.print((int)estimatedBTU);
      Serial.print(" BTU/min");
      
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