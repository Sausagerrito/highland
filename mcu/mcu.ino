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
bool setpointReached = false;
bool coolingActive = false;

//Linear actuator variables
bool actuatorMoving = false;
bool actuatorAtIdle = true;
unsigned long actuatorStartTime = 0;
const unsigned long actuatorMoveTime = 2000;
float actuatorPosition = 0.0;

//Debug variables
unsigned long lastSampleTime = 0;
unsigned long lastDebugTime = 0;
const unsigned long sampleTime = 50;
const unsigned long debugInterval = 250;

int heatPWM = 0;

//Champion torch variables
const float championMaxBTU = 775.0; //per minute
const float championPropaneFlow = 8.0;
const float championOxygenFlow = 40.0;
const float btuToC = 0.00203;
const float maxHeatingRate = championMaxBTU * btuToC;

//Champion flame simulation
const int centerFireMaxPWM = 100; //Center has 6 jets
const int outerFireThreshold = 101; //Outer has 30 jets

String getMachineState() {
  if (heatActive) {
    return currentTemp >= setpoint - 2.0 ? "heat" : "warming";
  } else if (coolingActive) {
    return "cooling";
  } else {
    return "idle";
  }
}

String getShieldStatus() {
  if (actuatorMoving) {
    return actuatorAtIdle ? "moving_to_idle" : "moving_to_engaged";
  } else {
    return actuatorAtIdle ? "idle" : "engaged";
  }
}

void writeHeatPWM(int value) {
  smoothedOutput = smoothedOutput * (1.0 - smoothingFactor) + value * smoothingFactor;
  heatPWM = constrain((int)smoothedOutput, 0, 255);
  analogWrite(heatOutput, heatPWM);
}

//Linear actuator simulation
void updateActuator() {
  if (!actuatorMoving) return;

  unsigned long elapsed = millis() - actuatorStartTime;
  if (elapsed >= actuatorMoveTime) {
    actuatorMoving = false;
    actuatorPosition = actuatorAtIdle ? 0.0 : 100.0;
  } else {
    float progress = (float)elapsed / actuatorMoveTime;
    actuatorPosition = actuatorAtIdle ? progress * 100.0 : 100.0 - (progress * 100.0);
  }
}

void moveActuatorToIdle() {
  actuatorMoving = true;
  actuatorAtIdle = true;
  actuatorStartTime = millis();
}

void moveActuatorAway() {
  actuatorMoving = true;
  actuatorAtIdle = false;
  actuatorStartTime = millis();
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
  float coolingCoeff = (heatPWM == 0) ? 0.005 : 0.001;
  float cooling = (simulatedTemp - ambient) * coolingCoeff;
  simulatedTemp += heating - cooling;
  simulatedTemp = constrain(simulatedTemp, 0.0, 9999.0);
  return simulatedTemp;
}

void setup() {
  pinMode(heatOutput, OUTPUT);

  Serial.begin(9600);

  myPID.SetProportionalMode(QuickPID::pMode::pOnError);
  myPID.SetDerivativeMode(QuickPID::dMode::dOnError);
  myPID.SetOutputLimits(0, 255);
  myPID.SetSampleTimeUs(sampleTime * 1000);
  myPID.SetMode(QuickPID::Control::manual);
  
  //Start in idle state
  writeHeatPWM(0);
  actuatorAtIdle = true;
  actuatorPosition = 0.0;
}

void loop() {
  Serial.println(currentTemp, 2);
  Serial.println(getMachineState());
  Serial.println(getShieldStatus());

  delay(1000);

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.replace("\r", "");
    command.trim();

    if (command == "start") {
      if (idleActive && !heatActive) {
        startHeating();
      }
    }
    else if (command == "stop") {
      if (heatActive) {
        stopHeating();
      }
    }
  }

  unsigned long now = millis();
  
  updateActuator();
  
  //Run temp control at fixed intervals
  if (now - lastSampleTime >= sampleTime) {
    lastSampleTime = now;
    
    //Read temp
    currentTemp = readTemperature();
    
    //Check setpoint reached
    if (heatActive && !setpointReached && currentTemp >= setpoint - 2.0) {
      setpointReached = true;
      moveActuatorAway();
    }
    
    //Safety limit
    if (currentTemp > 1251.0) {
      emergencyStop();
      return;
    }
    
    //Check if cooling should transition to idle
    if (coolingActive && currentTemp <= 0.1) {
      coolingActive = false;
      idleActive = true;
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
    else if (coolingActive) {
      //No heating while cooling
      writeHeatPWM(0);
    }
  }  
}

void startHeating() {
  idleActive = false;
  heatActive = true;
  coolingActive = false;
  setpointReached = false;
  
  //Reset PID and switch to automatic
  myPID.Reset();
  myPID.SetMode(QuickPID::Control::automatic);
}

void stopHeating() {
  heatActive = false;
  idleActive = false;
  coolingActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);
  
  moveActuatorToIdle();
}

void emergencyStop() {
  heatActive = false;
  idleActive = false;
  coolingActive = true;
  
  myPID.SetMode(QuickPID::Control::manual);
  writeHeatPWM(0);

  moveActuatorToIdle();
}
