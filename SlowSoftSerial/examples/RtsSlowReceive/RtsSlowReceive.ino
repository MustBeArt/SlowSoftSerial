/*
  RtsSlowReceive sketch

  This sketch demonstrates RTS hardware handshaking, by receiving data on
  a SlowSoftSerial port but with a big delay between characters, simulating
  a receiving application that is much slower than the data rate on the wire.

  It passes through the data to the USB serial port, so you can check for
  data corruption. There should be none, provided that the sending device
  honors RTS handshaking correctly.

  created for SlowSoftSerial 2020-05-12 Paul Williamson
*/

#include "SlowSoftSerial.h"

#define RTS_PIN 19
#define THRESHOLD 5

SlowSoftSerial sss(0,1);

void setup() {
  Serial.begin(9600);
  sss.begin(9600);
  sss.attachRts(RTS_PIN, THRESHOLD);
}

void loop() {
  if (Serial.available()) {      // If anything comes in Serial (USB),
    sss.write(Serial.read());   // read it and send it out sss (pins 0 & 1)
  }

  if (sss.available()) {        // If anything comes in sss (pins 0 & 1)
    Serial.write(sss.read());   // read it and send it out Serial (USB)
    delay(100);                 // process at about 10 characters/sec
  }
}
