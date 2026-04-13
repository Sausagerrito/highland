#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS 
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_METHANE       = 19;  
const int PIN_PWM_OXYGEN        = 18; 
const int PIN_RELAY_SOLENOID    = 10;  
const int PIN_RELAY_IGNITER     = 1;  

// --- Actuator / Stepper ---
const int PIN_STEP              = 15;  
const int PIN_DIR               = 14;  
const int PIN_OPT_SENSOR        = 7;  

// --- Thermocouples (Software SPI - Dedicated DO Pins) ---
const int PIN_CS_SHIELD         = 39;  
const int PIN_CS_HOT            = 40;  
const int PIN_CS_COLD           = 41; 

// ============================================================================
// HARDWARE OBJECTS & GLOBALS
// ============================================================================

Adafruit_MAX31855 thermoShield(PIN_CS_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_CS_HOT);
Adafruit_MAX31855 thermoCold(PIN_CS_COLD);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

// Plotting variables
bool isPlotting = false;
unsigned long lastPlotTime = 0;
const int PLOT_INTERVAL = 250; // Update plotter every 250ms (4 times a second)

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  
  // Wait briefly for serial monitor to open (helpful for Teensy)
  while (!Serial && millis() < 3000); 

  // Initialize Output Pins
  pinMode(PIN_PWM_METHANE, OUTPUT);
  pinMode(PIN_PWM_OXYGEN, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_IGNITER, OUTPUT);
  
  // Initialize Input Pins
  pinMode(PIN_OPT_SENSOR, INPUT_PULLUP);

  // Set safe default states (Everything OFF)
  digitalWrite(PIN_PWM_METHANE, LOW);
  digitalWrite(PIN_PWM_OXYGEN, LOW);
  digitalWrite(PIN_RELAY_SOLENOID, LOW);
  digitalWrite(PIN_RELAY_IGNITER, LOW);


  delay(500);
  // Initialize Sensors
  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();

  // Initialize Stepper settings
  stepper.setPinsInverted(true, true, false);
  stepper.setMinPulseWidth(20);
  stepper.setMaxSpeed(4000.0);
  stepper.setAcceleration(2000.0);

  Serial.println("\n=================================");
  Serial.println("HARDWARE DIAGNOSTIC SCRIPT READY");
  Serial.println("=================================");
  printHelp();
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  // Keep the stepper moving if a target was set
  stepper.run(); 

  // Stream data to Serial Plotter if enabled
  if (isPlotting && (millis() - lastPlotTime >= PLOT_INTERVAL)) {
    lastPlotTime = millis();
    
    double tShield = thermoShield.readCelsius();
    double tHot = thermoHot.readCelsius();
    double tCold = thermoCold.readCelsius();

    // The plotter handles NaN (Not a Number) poorly, so we default to 0 if there's a wiring fault
    if (isnan(tShield)) tShield = 0.0;
    if (isnan(tHot)) tHot = 0.0;
    if (isnan(tCold)) tCold = 0.0;

    // Print in Serial Plotter format: "Label1:Value1, Label2:Value2"
    Serial.print("Target1200C:1200, ");
    Serial.print("Shield:"); Serial.print(tShield); Serial.print(", ");
    Serial.print("Hot:"); Serial.print(tHot); Serial.print(", ");
    Serial.print("Cold:"); Serial.println(tCold);
  }

  // Check for Serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();         // Remove whitespace
    command.toUpperCase();  // Make case-insensitive

    if (command.length() > 0) {
      processCommand(command);
    }
  }
}

