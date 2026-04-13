#include <SPI.h>
#include <Adafruit_MAX31855.h>

// ============================================================================
// HARDWARE SPI WIRING REMINDER (Teensy 4.1)
// SCK (Clock) -> Pin 13 (Shared by all sensors)
// DO  (MISO)  -> Pin 12 (Shared by all sensors)
// ============================================================================

// Define ONLY the individual Chip Select (CS) pins
const int PIN_CS_SHIELD = 39;  
const int PIN_CS_HOT    = 40;  
const int PIN_CS_COLD   = 41; 

// Instantiate using the Hardware SPI constructor (Only needs the CS pin)
Adafruit_MAX31855 thermoShield(PIN_CS_SHIELD);
Adafruit_MAX31855 thermoHot(PIN_CS_HOT);
Adafruit_MAX31855 thermoCold(PIN_CS_COLD);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); 

  Serial.println("=================================");
  Serial.println("HARDWARE SPI THERMOCOUPLE TEST");
  Serial.println("=================================");

  // CRITICAL: Give the MAX31855 chips time to physically power on
  delay(500);

  // Initialize the sensors
  thermoShield.begin();
  thermoHot.begin();
  thermoCold.begin();
}

void loop() {
  // Read the temperatures
  double tShield = thermoShield.readCelsius();
  double tHot    = thermoHot.readCelsius();
  double tCold   = thermoCold.readCelsius();

  // Print out the telemetry
  Serial.print("Shield: ");
  printTemp(tShield);
  
  Serial.print("  |  Hot: ");
  printTemp(tHot);
  
  Serial.print("  |  Cold: ");
  printTemp(tCold);
  
  Serial.println();

  // CRITICAL: MAX31855 requires at least 70-100ms between readings. 
  // 250ms guarantees a stable, valid reading without returning 'nan'.
  delay(250);
}

// Helper function to format the output nicely
void printTemp(double temp) {
  if (isnan(temp)) {
    Serial.print("FAULT");
  } else {
    Serial.print(temp);
    Serial.print(" C");
  }
}
