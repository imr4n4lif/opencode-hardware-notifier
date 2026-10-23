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

// Connected: bright rising major triad (chiptune power-up)
const Note READY_MELODY[] = {
  { 1047, 70, 15 },  // C6
  { 1319, 70, 15 },  // E6
  { 1568, 70, 15 },  // G6
  { 2093, 180, 0 },  // C7
};

// Working: short upward arpeggio, subtle but clear
const Note WORKING_MELODY[] = {
  { 1175, 45, 10 },  // D6
  { 1397, 45, 10 },  // F6
  { 1568, 45, 10 },  // G6
  { 1760, 90, 0 },   // A6
};

// Complete: cheerful "level complete" fanfare
const Note IDLE_MELODY[] = {
  { 1568, 80, 20 },  // G6
  { 1976, 80, 20 },  // B6
  { 2349, 80, 20 },  // D7
  { 2637, 220, 0 },  // E7
};

// Error: descending minor triad with a sad tail
const Note ERROR_MELODY[] = {
  { 1245, 100, 25 }, // D#6
  { 1109, 100, 25 }, // C#6
  { 988,  100, 25 }, // B5
  { 831,  260, 0 },  // G#5
};

// Prompt: alternating trill (attention!)
const Note PROMPT_MELODY[] = {
  { 2093, 60, 20 },  // C7
  { 2349, 60, 20 },  // D7
  { 2093, 60, 20 },  // C7
  { 2637, 160, 0 },  // E7
};

// Boot: tiny upward chirp
const Note BOOT_MELODY[] = {
  { 1760, 50, 10 },  // A6
  { 2637, 80, 0 },   // E7
};

// Unknown: single soft ping
const Note UNKNOWN_MELODY[] = {
  { 1568, 150, 0 },  // G6
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
