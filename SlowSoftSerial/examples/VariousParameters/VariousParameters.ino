#include "SlowSoftSerial.h"

// This sketch sends at all the data bits, parity, and stop bits combinations
// that SlowSoftSerial supports. If you watch the output with a terminal
// emulator at any one setting, it will very probably become confused by all
// the errors, and display even the correctly received characters incorrectly.
// This sketch may only be useful with a serial analyzer to understand the
// output.

#define NUM_MODES 40

int dps[NUM_MODES] = {
  SSS_SERIAL_5N1, SSS_SERIAL_6N1, SSS_SERIAL_7N1, SSS_SERIAL_8N1,
  SSS_SERIAL_5N2, SSS_SERIAL_6N2, SSS_SERIAL_7N2, SSS_SERIAL_8N2,
  SSS_SERIAL_5E1, SSS_SERIAL_6E1, SSS_SERIAL_7E1, SSS_SERIAL_8E1,
  SSS_SERIAL_5E2, SSS_SERIAL_6E2, SSS_SERIAL_7E2, SSS_SERIAL_8E2,
  SSS_SERIAL_5O1, SSS_SERIAL_6O1, SSS_SERIAL_7O1, SSS_SERIAL_8O1,
  SSS_SERIAL_5O2, SSS_SERIAL_6O2, SSS_SERIAL_7O2, SSS_SERIAL_8O2,
  SSS_SERIAL_5M1, SSS_SERIAL_6M1, SSS_SERIAL_7M1, SSS_SERIAL_8M1,
  SSS_SERIAL_5M2, SSS_SERIAL_6M2, SSS_SERIAL_7M2, SSS_SERIAL_8M2,
  SSS_SERIAL_5S1, SSS_SERIAL_6S1, SSS_SERIAL_7S1, SSS_SERIAL_8S1,
  SSS_SERIAL_5S2, SSS_SERIAL_6S2, SSS_SERIAL_7S2, SSS_SERIAL_8S2,
 };

const char  *dps_names[NUM_MODES] = {
  "5N1", "6N1", "7N1", "8N1",
  "5N2", "6N2", "7N2", "8N2",
  "5E1", "6E1", "7E1", "8E1",
  "5E2", "6E2", "7E2", "8E2",
  "5O1", "6O1", "7O1", "8O1",
  "5O2", "6O2", "7O2", "8O2",
  "5M1", "6M1", "7M1", "8M1",
  "5M2", "6M2", "7M2", "8M2",
  "5S1", "6S1", "7S1", "8S1",
  "5S2", "6S2", "7S2", "8S2"
 };

 
SlowSoftSerial sss(0, 1);

void setup() {
  // Set up the USB serial port for monitoring status
  Serial.begin(9600);

  while (!Serial);    // wait for the USB to start up
}

void loop() {
  int mode;
  int chr;
  
  for (mode=0; mode < NUM_MODES; mode++) {
    Serial.print("Sending at ");
    Serial.println(dps_names[mode]);
    
    sss.begin(9600, dps[mode]);
    sss.print("\nTesting ");
    sss.print(dps_names[mode]);
    sss.print(" now\n");
    sss.flush();

    // If you have a loopback jumper installed, receive what
    // we just transmitted (it'll be in the buffer, up to 64).
    // However, note that the ASCII characters we sent won't fit
    // in 5-bit serial words at all, and only the digits will
    // fit in 6-bit serial words, so only the 7-bit and 8-bit
    // messages will be readable in the serial monitor.
    while (sss.available()) {
      chr = sss.read();
      if (isprint(chr) || isspace(chr)) {
        Serial.write(chr);
      } else {
        Serial.write('<');
        Serial.print(chr, HEX);
        Serial.write('>');
      }
    }
    
    sss.end();

    delay(100);
  }

  Serial.println("Repeating in a few seconds ...");
  delay(5000);
}