// ============================================================================
// COMMAND PARSER
// ============================================================================
void processCommand(String cmd) {
  // Only print command execution text if we aren't plotting (it messes up the graph's auto-scale)
  if (!isPlotting || cmd == "PLOT OFF") {
    Serial.print("Executing: ");
    Serial.println(cmd);
  }

  if (cmd == "HELP") {
    printHelp();
  } 
  
  // --- RELAY COMMANDS ---
  else if (cmd == "SOL ON") {
    digitalWrite(PIN_RELAY_SOLENOID, HIGH);
    if (!isPlotting) Serial.println("-> Solenoid Relay is ON");
  } 
  else if (cmd == "SOL OFF") {
    digitalWrite(PIN_RELAY_SOLENOID, LOW);
    if (!isPlotting) Serial.println("-> Solenoid Relay is OFF");
  } 
  else if (cmd == "IGN ON") {
    digitalWrite(PIN_RELAY_IGNITER, HIGH);
    if (!isPlotting) Serial.println("-> Igniter Relay is ON");
  } 
  else if (cmd == "IGN OFF") {
    digitalWrite(PIN_RELAY_IGNITER, LOW);
    if (!isPlotting) Serial.println("-> Igniter Relay is OFF");
  }

  // --- PWM / VALVE COMMANDS ---
  else if (cmd.startsWith("METHANE ")) {
    int val = cmd.substring(8).toInt();
    val = constrain(val, 0, 255);
    analogWrite(PIN_PWM_METHANE, val);
    if (!isPlotting) {
      Serial.print("-> Methane PWM set to ");
      Serial.println(val);
    }
  }
  else if (cmd.startsWith("OXYGEN ")) {
    int val = cmd.substring(7).toInt();
    val = constrain(val, 0, 255);
    analogWrite(PIN_PWM_OXYGEN, val);
    if (!isPlotting) {
      Serial.print("-> Oxygen PWM set to ");
      Serial.println(val);
    }
  }

  // --- STEPPER COMMANDS ---
  else if (cmd.startsWith("STEP ")) {
    long target = cmd.substring(5).toInt();
    stepper.moveTo(target);
    if (!isPlotting) {
      Serial.print("-> Stepper moving to position: ");
      Serial.println(target);
    }
  }
  else if (cmd == "STOP") {
    stepper.stop(); // Calculates a new target to decelerate to a stop
    if (!isPlotting) Serial.println("-> Stepper stopping...");
  }

  // --- SENSOR COMMANDS ---
  else if (cmd == "OPT") {
    int optState = digitalRead(PIN_OPT_SENSOR);
    if (!isPlotting) {
      Serial.print("-> Optical Sensor State: ");
      Serial.println(optState == LOW ? "LOW (Triggered)" : "HIGH (Open)");
    }
  }
  else if (cmd == "TEMP") {
    // Kept the manual one-shot read for standard Serial Monitor use
    if (!isPlotting) {
      Serial.println("-> Reading Thermocouples...");
      
      double tShield = thermoShield.readCelsius();
      double tHot = thermoHot.readCelsius();
      double tCold = thermoCold.readCelsius();

      Serial.print("   Shield Temp: ");
      if (isnan(tShield)) Serial.println("FAULT (Check wiring)"); else Serial.println(tShield);
      
      Serial.print("   Hot Temp:    ");
      if (isnan(tHot)) Serial.println("FAULT (Check wiring)"); else Serial.println(tHot);

      Serial.print("   Cold Temp:   ");
      if (isnan(tCold)) Serial.println("FAULT (Check wiring)"); else Serial.println(tCold);
    }
  }
  
  // --- PLOTTER COMMANDS ---
  else if (cmd == "PLOT ON") {
    isPlotting = true;
  }
  else if (cmd == "PLOT OFF") {
    isPlotting = false;
    Serial.println("-> Plotting stopped.");
  }

  // --- FALLBACK ---
  else {
    if (!isPlotting) Serial.println("-> ERROR: Unknown Command. Type HELP for options.");
  }
  
  if (!isPlotting) Serial.println(); // Blank line for readability
}

void printHelp() {
  Serial.println("\n--- COMMAND MENU ---");
  Serial.println("SOL ON       : Turn Solenoid Relay ON");
  Serial.println("SOL OFF      : Turn Solenoid Relay OFF");
  Serial.println("IGN ON       : Turn Igniter Relay ON");
  Serial.println("IGN OFF      : Turn Igniter Relay OFF");
  Serial.println("METHANE <val>: Set Methane PWM (0 to 255)");
  Serial.println("OXYGEN <val> : Set Oxygen PWM (0 to 255)");
  Serial.println("STEP <pos>   : Move stepper to absolute position (e.g., STEP 3000)");
  Serial.println("STOP         : Stop the stepper motor immediately");
  Serial.println("OPT          : Read optical sensor state");
  Serial.println("TEMP         : Single read of all three thermocouples (Text Format)");
  Serial.println("PLOT ON      : Start continuous data stream for Serial Plotter");
  Serial.println("PLOT OFF     : Stop continuous data stream");
  Serial.println("HELP         : Print this menu again");
  Serial.println("--------------------\n");
}
