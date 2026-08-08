# arduino-car

Bluetooth connection and terminal GUI for issuing commands to Arduino-controlled tiny cars.

**Control page: https://n-rondoni.github.io/arduino-car/**

Sketches are in `sketches/`. Get them with **Code → Download ZIP**.

---

## First time only: install the motor library

**Sketch → Include Library → Manage Libraries**, search for `Adafruit Motor Shield library`, and install the **version 1.x** entry — not V2. The search puts V2 first, and V2 will not compile against these sketches.

Skipping this gives you `AFMotor.h: No such file or directory`.

---

## Uploading a sketch

**Unplug the battery first.** The board won't show up in the Port menu while it's connected.

Then: plug in USB, **Tools → Board →** Arduino Uno, **Tools → Port →** the `usbmodem` entry, upload. Plug the battery back in when you're done.

Nothing needs to be unwired. The Bluetooth module sits on pins 2 and A3, so it stays out of the way of uploads.

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

**End sequences with a stop.** A command keeps running until the next one replaces it, so the last one in a sequence has nothing to end it. Write `l f r s`, not `l f r`. (The car also halts by itself after three seconds of silence, but that's a backstop, not a brake.)

---

## Settings at the bottom of the page

**Line ending** — leave it alone. The driving sketch works either way.

**Gap between commands** — how long each command in a sequence runs. Turns are quick, so start short.

---

## When it doesn't work

| What you see | What it means |
|---|---|
| No devices in the Connect list | Chrome lacks Bluetooth permission, or something else is already connected — only one thing can hold the module at a time |
| Module LED blinking, never solid | Not connected yet; blinking means it's waiting |
| Module LED off | No power |
| `AFMotor.h: No such file or directory` | The motor library isn't installed — see above |
| Board missing from the Port menu | The battery is still connected — unplug it to upload |
| Connected, commands do nothing | Check the module's wires: TXD to pin 2, RXD to A3 |
| Worked, then stopped after a power cycle | Cutting power kills the Bluetooth link — reconnect on the page |
| Car keeps going after a sequence ends | No stop at the end — add `s` |
| Car stops on its own after a few seconds | Lost the connection; that's the safety timeout doing its job |

Best single check: the **`L` LED on the Arduino board**. If it responds, everything from browser to sketch is working.
