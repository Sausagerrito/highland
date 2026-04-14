// --- SIMULATED STATE ---
enum SystemState { IDLE, HOMING, READY, HEATING, TESTING };
SystemState currentState = IDLE;

// --- TELEMETRY VARIABLES ---
float t_shield = 25.0;
float t_hot = 25.0;
float t_cold = 25.0;
float target_temp = 0.0;
float test_timer = 0.0;

// --- CONFIG VARIABLES ---
float test_duration = 600.0;
float temp_curve[5] = {1400.0, 1400.0, 1400.0, 1400.0, 1400.0};
float homing_timer = 0.0;

// --- TIMERS ---
unsigned long last_sim_time = 0;
unsigned long last_tx_time = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  unsigned long now = millis();
  float dt = (now - last_sim_time) / 1000.0; // Delta time in seconds

  // --------------------------------------------------------
  // 1. READ COMMANDS FROM RUST
  // --------------------------------------------------------
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "CMD:HOME") {
      currentState = HOMING;
      homing_timer = 0.0;
    } 
    else if (cmd == "CMD:START") {
      currentState = HEATING;
    } 
    else if (cmd == "CMD:START_SKIP") {
      currentState = TESTING;
      test_timer = 0.0;
    } 
    else if (cmd == "CMD:STOP") {
      currentState = IDLE;
      test_timer = 0.0;
    }
    else if (cmd.startsWith("SET_TIME:")) {
      test_duration = cmd.substring(9).toFloat();
    }
    else if (cmd.startsWith("SET_CURVE:")) {
      String data = cmd.substring(10);
      for (int i = 0; i < 4; i++) {
        int idx = data.indexOf(',');
        temp_curve[i] = data.substring(0, idx).toFloat();
        data = data.substring(idx + 1);
      }
      temp_curve[4] = data.toFloat();
    }
  }

  // --------------------------------------------------------
  // 2. SIMULATE PHYSICS & STATE MACHINE
  // --------------------------------------------------------
  if (now - last_sim_time >= 50) { 
    last_sim_time = now;
    
    // Add a tiny bit of noise to make the active flame look alive
    float noise = sin(now / 100.0) * 1.5;

    switch (currentState) {
      case IDLE:
      case READY:
        target_temp = temp_curve[0];
        test_timer = 0.0;
        // Everything cools down to ambient
        t_shield += (25.0 - t_shield) * 0.1 * dt;
        t_hot += (25.0 - t_hot) * 0.1 * dt;
        t_cold += (25.0 - t_cold) * 0.05 * dt;
        break;

      case HOMING:
        target_temp = temp_curve[0];
        homing_timer += dt;
        t_shield += (25.0 - t_shield) * 0.1 * dt;
        t_hot += (25.0 - t_hot) * 0.1 * dt;
        t_cold += (25.0 - t_cold) * 0.05 * dt;
        
        // Simulate a 2-second homing sequence
        if (homing_timer >= 2.0) {
          currentState = READY;
        }
        break;

      case HEATING:
        target_temp = temp_curve[0];
        
        // ONLY the shield gets hot.
        t_shield += (target_temp + 50.0 - t_shield) * 0.8 * dt;
        
        // Hot and Cold sides are protected, stay at ambient
        t_hot += (25.0 - t_hot) * 0.1 * dt;
        t_cold += (25.0 - t_cold) * 0.05 * dt;
        
        // Once shield stabilizes at target temp, whip it out of the way!
        if (t_shield >= target_temp - 2.0) {
          t_shield = target_temp; 
          currentState = TESTING;
          test_timer = 0.0;
          Serial.println("Simulator: Target hit! Shield retracting, test starting.");
        }
        break;

      case TESTING:
        test_timer += dt;

        // Interpolate the target temperature along the 5-point curve
        float progress = constrain(test_timer / test_duration, 0.0, 1.0);
        float scaled = progress * 4.0;
        int idx = (int)scaled;
        float frac = scaled - idx;

        if (idx < 4) {
          target_temp = temp_curve[idx] + (temp_curve[idx + 1] - temp_curve[idx]) * frac;
        } else {
          target_temp = temp_curve[4];
        }

        // 1. Shield is out of the fire, it cools down
        t_shield += (25.0 - t_shield) * 0.2 * dt; 
        
        // 2. Hot side takes the full blast of the flame, tracks target tightly
        t_hot += (target_temp + 150.0 - t_hot) * 0.8 * dt; 
        
        // 3. Cold side warms up slowly via conduction through the battery/sample
        t_cold += (target_temp * 0.2 - t_cold) * 0.02 * dt; 

        // Auto-stop when time runs out
        if (test_timer >= test_duration) {
          currentState = IDLE;
          test_timer = 0.0;
          Serial.println("Simulator: Time elapsed. Stopping test.");
        }
        break;
    }
  }

  // --------------------------------------------------------
  // 3. SEND TELEMETRY TO RUST (10x per second)
  // --------------------------------------------------------
  if (now - last_tx_time >= 100) {
    last_tx_time = now;

    String stateStr = "IDLE";
    if (currentState == HOMING) stateStr = "HOMING";
    else if (currentState == READY) stateStr = "READY";
    else if (currentState == HEATING) stateStr = "HEATING";
    else if (currentState == TESTING) stateStr = "TESTING";

    // Add the active noise only to whichever side is currently being blasted
    float reported_shield = t_shield;
    float reported_hot = t_hot;
    float noise = sin(now / 100.0) * 1.5;
    
    if (currentState == HEATING) reported_shield += noise;
    if (currentState == TESTING) reported_hot += noise;

    Serial.print("SYS_TIME:"); Serial.print(now / 1000.0, 2);
    Serial.print(" TEST_TIMER:"); Serial.print(test_timer, 2);
    Serial.print(" T_SHIELD:"); Serial.print(reported_shield, 2);
    Serial.print(" T_HOT:"); Serial.print(reported_hot, 2);
    Serial.print(" T_COLD:"); Serial.print(t_cold, 2);
    Serial.print(" TARGET:"); Serial.print(target_temp, 2);
    Serial.print(" STATE:"); Serial.println(stateStr);
  }
}