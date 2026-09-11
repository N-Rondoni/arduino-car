/*
  drive_car.ino  —  receives commands from the control page and drives the car

  Understands, in any capitalisation:

      f  forward        b  back           l  left
      r  right          s  stop

      tl  turn left     tr  turn right    (by degrees, not seconds - see below)

      on   LED steady            off  LED off
      p    LED pulses            (short for "pulse")

  Every command is echoed back so it appears in the page's log. Unknown
  commands say so rather than failing silently.

  LIBRARY
    Needs the Adafruit Motor Shield library V1. Install it once from
    Sketch -> Include Library -> Manage Libraries, search "Adafruit Motor
    Shield library", pick the version 1.x entry (not V2).

    Also needs NewPing, for the distance sensor. Same menu, search "NewPing".

  WIRING
      module TXD  ->  pin 2
      module RXD  ->  pin A3
      module VCC  ->  5V
      module GND  ->  GND

    Pins 2 and A3 are free on the v1 shield, which uses 3-8, 11 and 12.

    Nothing is on pins 0 and 1, so uploads work with everything wired.
    Unplug the battery before uploading — the board won't appear in the
    Port menu while it's connected.

  DISTANCE SENSOR (HC-SR04)
      sensor VCC   ->  5V
      sensor GND   ->  GND
      sensor Trig  ->  pin A0
      sensor Echo  ->  pin A1

    Every PING_INTERVAL_MS the sketch pings the sensor and sends the
    reading to the page and the USB monitor, the same as any other
    message: "distance: 28cm".

    To make the car react to distance, edit handleDistanceRules() near the
    bottom of this file. It's called once per ping with the latest reading
    and knows which way the car is currently driving, so a rule like "stop
    if something is closer than 15cm while going forward" is a few lines.
    A worked example is already there — read it, then add your own below it.

  HOW MOVEMENT WORKS
    A command starts the motors and they keep running until another command
    changes them, or until an optional duration you attach to it runs out.
    There are no delays, so a stop (or a timed command ending) is acted on
    the instant it's due.

    Attach a number of seconds directly after a movement letter to make it
    self-limiting:  f5  drives forward for 5 seconds, then stops on its own.
    b2.5 / left3 / r0.5 all work the same way — short form or long form,
    whole numbers or decimals. Leave the number off and it behaves as
    before: runs until the next command replaces it.

    Without a duration, the last command in a sequence runs on indefinitely.
    End sequences with a stop:  l f r s
    With durations, each step already ends itself, so a trailing stop is
    optional (but harmless):  f5 l2 r2 s

    As a backstop, the car halts by itself if nothing is heard for SAFETY_MS
    — this still applies on top of any duration timer.

  TURNING BY DEGREES
    tl and tr work like l and r, but the number after them is degrees, not
    seconds:  tl90 pivots left roughly 90°, tr45 pivots right roughly 45°.

    "Roughly" is the honest word for it - the car has no way to sense how
    far it's actually turned, so this is really "however long MS_PER_DEGREE
    says 90° takes." That constant needs calibrating on your actual car
    (see its comment above) before the angles will be close to accurate.
    Plain l / r (with or without a plain number of seconds) still work
    exactly as before, if you'd rather turn by time instead of degrees.

  DEBUGGING
    The USB Serial Monitor is free — open it at 9600 to watch commands
    arrive over Bluetooth as they happen.
*/

#include <AFMotor.h>
#include <SoftwareSerial.h>
#include <NewPing.h>
#include <ctype.h>
#include <string.h>

AF_DCMotor Motor1(1);   // back left
AF_DCMotor Motor2(2);   // front left
AF_DCMotor Motor3(3);   // front right
AF_DCMotor Motor4(4);   // back right

SoftwareSerial bt(2, A3);   // pin 2 = Arduino receives, A3 = Arduino sends

#define TRIGGER_PIN  A0
#define ECHO_PIN     A1
#define MAX_DISTANCE 200   // cm - readings beyond this come back as 0

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

const unsigned long PING_INTERVAL_MS = 250;  // how often to check distance
unsigned long lastPing = 0;

const byte SPEED = 100;     // 0-255. Raise for a faster car.

