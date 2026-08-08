/*
  drive_car.ino  —  receives commands from the control page and drives the car

  Understands, in any capitalisation:

      f  forward        b  back           l  left
      r  right          s  stop

      on   LED steady            off  LED off
      p    LED pulses            (short for "pulse")

  Every command is echoed back so it appears in the page's log. Unknown
  commands say so rather than failing silently.

  LIBRARY
    Needs the Adafruit Motor Shield library V1. Install it once from
    Sketch -> Include Library -> Manage Libraries, search "Adafruit Motor
    Shield library", pick the version 1.x entry (not V2).

  WIRING
      module TXD  ->  pin 2
      module RXD  ->  pin A3
      module VCC  ->  5V
      module GND  ->  GND

    Pins 2 and A3 are free on the v1 shield, which uses 3-8, 11 and 12.

    Nothing is on pins 0 and 1, so uploads work with everything wired.
    Unplug the battery before uploading — the board won't appear in the
    Port menu while it's connected.

  HOW MOVEMENT WORKS
    A command starts the motors and they keep running until another command
    changes them. There are no delays, so a stop is acted on the instant it
    arrives. How long each step lasts is set by the "gap between commands"
    control on the page.

    This means the last command in a sequence runs on indefinitely. End
    sequences with a stop:  l f r s

    As a backstop, the car halts by itself if nothing is heard for SAFETY_MS.

  DEBUGGING
    The USB Serial Monitor is free — open it at 9600 to watch commands
    arrive over Bluetooth as they happen.
*/

#include <AFMotor.h>
#include <SoftwareSerial.h>
#include <ctype.h>
#include <string.h>

AF_DCMotor Motor1(1);   // back left
AF_DCMotor Motor2(2);   // front left
AF_DCMotor Motor3(3);   // front right
AF_DCMotor Motor4(4);   // back right

SoftwareSerial bt(2, A3);   // pin 2 = Arduino receives, A3 = Arduino sends

const byte SPEED = 100;     // 0-255. Raise for a faster car.

// Auto-stop if the car is moving and no command arrives for this long.
// Guards against a dropped connection leaving a car driving into a wall.
// Set to 0 to disable.
const unsigned long SAFETY_MS = 3000;

// Treat the buffer as a finished command if no newline arrives within this
// long. Makes the page's Line ending setting irrelevant.
const unsigned long FLUSH_MS = 150;

const unsigned long PULSE_MS = 250;   // half-period of the LED pulse

const byte CMD_MAX = 16;
char command[CMD_MAX];
byte length = 0;
unsigned long lastByte = 0;

bool moving = false;
unsigned long lastCommand = 0;

bool pulsing = false;
unsigned long lastToggle = 0;

void setup() {
  Serial.begin(9600);       // USB, for debugging
  bt.begin(9600);           // Bluetooth module

  Motor1.setSpeed(SPEED);
  Motor2.setSpeed(SPEED);
  Motor3.setSpeed(SPEED);
  Motor4.setSpeed(SPEED);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  halt();
  say(F("ready"));
}

void loop() {
  while (bt.available()) {
    char c = bt.read();
    lastByte = millis();

    if (c == '\n' || c == '\r') {      // command finished
      flush();
    }
    else if (length < CMD_MAX - 1) {
      command[length++] = tolower(c);  // lowercase as we go, so Left == left
    }
  }

  // No newline came, but nothing has arrived for a while — act on what we have.
  if (length > 0 && millis() - lastByte > FLUSH_MS) {
    flush();
  }

  if (SAFETY_MS > 0 && moving && millis() - lastCommand > SAFETY_MS) {
    halt();
    say(F("auto-stop: no command received"));
  }

  // Pulsing checks the clock rather than using delay(), so the sketch keeps
  // listening. A stop sent mid-pulse is acted on immediately.
  if (pulsing && millis() - lastToggle >= PULSE_MS) {
    lastToggle = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

void flush() {
  if (length == 0) return;
  command[length] = '\0';
  run(command);
  length = 0;
}

// Send to the page and to the USB monitor at once.
void say(const __FlashStringHelper *message) {
  bt.println(message);
  Serial.println(message);
}

// True if the command matches either the short or the long form.
bool matches(const char *cmd, const char *shortForm, const char *longForm) {
  return strcmp(cmd, shortForm) == 0 || strcmp(cmd, longForm) == 0;
}

void run(const char *cmd) {
  lastCommand = millis();

  if      (matches(cmd, "f", "forward")) forward();
  else if (matches(cmd, "b", "back"))    back();
  else if (matches(cmd, "l", "left"))    left();
  else if (matches(cmd, "r", "right"))   right();
  else if (matches(cmd, "s", "stop"))    halt();
  else if (matches(cmd, "p", "pulse")) {
    pulsing = true;
    lastToggle = millis();
    say(F("led pulsing"));
  }
  else if (strcmp(cmd, "on") == 0) {
    pulsing = false;
    digitalWrite(LED_BUILTIN, HIGH);
    say(F("led on"));
  }
  else if (strcmp(cmd, "off") == 0) {
    pulsing = false;
    digitalWrite(LED_BUILTIN, LOW);
    say(F("led off"));
  }
  else {
    bt.print(F("unknown command: "));
    bt.println(cmd);
    Serial.print(F("unknown command: "));
    Serial.println(cmd);
  }
}

/* ---- movement ------------------------------------------------------------
   Left side is Motor1 and Motor2. Right side is Motor3 and Motor4.

   Turns are pivots: one side forward, the other back, so the car spins in
   place rather than arcing.

   If a turn goes the wrong way, the motor leads are crossed on that side —
   fix the wiring rather than the code, or forward and back will be wrong too.
                                                                            */

void forward() {
  moving = true;
  Motor1.run(FORWARD);
  Motor2.run(FORWARD);
  Motor3.run(FORWARD);
  Motor4.run(FORWARD);
  say(F("forward"));
}

void back() {
  moving = true;
  Motor1.run(BACKWARD);
  Motor2.run(BACKWARD);
  Motor3.run(BACKWARD);
  Motor4.run(BACKWARD);
  say(F("back"));
}

void left() {
  moving = true;
  Motor1.run(BACKWARD);   // left side reverses
  Motor2.run(BACKWARD);
  Motor3.run(FORWARD);    // right side drives
  Motor4.run(FORWARD);
  // This is a pivot turn — the car spins in place. For a wider, gentler arc,
  // change the two BACKWARD lines above to RELEASE so that side coasts.
  say(F("left"));
}

void right() {
  moving = true;
  Motor1.run(FORWARD);    // left side drives
  Motor2.run(FORWARD);
  Motor3.run(BACKWARD);   // right side reverses
  Motor4.run(BACKWARD);
  // This is a pivot turn — the car spins in place. For a wider, gentler arc,
  // change the two BACKWARD lines above to RELEASE so that side coasts.
  say(F("right"));
}

void halt() {
  moving = false;
  Motor1.run(RELEASE);
  Motor2.run(RELEASE);
  Motor3.run(RELEASE);
  Motor4.run(RELEASE);
  say(F("stop"));
}
