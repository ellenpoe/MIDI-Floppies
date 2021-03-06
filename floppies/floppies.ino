#include "hardware_defs.h"
#include "midi.h"

#include "TimerOne.h"

byte max_notes_by_floppy[]=FLOPPY_MAX_NOTES;

unsigned int micros_per_tick=40;

byte current_pos[NUM_FLOPPIES];
byte current_dir[NUM_FLOPPIES];
byte current_note[NUM_FLOPPIES];
byte current_channel[NUM_FLOPPIES];
unsigned int period_ticks[NUM_FLOPPIES];
unsigned int ticks_since_step[NUM_FLOPPIES];
byte last_cmd=0;

void setupPins();
void tick();
void playNote(byte channel, byte note, byte velocity);
void stopNote(byte channel, byte note);
void pitchBend(byte channel, byte LSB, byte MSB);
void stepFloppy(byte floppy, byte dir);
void resetAll();
void pulseAll();
void testAll();

void setup() {
  setupPins();
  // if desired, resetAll() and then testAll() should be called here
  Timer1.initialize(micros_per_tick); // Set up a timer at the defined resolution
  Timer1.attachInterrupt(tick); // Attach the tick function
  SERIAL_BEGIN(MIDI_BAUD);
}

void loop() {
  byte incomingByte;
  byte msb;
  byte lsb;
  byte note;
  byte velocity;

  if (SERIAL_AVAILABLE()>0) {
    // read the incoming byte:
    incomingByte = SERIAL_READ();
    if (incomingByte >= 144 && incomingByte < 160) { // note on message starting
      while (!SERIAL_AVAILABLE());
      note=SERIAL_READ();
      while (!SERIAL_AVAILABLE());
      velocity=SERIAL_READ();
      playNote(incomingByte-144, note, velocity);
      last_cmd=incomingByte;
    }
    else if (incomingByte >= 128 && incomingByte < 144) { // note off message starting
      while (!SERIAL_AVAILABLE());
      note=SERIAL_READ();
      while (!SERIAL_AVAILABLE());
      SERIAL_READ();
      stopNote(incomingByte-128, note);
      last_cmd=incomingByte;
    }
    else if (incomingByte >= 224 && incomingByte < 240) { // pitch bend message starting
      while (!SERIAL_AVAILABLE());
      lsb=SERIAL_READ();
      while (!SERIAL_AVAILABLE());
      msb=SERIAL_READ();
      pitchBend(incomingByte-224, lsb, msb);
      last_cmd=incomingByte;
    }
    else if (incomingByte < 128 && last_cmd >= 144 && last_cmd < 160) { // data byte, assume it's a continuation of the last command
      note=incomingByte;
      while (!SERIAL_AVAILABLE());
      velocity=SERIAL_READ();
      playNote(last_cmd-144, note, velocity);
    } 
    else if (incomingByte < 128 && last_cmd >= 128 && last_cmd < 144) { // data byte, assume it's a continuation of the last command
      note=incomingByte;
      while (!SERIAL_AVAILABLE());
      SERIAL_READ();
      stopNote(last_cmd-128, note);
    }
    else if (incomingByte < 128 && incomingByte >= 224 && incomingByte < 240) { // data byte, assume it's a continuation of the last command
      lsb=incomingByte;
      while (!SERIAL_AVAILABLE());
      msb=SERIAL_READ();
      pitchBend(last_cmd-224, lsb, msb);
      last_cmd=incomingByte;
    }
    else {
      // no other commands implemented
    }
  }
}

void setupPins() {
  for (byte i=0; i<NUM_FLOPPIES; i++) {
    pinMode(STEP_PIN(i), OUTPUT);
    pinMode(DIR_PIN(i), OUTPUT);
    pinMode(GND_PIN(i), OUTPUT);
    digitalWrite(GND_PIN(i), LOW); // set the ground pin 
  }
}

void tick() {
  for (byte i=0; i<NUM_FLOPPIES; i++) {
    if (period_ticks[i]!=0) {
      ticks_since_step[i]++;
      // if this floppy is due to be stepped again
      if (ticks_since_step[i] > period_ticks[i]) {
        // switch directions and step
        current_dir[i]=!current_dir[i];
        stepFloppy(i, current_dir[i]);
        // start counting from zero again
        ticks_since_step[i]=0;
      }
    }
  }
}

