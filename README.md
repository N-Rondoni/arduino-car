# arduino-car

Bluetooth connection and terminal GUI for issuing commands to Arduino-controlled tiny cars.

**Control page: https://n-rondoni.github.io/arduino-car/**

Sketches are in `sketches/`. Get them with **Code → Download ZIP**.

---

## Uploading a sketch

**Pull the two wires off pins 0 and 1 first. Put them back afterward.**

The Uno shares one serial port between the USB cable and pins 0/1. Leave them connected and the upload fails with `not in sync`. Only those two wires come out — leave USB and power alone.

Then: **Tools → Board →** Arduino Uno, **Tools → Port →** the `usbmodem` entry, upload.

---

## Driving the car

1. **Use Chrome.** Safari and Firefox can't reach Bluetooth devices.
2. First time only: **System Settings → Privacy & Security → Bluetooth**, turn Chrome on. Otherwise the device list is empty with no explanation.
3. Open the control page, click **Connect**, pick the module.
4. Lamp turns solid green when connected.

Click the buttons, or type commands. Several separated by spaces run in order:

```
l f r
```

The **gap** setting controls how long each one runs before the next arrives.

---

## Line endings

The dropdown decides whether a newline follows each command. It has to match the sketch.

| Sketch | Setting |
|---|---|
| Acts on every character | **None** |
| Waits for a whole word | **Newline** |

If a word command does nothing at all, check this first.

---

## When it doesn't work

| What you see | What it means |
|---|---|
| No devices in the Connect list | Chrome lacks Bluetooth permission, or something else is already connected — only one thing can hold the module at a time |
| Module LED blinking, never solid | Not connected yet; blinking means it's waiting |
| Module LED off | No power |
| Connected, commands do nothing | The two wires didn't go back on pins 0 and 1 after the last upload |
| Upload fails, `not in sync` | The wires are still on pins 0 and 1 |
| Worked, then stopped after a power cycle | Cutting power kills the Bluetooth link — reconnect on the page |

Best single check: the **`L` LED on the Arduino board**. If it responds, everything from browser to sketch is working.
