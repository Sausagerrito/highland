#include <QuickPID.h>

//Pins
const int heatOutput = 11;

//PID Params
float Kp = 5.0;
float Ki = 0.15;
float Kd = 40.0;

//Temp Control

float controllerOutput = 0.0;



//Output smoothing
float smoothedOutput = 0.0;
const float smoothingFactor = 0.1;

//State variables
bool idleActive = true;
bool heatActive = false;
bool setpointReached = false;
bool coolingActive = false;
float tcu1 = 0.0;
float tcu2 = 0.0;
float tcu3 = 0.0;
float setpoint = 1200.0;

//Fixed sample time
QuickPID myPID(&tcu1, &controllerOutput,
&setpoint, Kp, Ki, Kd, QuickPID::Action::direct);

//Linear actuator variables
bool actuatorMoving = false;
bool actuatorAtIdle = true;
unsigned long actuatorStartTime = 0;
const unsigned long actuatorMoveTime = 2000;
float actuatorPosition = 0.0;

//Debug variables
unsigned long lastSampleTime = 0;
unsigned long lastDebugTime = 0;
const unsigned long sampleTime = 500;
const unsigned long debugInterval = 250;

int heatPWM = 0;

//Champion torch variables
const float championMaxBTU = 775.0; //per minute
const float championPropaneFlow = 8.0;
const float championOxygenFlow = 40.0;
const float btuToC = 0.25;
const float maxHeatingRate = championMaxBTU * btuToC;

//Champion flame simulation
const int centerFireMaxPWM = 100; //Center has 6 jets
const int outerFireThreshold = 101; //Outer has 30 jets

float lastHeatingInput = 0.0;

String getMachineState() {
  if (heatActive) {
    return tcu1 >= setpoint - 2.0 ? "heat" : "warming";
  } else if (coolingActive) {
    return "cooling";
  } else {
    return "idle";
  }
}

String getShieldStatus() {
  if (actuatorMoving) {
    return actuatorAtIdle ? "moving to idle" : "moving to engaged";
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
  if (!actuatorAtIdle || (actuatorMoving && !actuatorAtIdle)) {
    actuatorMoving = true;
    actuatorAtIdle = true;
    actuatorStartTime = millis();
  }
}

void moveActuatorAway() {
  if (actuatorAtIdle || (actuatorMoving && actuatorAtIdle)) {
    actuatorMoving = true;
    actuatorAtIdle = false;
    actuatorStartTime = millis();
  }
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
    float centerFireHeat = maxHeatingRate * 0.5;  //Center fire at max
    float outerFireHeat = ((heatPWM - centerFireMaxPWM) / (255.0 - centerFireMaxPWM)) * (maxHeatingRate * 0.7);
    heating = centerFireHeat + outerFireHeat;
  }  

  float cooling = (simulatedTemp - ambient) * 0.04;
  simulatedTemp += (heating - cooling) * (sampleTime / 1000.0);
  simulatedTemp = constrain(simulatedTemp, 0.0, 1250.0);
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


  static unsigned long lastSerialTime = 0;
  if (millis() - lastSerialTime >= 1000) {
    lastSerialTime = millis();

    // Serial.print("Current Temp: ");
    // Serial.println(tcu1, 2);
    // Serial.print("Setpoint Temp: ");
    // Serial.println(setpoint, 2);
    // Serial.println("Machine State: " + getMachineState());
    // Serial.println("Shield Status: " + getShieldStatus());
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.replace("\r", "");
    command.trim();

    if (command == "start") {
      if (!heatActive) {
        startHeating();
      }
    }
    else if (command == "stop") {
      if (heatActive) {
        stopHeating();
      }
    }

    //Command for setpoint temp
    else if (command.startsWith("set temp ")) {
      String valueStr = command.substring(9);
      valueStr.trim();
      float newSetPoint = valueStr.toFloat();
      if (newSetPoint > 0.0 && newSetPoint <= 1250.0) {
        setpoint = newSetPoint;
        //Serial.println("Setpoint updated to: " + String(setpoint, 2));
        myPID.Reset();
      } else {
       //Serial.println("Invalid setpoint value");
      }
    }
  }

  unsigned long now = millis();
  updateActuator();
  
  //Run temp control at fixed intervals
  if (now - lastSampleTime >= sampleTime) {
    lastSampleTime = now;
    
    //Read temp
    tcu1 = readTemperature();

    Serial.print("TCU1=");
    Serial.println(tcu1, 2);

    Serial.print("TCU2=");
    Serial.println(tcu2, 2);

    Serial.print("TCU3=");  
    Serial.println(tcu3, 2);
    
    //Check setpoint reached
    if (heatActive && !setpointReached && tcu1 >= setpoint - 2.0) {
      setpointReached = true;
      moveActuatorAway();
    }
    
    //Safety limit
    if (tcu1 > 1251.0) {
      emergencyStop();
      return;
    }
    
    //Check if cooling should transition to idle
    if (coolingActive && tcu1 <= 0.1) {
      coolingActive = false;
      idleActive = true;
    }
    
    //State machine
    if (heatActive) {
      myPID.Compute();

      //Braking to prevent overshoot
      float finalOutput = controllerOutput;
      if (tcu1 > (setpoint - 30.0) && tcu1 < setpoint) {
        finalOutput = constrain(controllerOutput, 0, 100); //Cap power in final approach
      }
      if (tcu1 >= setpoint) {
        finalOutput = 0; //Cut power once target reached
      }
      writeHeatPWM((int)finalOutput);
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