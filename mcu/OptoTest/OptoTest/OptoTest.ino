void setup() {
  Serial.begin(115200);
  pinMode(7, INPUT_PULLUP);
}

void loop() {
  Serial.println(digitalRead(7));
  delay(200);
}