// How many milliseconds a 1-degree pivot turn takes at the current SPEED.
// Used to turn "tl90" into a duration. There's no sensor telling the car
// how far it's actually turned, so this has to be measured by hand:
//   1. Send "tl360" (or count seconds if you'd rather test with plain "l").
//   2. Watch how far the car actually rotates, in degrees.
//   3. MS_PER_DEGREE = MS_PER_DEGREE * (360.0 / actual_degrees_achieved)
// Redo this any time SPEED, the battery, or the car's weight changes much.
const float MS_PER_DEGREE = 8.0;

// Auto-stop if the car is moving and no command arrives for this long.
// Guards against a dropped connection leaving a car driving into a wall.
// Doesn't apply to a timed move (e.g. "f5") - that already has its own end
// time, so a duration longer than SAFETY_MS still runs in full.
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

// Set when a command like "f5" gives itself a duration. Checked in loop()
// the same non-blocking way the LED pulse and safety auto-stop are.
bool timedMoveActive = false;
unsigned long timedMoveEnd = 0;

// For manoeuvres that chain a turn after the current timed move ends
// (see handleDistanceRules' example 4). 0 = nothing queued. Positive =
// turn left that many degrees, negative = turn right that many degrees.
int pendingTurnDegrees = 0;

// Which way the car is currently driving, if any. Lets handleDistanceRules()
// tell "driving forward" apart from "backing up" or "turning".
enum Direction { NONE, GOING_FORWARD, GOING_BACK, GOING_LEFT, GOING_RIGHT };
Direction currentDirection = NONE;

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

  if (SAFETY_MS > 0 && moving && !timedMoveActive && millis() - lastCommand > SAFETY_MS) {
    halt();
    say(F("auto-stop: no command received"));
  }

  // A timed command (e.g. "f5") has run its course. If a chained turn was
  // queued (see handleDistanceRules example 4), do that instead of halting.
  if (timedMoveActive && millis() >= timedMoveEnd) {
    timedMoveActive = false;
    if (pendingTurnDegrees != 0) {
      int degrees = pendingTurnDegrees;
      pendingTurnDegrees = 0;
      if (degrees > 0) { left();  scheduleDegrees(degrees); }
      else              { right(); scheduleDegrees(-degrees); }
      say(F("turning"));
    }
    else {
      halt();
      say(F("timed stop"));
    }
  }

  // Ping the distance sensor on its own schedule, report it, then let
  // handleDistanceRules() decide if the car should react.
  if (millis() - lastPing >= PING_INTERVAL_MS) {
    lastPing = millis();
    unsigned int cm = sonar.ping_cm();
    reportDistance(cm);
    handleDistanceRules(cm);
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

// Same idea as say(), but for the numeric distance reading.
// cm == 0 means "no echo" - either nothing is in range, or a bad reading.
void reportDistance(unsigned int cm) {
  bt.print(F("distance: "));
  bt.print(cm);
  bt.println(F("cm"));
  Serial.print(F("distance: "));
  Serial.print(cm);
  Serial.println(F("cm"));
}

/* ---- distance rules --------------------------------------------------
   Called once per ping, right after the reading is reported.

     cm                the latest distance, in centimetres (0 = no echo)
     currentDirection  NONE, GOING_FORWARD, GOING_BACK, GOING_LEFT,
                        or GOING_RIGHT - whatever the car is doing right now

   Add rules below the examples. Keep each one an "if (...) { ... }" block
   - anything you already know how to do in the movement functions below
   (halt(), forward(), left(), say(), etc.) works fine here too.
                                                                          */
void handleDistanceRules(unsigned int cm) {

  // Example 1: something is within 15cm while driving forward - stop.
  // A cm of 0 means no echo (out of range), so that's excluded with cm > 0.
  if (currentDirection == GOING_FORWARD && cm > 0 && cm < 15) {
    halt();
    say(F("obstacle stop"));
  }

  // Example 2: same idea, but behind the car while backing up. Shows the
  // same pattern reused for a different direction.
  if (currentDirection == GOING_BACK && cm > 0 && cm < 15) {
    halt();
    say(F("obstacle stop (behind)"));
  }

  // Example 3: two thresholds instead of one. Close enough to worry about,
  // but not close enough to panic - just say something, don't stop.
  // Note this checks BEFORE example 1's tighter threshold logically would,
  // so something at 10cm triggers both this warning AND the stop above
  // (both run every ping - they're not "else if", each is its own check).
  if (currentDirection == GOING_FORWARD && cm >= 15 && cm < 30) {
    say(F("obstacle warning"));
  }

  // Example 4: a simple avoidance manoeuvre instead of just stopping.
  // Combines two things already in this file: schedule() gives the back-up
  // a duration, and turnDegrees the turn afterward - see PENDING_TURN below
  // for how the "back up, then turn" sequence is timed without delay().
  if (currentDirection == GOING_FORWARD && cm > 0 && cm < 10) {
    back();
    schedule(0.5);           // back up for half a second...
    pendingTurnDegrees = 90; // ...then turn, once the back-up finishes
    say(F("obstacle - backing up"));
  }

  // ---- add your own rules here ----

}

// True if the command matches either the short or the long form.
bool matches(const char *cmd, const char *shortForm, const char *longForm) {
  return strcmp(cmd, shortForm) == 0 || strcmp(cmd, longForm) == 0;
}

// If a duration was given (seconds > 0), arms the auto-stop timer.
// Otherwise clears it, so the move behaves the old way: runs until replaced.
void schedule(float seconds) {
  if (seconds > 0) {
    timedMoveActive = true;
    timedMoveEnd = millis() + (unsigned long)(seconds * 1000.0);
  }
  else {
    timedMoveActive = false;
  }
}

// Same idea, but takes degrees and converts using MS_PER_DEGREE.
void scheduleDegrees(float degrees) {
  schedule(degrees * MS_PER_DEGREE / 1000.0);
}

void run(const char *cmd) {
  lastCommand = millis();
  pendingTurnDegrees = 0;  // a fresh command always cancels any queued turn

  // Split "f5" / "back2.5" / "tl90" into the letters ("f" / "back" / "tl")
  // and whatever trails them, parsed as a number. What that number means
  // depends on the command: seconds for f/b/l/r, degrees for tl/tr.
  char action[CMD_MAX];
  byte i = 0;
  while (cmd[i] != '\0' && isalpha((unsigned char)cmd[i]) && i < CMD_MAX - 1) {
    action[i] = cmd[i];
    i++;
  }
  action[i] = '\0';
  float value = (cmd[i] != '\0') ? atof(cmd + i) : 0;

  if      (matches(action, "f", "forward")) { forward(); schedule(value); }
  else if (matches(action, "b", "back"))    { back();    schedule(value); }
  else if (matches(action, "l", "left"))    { left();    schedule(value); }
  else if (matches(action, "r", "right"))   { right();   schedule(value); }
  else if (matches(action, "tl", "turnleft"))  { left();  scheduleDegrees(value); }
  else if (matches(action, "tr", "turnright")) { right(); scheduleDegrees(value); }
  else if (matches(action, "s", "stop")) {
    halt();
    timedMoveActive = false;
  }
  else if (matches(action, "p", "pulse")) {
    pulsing = true;
    lastToggle = millis();
    say(F("led pulsing"));
  }
  else if (strcmp(action, "on") == 0) {
    pulsing = false;
    digitalWrite(LED_BUILTIN, HIGH);
    say(F("led on"));
  }
  else if (strcmp(action, "off") == 0) {
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
  currentDirection = GOING_FORWARD;
  Motor1.run(FORWARD);
  Motor2.run(FORWARD);
  Motor3.run(FORWARD);
  Motor4.run(FORWARD);
  say(F("forward"));
}

void back() {
  moving = true;
  currentDirection = GOING_BACK;
  Motor1.run(BACKWARD);
  Motor2.run(BACKWARD);
  Motor3.run(BACKWARD);
  Motor4.run(BACKWARD);
  say(F("back"));
}

void left() {
  moving = true;
  currentDirection = GOING_LEFT;
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
  currentDirection = GOING_RIGHT;
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
  currentDirection = NONE;
  Motor1.run(RELEASE);
  Motor2.run(RELEASE);
  Motor3.run(RELEASE);
  Motor4.run(RELEASE);
  say(F("stop"));
}
