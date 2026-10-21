# OpenCode Hardware Notifier

Physical status feedback for [OpenCode](https://opencode.ai) using an Arduino, a buzzer, and four LEDs.

When OpenCode starts working, finishes, hits an error, or needs your input, this plugin sends a status line over serial. The Arduino receives it and reacts with a distinct sound and LED colour, so you can see and hear what the agent is doing without watching the terminal.

## Features

- **Distinct sound + LED for every state** (working, idle, error, prompt)
- **Immediate feedback** when a prompt is sent, before the model starts thinking
- **Correct handling of cancellation**, including cancelling before processing begins
- **No repeat buzzing** - a state is only sent when it actually changes
- **Automatic serial reconnection** on plugin load, with a boot delay for boards that reset
- **Debug log** at `/tmp/opencode-buzzer-debug.log`

## Status mapping

| State   | OpenCode event(s)                     | Sound                      | LED    |
|---------|---------------------------------------|----------------------------|--------|
| READY   | plugin connected                      | 2 short beeps              | -      |
| WORKING | prompt sent / execution started       | long 800 Hz beep           | Green  |
| IDLE    | execution succeeded / cancelled       | short 1200 Hz beep         | Yellow |
| ERROR   | execution failed / error              | 3 fast 600 Hz beeps        | Red    |
| PROMPT  | permission / question (reserved)      | double ascending beep      | Blue   |

## Hardware

- Arduino (Uno, Nano, Leonardo, etc.)
- 1x buzzer (active or passive - `tone()` drives both)
- 4x LEDs: green, yellow, red, blue
- 4x 220 ohm resistors

### Wiring

| Arduino pin | Component                                   |
|-------------|---------------------------------------------|
| 8           | Buzzer +                                    |
| 9           | Green LED anode (220 ohm resistor)          |
| 10          | Yellow LED anode (220 ohm resistor)         |
| 11          | Red LED anode (220 ohm resistor)            |
| 12          | Blue LED anode (220 ohm resistor)           |
| GND         | Buzzer - and all LED cathodes               |

If you use an RGB LED instead of separate LEDs, adapt `setLeds()` to drive the three colour pins with `analogWrite()`.

## Installation

### 1. Flash the Arduino

Open `arduino/buzzer_notifier.ino` in the Arduino IDE, select your board and port, then upload.

The board resets when the serial port opens, so the plugin waits 2 seconds after connecting before sending `READY`.

### 2. Install the plugin

Copy the plugin into your global OpenCode plugin directory:

```bash
mkdir -p ~/.config/opencode/plugins
cp plugin/buzzer.js ~/.config/opencode/plugins/buzzer.js
```

OpenCode v2 auto-loads plugins from that directory. If it does not, register the plugin in `~/.config/opencode/cli.json` (or `opencode.json`):

```jsonc
{
  "plugins": ["./plugins/buzzer.js"]
}
```

The plugin only uses Node.js built-in modules, so no `npm install` is required. Keep a `package.json` next to it (see this repo) if OpenCode attempts an install.

### 3. Configure the serial port

The default port is `/dev/ttyUSB0` at 9600 baud. Change `PORT` and `BAUD` near the top of `plugin/buzzer.js` if your board appears elsewhere:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

On Linux, make sure your user can access the port (usually by joining the `dialout` group):

```bash
sudo usermod -aG dialout "$USER"
```

Then log out and back in.

### 4. Restart OpenCode

```bash
opencode
```

You should hear the `READY` double beep when the plugin connects.

## Testing

Send a status manually:

```bash
echo "WORKING" > /dev/ttyUSB0
```

Watch the plugin's event log:

```bash
tail -f /tmp/opencode-buzzer-debug.log
```

Then start a task in OpenCode and confirm the LED and sound change.

## Troubleshooting

| Symptom                          | Likely cause                                                    |
|----------------------------------|-----------------------------------------------------------------|
| No sound at all                  | Wrong port, missing `dialout` permission, or board not flashed  |
| Sound only once at startup       | Plugin connected but events are not matching (`WORKING` log?)    |
| Stuck on green after cancelling  | Cancellation event name not covered - check the debug log        |
| `NpmInstallFailedError`          | Add a `package.json` with no dependencies next to the plugin     |
| Continuous buzzing               | An overly broad event match is firing on background events      |

## Notes

- The plugin ignores background events such as `provider.updated` and `model.updated`.
- `lastState` prevents resending an unchanged status, so the buzzer only reacts to transitions.
- The Arduino also ignores repeated identical commands as a second layer of protection.

## License

MIT - see [LICENSE](LICENSE).
