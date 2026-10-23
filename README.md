# OpenCode Hardware Notifier

Physical status feedback for [OpenCode](https://opencode.ai). An Arduino drives a buzzer and four LEDs whenever the agent starts working, finishes, fails, or needs your attention.

![Circuit diagram](docs/circuit-diagram.png)

---

## Status reference

| State   | Trigger                                              | Buzzer                     | LED    |
|---------|------------------------------------------------------|----------------------------|--------|
| READY   | Plugin connected                                     | Rising 3-note triad        | -      |
| WORKING | Prompt sent / thinking / tools / subagents            | Short rising motif         | Green  |
| IDLE    | Whole task succeeded or was cancelled                | 3-note completion chime    | Yellow |
| ERROR   | Whole task failed                                    | Falling low alarm          | Red    |
| PROMPT  | Permission requested or a question needs an answer    | Alternating attention trill| Blue   |
| OFF     | OpenCode closes or the plugin unloads                | Silent                     | All off|

A state is only sent when it changes, so the buzzer never repeats itself. New
states interrupt the current melody, and the melody player is non-blocking, so
the Arduino keeps reading serial input while it plays.

## Behaviour

`WORKING` covers everything the agent does: accepting a prompt, thinking,
streaming text, running tools, compacting, retrying, and waiting on subagents.
Step, text, and reasoning boundaries do **not** end the task, so the light no
longer turns yellow as soon as the model stops thinking.

`IDLE` means the whole task is finished. The plugin tracks every executing
session, including subagent sessions, and only turns yellow when the last one
ends. Cancelling a task also returns to `IDLE`.

`ERROR` is reserved for a failed task. A subagent failure does not immediately
turn the light red while the parent is still running - the parent decides
whether the failure is fatal. An error stays latched until new work starts, so
a late idle event cannot hide it.

`PROMPT` fires when OpenCode asks for permission or presents a question/form.
Replying returns the notifier to `WORKING`.

`OFF` is sent when the plugin unloads, for example when OpenCode closes or the
background service stops. The Arduino stops any melody and turns every LED off
without making a sound.

## Features

- Distinct sound and colour for each state
- Short melodic cues instead of flat beeps, with non-blocking playback
- Immediate feedback when a prompt is sent, before the model starts thinking
- Working state held through thinking, tool calls, and subagents
- Completion only when the entire task, not an individual step, finishes
- Correct handling of cancellation, including cancelling before processing begins
- Permission/question prompts surfaced on the blue LED
- Repeat suppression in both the plugin and the Arduino
- Boot delay for boards that reset when the serial port opens
- Debug log at `/tmp/opencode-buzzer-debug.log`

## Hardware

- Arduino Uno, Nano, Leonardo, or similar
- 1x buzzer (active or passive - `tone()` drives both)
- 4x LEDs: green, yellow, red, blue
- 4x 220 ohm resistors
- Breadboard and jumper wires

### Wiring

| Arduino pin | Component                            |
|-------------|--------------------------------------|
| 8           | Buzzer +                             |
| 9           | Green LED anode (220 ohm resistor)   |
| 10          | Yellow LED anode (220 ohm resistor)  |
| 11          | Red LED anode (220 ohm resistor)     |
| 12          | Blue LED anode (220 ohm resistor)    |
| GND         | Buzzer - and all LED cathodes        |

> The Tinkercad diagram above is a visual reference. The pin table matches the default pins in the sketch - adjust either if you wire it differently.

If you use an RGB LED instead of separate LEDs, replace `setLeds()` with three `analogWrite()` calls.

Melodies live in the `*_MELODY` arrays at the top of the sketch. Change the
frequencies, durations, and gaps there to tune the sounds.

## Setup

### 1. Flash the Arduino

Open `arduino/buzzer_notifier.ino` in the Arduino IDE, select your board and port, and upload.

### 2. Install the plugin

```bash
mkdir -p ~/.config/opencode/plugins
cp plugin/buzzer.js ~/.config/opencode/plugins/buzzer.js
```

OpenCode v2 auto-loads server plugins from that directory. If yours does not, register it in `~/.config/opencode/opencode.json(c)`:

```jsonc
{
  "plugins": ["./plugins/buzzer.js"]
}
```

Do **not** add it to `~/.config/opencode/cli.json`. That file is only for CLI/TUI plugins, so a server plugin listed there shows up as a failed plugin in the Plugins screen.

The plugin uses only Node.js built-in modules, so no dependencies are needed. Keep a `package.json` next to it if OpenCode tries to run an install.

### 3. Check the serial port

The default is `/dev/ttyUSB0` at 9600 baud. If your board appears elsewhere, update `PORT` and `BAUD` at the top of `plugin/buzzer.js`:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

On Linux, grant access to the port (usually the `dialout` group), then log out and back in:

```bash
sudo usermod -aG dialout "$USER"
```

### 4. Restart OpenCode

```bash
opencode
```

You should hear the `READY` rising triad when the plugin connects.

## Testing

Run the state-machine tests:

```bash
npm test
```

Send a status manually:

```bash
echo "WORKING" > /dev/ttyUSB0
```

Follow the plugin events live:

```bash
tail -f /tmp/opencode-buzzer-debug.log
```

Then start a task in OpenCode and watch the LED and buzzer change.

## Troubleshooting

| Symptom                                  | Likely cause                                                     |
|------------------------------------------|------------------------------------------------------------------|
| No sound at all                          | Wrong port, missing `dialout` permission, or board not flashed   |
| Sound only once at startup               | Plugin connected but no event matched - check the debug log      |
| Stuck on green after cancelling          | Cancellation event name not covered - check the debug log        |
| Stays green after the task visibly ends  | Completion event not seen - check the debug log for `execution.succeeded` |
| Plugin shows **failed** in the Plugins screen | It is listed under `plugins` in `cli.json` (TUI-only) - remove it from there; server plugins are auto-loaded from `~/.config/opencode/plugins/` |
| `NpmInstallFailedError`                  | A server plugin was listed in `cli.json`, or `package.json` is missing next to a package plugin |
| Continuous buzzing                       | An event match is too broad and firing on background events      |

## License

[MIT](LICENSE)
