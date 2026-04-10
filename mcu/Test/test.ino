#include <AccelStepper.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// ============================================================================
// PIN DEFINITIONS 
// ============================================================================

// --- Relays & Valves ---
const int PIN_PWM_METHANE       = 2;  
const int PIN_PWM_OXYGEN        = 11; 
const int PIN_RELAY_SOLENOID    = 3;  
const int PIN_RELAY_IGNITER     = 4;  

// --- Actuator / Stepper ---
const int PIN_STEP              = 5;  
const int PIN_DIR               = 6;  
const int PIN_OPT_SENSOR        = 7;  

// --- Thermocouples (Software SPI - Dedicated DO Pins) ---
const int PIN_SCK_SHARED        = 13; 
const int PIN_CS_SHIELD         = 8;  
const int PIN_DO_SHIELD         = 12; 
const int PIN_CS_HOT            = 9;  
const int PIN_DO_HOT            = 24; 
const int PIN_CS_COLD           = 10; 
const int PIN_DO_COLD           = 25; 

// ============================================================================
// HARDWARE OBJECTS
// ============================================================================

Adafruit_MAX31855 thermoShield(PIN_SCK_SHARED, PIN_CS_SHIELD, PIN_DO_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_SCK_SHARED, PIN_CS_HOT, PIN_DO_HOT);
Adafruit_MAX31855 thermoCold(PIN_SCK_SHARED, PIN_CS_COLD, PIN_DO_COLD);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

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

  // Initialize Sensors
  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();

  // Initialize Stepper settings
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
  Serial.print("Executing: ");
  Serial.println(cmd);

  if (cmd == "HELP") {
    printHelp();
  } 
  
  // --- RELAY COMMANDS ---
  else if (cmd == "SOL ON") {
    digitalWrite(PIN_RELAY_SOLENOID, HIGH);
    Serial.println("-> Solenoid Relay is ON");
  } 
  else if (cmd == "SOL OFF") {
    digitalWrite(PIN_RELAY_SOLENOID, LOW);
    Serial.println("-> Solenoid Relay is OFF");
  } 
  else if (cmd == "IGN ON") {
    digitalWrite(PIN_RELAY_IGNITER, HIGH);
    Serial.println("-> Igniter Relay is ON");
  } 
  else if (cmd == "IGN OFF") {
    digitalWrite(PIN_RELAY_IGNITER, LOW);
    Serial.println("-> Igniter Relay is OFF");
  }

  // --- PWM / VALVE COMMANDS ---
  else if (cmd.startsWith("METHANE ")) {
    int val = cmd.substring(8).toInt();
    val = constrain(val, 0, 255);
    analogWrite(PIN_PWM_METHANE, val);
    Serial.print("-> Methane PWM set to ");
    Serial.println(val);
  }
  else if (cmd.startsWith("OXYGEN ")) {
    int val = cmd.substring(7).toInt();
    val = constrain(val, 0, 255);
    analogWrite(PIN_PWM_OXYGEN, val);
    Serial.print("-> Oxygen PWM set to ");
    Serial.println(val);
  }

  // --- STEPPER COMMANDS ---
  else if (cmd.startsWith("STEP ")) {
    long target = cmd.substring(5).toInt();
    stepper.moveTo(target);
    Serial.print("-> Stepper moving to position: ");
    Serial.println(target);
  }
  else if (cmd == "STOP") {
    stepper.stop(); // Calculates a new target to decelerate to a stop
    Serial.println("-> Stepper stopping...");
  }

  // --- SENSOR COMMANDS ---
  else if (cmd == "OPT") {
    int optState = digitalRead(PIN_OPT_SENSOR);
    Serial.print("-> Optical Sensor State: ");
    Serial.println(optState == LOW ? "LOW (Triggered)" : "HIGH (Open)");
  }
  else if (cmd == "TEMP") {
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
  
  // --- FALLBACK ---
  else {
    Serial.println("-> ERROR: Unknown Command. Type HELP for options.");
  }
  
  Serial.println(); // Blank line for readability
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
  Serial.println("TEMP         : Read all three thermocouples");
  Serial.println("HELP         : Print this menu again");
  Serial.println("--------------------\n");
}
