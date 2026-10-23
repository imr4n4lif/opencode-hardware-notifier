// OpenCode Hardware Notifier - Arduino receiver
//
// Listens on serial for status lines from the OpenCode plugin:
//   READY, WORKING, IDLE, ERROR, PROMPT
// and drives separate LEDs plus a buzzer.
//
// Wiring:
//   Pin 8  -> Buzzer +
//   Pin 9  -> Green LED   (WORKING)
//   Pin 10 -> Yellow LED  (IDLE / task complete)
//   Pin 11 -> Red LED     (ERROR)
//   Pin 12 -> Blue LED    (PROMPT)
//   GND    -> Buzzer - and all LED cathodes (via 220 ohm resistors)

const int GREEN_PIN = 9;   // WORKING
const int YELLOW_PIN = 10; // IDLE
const int RED_PIN = 11;    // ERROR
const int BLUE_PIN = 12;   // PROMPT

const int BUZZER_PIN = 8;

// A melody is a list of notes. A frequency of 0 is a rest.
struct Note {
  uint16_t frequency;
  uint16_t duration;
  uint16_t gap;
};

// Connected: bright rising triad.
const Note READY_MELODY[] = {
  { 660, 80, 20 },
  { 880, 80, 20 },
  { 1320, 160, 0 },
};

// Thinking / working: short, subtle rising motif.
const Note WORKING_MELODY[] = {
  { 440, 55, 15 },
  { 554, 55, 15 },
  { 659, 90, 0 },
};

// Complete: pleasant "ta-da" chime.
const Note IDLE_MELODY[] = {
  { 784, 80, 20 },
  { 1047, 80, 20 },
  { 1319, 170, 0 },
};

// Error: falling alarm.
const Note ERROR_MELODY[] = {
  { 392, 110, 30 },
  { 311, 110, 30 },
  { 262, 110, 30 },
  { 196, 260, 0 },
};

// Needs you: attention trill.
const Note PROMPT_MELODY[] = {
  { 1047, 70, 30 },
  { 1319, 70, 30 },
  { 1047, 70, 30 },
  { 1568, 180, 0 },
};

// Boot: tiny hardware chirp while the plugin connects.
const Note BOOT_MELODY[] = {
  { 988, 70, 20 },
  { 1319, 100, 0 },
};

// Unknown command: single acknowledgement beep.
const Note UNKNOWN_MELODY[] = {
  { 880, 180, 0 },
};

String inputString = "";
String currentState = "";

const Note *activeMelody = nullptr;
uint8_t melodyLength = 0;
uint8_t melodyIndex = 0;
unsigned long nextNoteAt = 0;
bool melodyPlaying = false;

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
  playMelody(BOOT_MELODY, sizeof(BOOT_MELODY) / sizeof(BOOT_MELODY[0]));

  Serial.println("Arduino ready");
}

void loop() {
  updateMelody();

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
    // Plugin connected: rising triad, all LEDs stay off
    playMelody(READY_MELODY, sizeof(READY_MELODY) / sizeof(READY_MELODY[0]));
  }
  else if (cmd == "WORKING") {
    // Thinking, executing, tool calls or subagents: green LED
    digitalWrite(GREEN_PIN, HIGH);
    playMelody(WORKING_MELODY, sizeof(WORKING_MELODY) / sizeof(WORKING_MELODY[0]));
  }
  else if (cmd == "IDLE") {
    // Whole task complete (or cancelled): yellow LED + completion chime
    digitalWrite(YELLOW_PIN, HIGH);
    playMelody(IDLE_MELODY, sizeof(IDLE_MELODY) / sizeof(IDLE_MELODY[0]));
  }
  else if (cmd == "ERROR") {
    // Task failed: red LED + falling alarm
    digitalWrite(RED_PIN, HIGH);
    playMelody(ERROR_MELODY, sizeof(ERROR_MELODY) / sizeof(ERROR_MELODY[0]));
  }
  else if (cmd == "PROMPT") {
    // Permission or question needs the user: blue LED + trill
    digitalWrite(BLUE_PIN, HIGH);
    playMelody(PROMPT_MELODY, sizeof(PROMPT_MELODY) / sizeof(PROMPT_MELODY[0]));
  }
  else if (cmd == "OFF" || cmd == "SHUTDOWN") {
    // Plugin unloaded / OpenCode closed: silently clear every indicator
    stopMelody();
  }
  else {
    // Unknown: single beep
    playMelody(UNKNOWN_MELODY, sizeof(UNKNOWN_MELODY) / sizeof(UNKNOWN_MELODY[0]));
  }
}

void setLeds(int green, int yellow, int red, int blue) {
  digitalWrite(GREEN_PIN, green);
  digitalWrite(YELLOW_PIN, yellow);
  digitalWrite(RED_PIN, red);
  digitalWrite(BLUE_PIN, blue);
}

void stopMelody() {
  melodyPlaying = false;
  noTone(BUZZER_PIN);
}

void playMelody(const Note *melody, uint8_t length) {
  stopMelody();
  activeMelody = melody;
  melodyLength = length;
  melodyIndex = 0;
  melodyPlaying = length > 0;
  nextNoteAt = millis();
}

// Non-blocking melody player so a new state can interrupt the current sound
// and serial input is never ignored while a tone is playing.
void updateMelody() {
  if (!melodyPlaying) return;

  unsigned long now = millis();
  if ((long)(now - nextNoteAt) < 0) return;

  if (melodyIndex >= melodyLength) {
    melodyPlaying = false;
    noTone(BUZZER_PIN);
    return;
  }

  const Note &note = activeMelody[melodyIndex++];

  if (note.frequency > 0) {
    tone(BUZZER_PIN, note.frequency, note.duration);
  } else {
    noTone(BUZZER_PIN);
  }

  nextNoteAt = now + note.duration + note.gap;
}
