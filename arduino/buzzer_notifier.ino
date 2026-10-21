// OpenCode Hardware Notifier - Arduino receiver
//
// Listens on serial for status lines from the OpenCode plugin:
//   READY, WORKING, IDLE, ERROR, PROMPT
// and drives separate LEDs plus a buzzer.
//
// Wiring:
//   Pin 8  -> Buzzer +
//   Pin 9  -> Green LED   (WORKING)
//   Pin 10 -> Yellow LED  (IDLE)
//   Pin 11 -> Red LED     (ERROR)
//   Pin 12 -> Blue LED    (PROMPT)
//   GND    -> Buzzer - and all LED cathodes (via 220 ohm resistors)

const int GREEN_PIN = 9;   // WORKING
const int YELLOW_PIN = 10; // IDLE
const int RED_PIN = 11;    // ERROR
const int BLUE_PIN = 12;   // PROMPT

const int BUZZER_PIN = 8;

String inputString = "";
String currentState = "";

void setup() {
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.setTimeout(100);

  // Startup: all LEDs off
  setLeds(LOW, LOW, LOW, LOW);

  // Startup chirp
  tone(BUZZER_PIN, 1000, 200);
  delay(250);
  tone(BUZZER_PIN, 1500, 200);
  Serial.println("Arduino ready");
}

void loop() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        handleCommand(inputString);
      }
      inputString = "";
    } else {
      inputString += inChar;
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  Serial.print("Received: ");
  Serial.println(cmd);

  // Ignore repeated identical state - no redundant buzzing/LED changes
  if (cmd == currentState) {
    Serial.println("(unchanged, ignoring)");
    return;
  }
  currentState = cmd;

  // Turn all LEDs off first
  setLeds(LOW, LOW, LOW, LOW);

  if (cmd == "READY") {
    // Plugin connected: double beep
    beep(400, 100);
    delay(100);
    beep(400, 100);
  }
  else if (cmd == "WORKING") {
    // OpenCode processing: long medium beep + GREEN LED solid
    digitalWrite(GREEN_PIN, HIGH);
    tone(BUZZER_PIN, 800, 500);
  }
  else if (cmd == "IDLE") {
    // Done or canceled: short high beep + YELLOW LED solid
    digitalWrite(YELLOW_PIN, HIGH);
    beep(1200, 150);
  }
  else if (cmd == "ERROR") {
    // Error: 3 fast low beeps + RED LED solid
    digitalWrite(RED_PIN, HIGH);
    for (int i = 0; i < 3; i++) {
      beep(600, 150);
      delay(100);
    }
  }
  else if (cmd == "PROMPT") {
    // Needs input: double ascending beep + BLUE LED solid
    digitalWrite(BLUE_PIN, HIGH);
    beep(1000, 150);
    delay(100);
    beep(1400, 150);
  }
  else {
    // Unknown: single beep
    beep(800, 300);
  }
}

void setLeds(int green, int yellow, int red, int blue) {
  digitalWrite(GREEN_PIN, green);
  digitalWrite(YELLOW_PIN, yellow);
  digitalWrite(RED_PIN, red);
  digitalWrite(BLUE_PIN, blue);
}

void beep(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
  delay(duration);
  noTone(BUZZER_PIN);
}
