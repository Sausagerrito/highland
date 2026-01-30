// =======================================================
// Teensy Serial Thermocouple Simulator Receiver
// Expects lines like:
//   T,123.45,124.10,122.98\n
// =======================================================
// hello


#include <Arduino.h>

static const uint32_t SERIAL_BAUD = 115200;
static const uint8_t  NUM_TC = 3;
int board_led = 13;
float t1, t2, t3;

// Parsed thermocouple values (°C)
float thermocouples[NUM_TC];

// Buffer for incoming serial line
static const size_t LINE_BUF_SIZE = 64;
char lineBuffer[LINE_BUF_SIZE];
size_t lineIndex = 0;

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(led, OUTPUT);

  // Wait for USB serial (important on Teensy)
  while (!Serial && millis() < 3000) {
  }

  Serial.println("Teensy thermocouple receiver ready");
}

void processLine(const char* line) {

  int parsed = sscanf(line, "T,%f,%f,%f", &t1, &t2, &t3);

  if (parsed == NUM_TC) {
    thermocouples[0] = t1;
    thermocouples[1] = t2;
    thermocouples[2] = t3;

    // ---- Use thermocouples[] as if they were real sensors ----
    Serial.print("TCs: ");
    for (uint8_t i = 0; i < NUM_TC; i++) {
      Serial.print(thermocouples[i], 2);
      if (i < NUM_TC - 1) Serial.print(", ");
    }
    Serial.println(" °C");
  } else {
    Serial.print("Parse error: ");
    Serial.println(line);
  }
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();

    // End-of-line → process
    if (c == '\n') {
      lineBuffer[lineIndex] = '\0';
      processLine(lineBuffer);
      lineIndex = 0;
    }
    // Ignore carriage return
    else if (c != '\r') {
      if (lineIndex < LINE_BUF_SIZE - 1) {
        lineBuffer[lineIndex++] = c;
      } else {
        // Overflow → reset buffer
        lineIndex = 0;
      }
    }
  }

  // ---- Your normal loop code goes here ----
  if (t1 >= 1100) {
    digitalWrite(board_led, HIGH);
  }
  else {
    digitalWrite(board_led, LOW);
  }

  // Control logic, state machines, etc.
}
