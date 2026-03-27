const int valvePin = 1; 

void setup() {
  pinMode(valvePin, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); 
    
    if (command.equalsIgnoreCase("Open")) {
      digitalWrite(valvePin, HIGH);
    } 
    else if (command.equalsIgnoreCase("Close")) {
      digitalWrite(valvePin, LOW);
    } 
  }
}