void stopNote(byte channel, byte note) {
  byte i;
  for (i=0; i<NUM_FLOPPIES; i++) {
    if (current_channel[i]==channel && current_note[i]==note) {
      current_channel[i]=0;
      current_note[i]=0;
      period_ticks[i]=0;
    }
  }
}

void playNote(byte channel, byte note, byte velocity) {
  if (velocity==0) {
    stopNote(channel, note);
    return;
  }
  byte floppy;
  signed int desiredIndex=-1; // the index of the floppy that we want to use

  unsigned int period=PERIOD_BY_NOTE(note)/micros_per_tick;
  if (period==0) {
    return;
  }

  for (floppy=0; floppy<NUM_FLOPPIES; floppy++) {
    // if this floppy is not in use
    if (current_note[floppy]==0) {
      if (note<=MAX_NOTE_BY_FLOPPY(floppy)) {
        //use this floppy
        desiredIndex=floppy;
        break;
      }
    }
  }
  // if we found something
  if (desiredIndex!=-1) {
    // set the values indicating what the floppy is doing
    period_ticks[desiredIndex]=period;
    current_note[desiredIndex]=note;
    current_channel[desiredIndex]=channel;

    // step it now to get the note started ASAP
    stepFloppy(desiredIndex, current_dir[desiredIndex]);
    ticks_since_step[desiredIndex]=0;
  }
}

void pitchBend(byte channel, byte LSB, byte MSB) {
  byte i;
  unsigned int finalVal=(((unsigned int)MSB & 0x7F) << 7) + ((unsigned int)LSB & 0x7F);
  if (finalVal==0x2000) {
    for (i=0; i<NUM_FLOPPIES; i++) {
      if (current_channel[i]==channel) {
        period_ticks[i]=PERIOD_BY_NOTE(current_note[i])/micros_per_tick;
      }
    }
  }
  else if (finalVal<0x2000) {
    for (i=0; i<NUM_FLOPPIES; i++) {
      if (current_channel[i]==channel) {
        unsigned long pitchBendProportion=0x2000-finalVal;
        unsigned long flatPeriod=PERIOD_BY_NOTE(current_note[i]-1);
        unsigned long period=PERIOD_BY_NOTE(current_note[i]);
        period+=(unsigned long)((flatPeriod-period)*((double)pitchBendProportion/0x2000));
        period_ticks[i]=period/micros_per_tick;
      }
    }
  }
  else if (finalVal>0x2000) {
    for (i=0; i<NUM_FLOPPIES; i++) {
      if (current_channel[i]==channel) {
        unsigned long pitchBendProportion=finalVal-0x2000;
        unsigned long sharpPeriod=PERIOD_BY_NOTE(current_note[i]+1);
        unsigned long period=PERIOD_BY_NOTE(current_note[i]);
        period-=(unsigned long)((period-sharpPeriod)*((double)pitchBendProportion/0x2000));
        period_ticks[i]=period/micros_per_tick;
      }
    }
  }
}

void stepFloppy(byte floppy, byte dir) {
  digitalWrite(DIR_PIN(floppy), dir ? HIGH : LOW);
  digitalWrite(STEP_PIN(floppy), HIGH);
  digitalWrite(STEP_PIN(floppy), LOW);

  // don't let the current_pos move beyond the number of tracks
  if (current_pos[floppy]<(NUM_TRACKS-1) && dir) {
    current_pos[floppy]++;
  } 
  else if (current_pos[floppy]>0 && !dir) {
    current_pos[floppy]--;
  }
}

void resetAll() {
  for (byte i=0; i<NUM_TRACKS; i++) {
    for (byte floppy=0; floppy<NUM_FLOPPIES; floppy++) {
      stepFloppy(floppy, 0);
    }
    delay(5);
  }
}

void pulseAll() {
  for (byte floppy=0; floppy<NUM_FLOPPIES; floppy++) {
    stepFloppy(floppy, 1);
  }
  delay(5);
  for (byte floppy=0; floppy<NUM_FLOPPIES; floppy++) {
    stepFloppy(floppy, 0);
  }
}

void testAll() {
  pulseAll();
  for (byte floppy=0; floppy<NUM_FLOPPIES; floppy++) {
    for (byte track=0; track<NUM_TRACKS; track++) {
      stepFloppy(floppy, 1);
      delay(5);
    }
    pulseAll();
    for (byte track=0; track<NUM_TRACKS; track++) {
      stepFloppy(floppy, 0);
      delay(5);
    }
    pulseAll();
  }
}







