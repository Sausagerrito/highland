float t_shield = 25.0;
float t_hot = 25.0;
float t_cold = 25.0;
bool is_heating = false;

unsigned long last_sim_time = 0;
unsigned long last_tx_time = 0;

void setup() {
  // Must match the Rust app
  Serial.begin(115200);
}

void loop() {
  unsigned long now = millis();

  // --------------------------------------------------------
  // 1. READ COMMANDS FROM RUST
  // --------------------------------------------------------
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "CMD:START") {
      is_heating = true;
      Serial.println("Simulator: Ignition Sequence Started!");
    } 
    else if (cmd == "CMD:HOME") {
      is_heating = false;
      Serial.println("Simulator: Actuator Moving Left");
    } 
    else if (cmd == "CMD:SHIELD") {
      Serial.println("Simulator: Actuator Moving Right");
    } 
    else if (cmd == "CMD:STOP") {
      is_heating = false;
      Serial.println("Simulator: Emergency Stop!");
    }
  }

  // --------------------------------------------------------
  // 2. SIMULATE PHYSICS (Runs every 10ms)
  // --------------------------------------------------------
  if (now - last_sim_time >= 10) {
    last_sim_time = now;
    float time_sec = now / 1000.0;

    if (is_heating) {
      // Add sine-wave noise to make the graph look alive
      float noise = sin(time_sec * 10.0) * 2.0;
      
      t_shield += 8.0 + noise;
      if (t_shield > 1400.0) t_shield = 1400.0;
      
      t_hot += 3.0 + noise;
      if (t_hot > 1400.0) t_hot = 1400.0;
      
      t_cold += 0.5;
      if (t_cold > 500.0) t_cold = 500.0;
    } else {
      // Cool down toward ambient (25.0)
      t_shield -= 3.0;
      if (t_shield < 25.0) t_shield = 25.0;
      
      t_hot -= 1.5;
      if (t_hot < 25.0) t_hot = 25.0;
      
      t_cold -= 0.2;
      if (t_cold < 25.0) t_cold = 25.0;
    }
  }

  // --------------------------------------------------------
  // 3. SEND TELEMETRY TO RUST (Runs every 100ms)
  // --------------------------------------------------------
  if (now - last_tx_time >= 100) {
    last_tx_time = now;
    
    Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
    Serial.print(" T_SHIELD:"); Serial.print(t_shield, 2);
    Serial.print(" T_HOT:"); Serial.print(t_hot, 2);
    Serial.print(" T_COLD:"); Serial.print(t_cold, 2);
    Serial.println(); // Newline triggers the Rust parser
  }
}
