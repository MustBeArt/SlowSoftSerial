#include "SlowSoftSerial.h"

// It's ok to have several SlowSoftSerial objects,
// but only one can be active at the same time.
SlowSoftSerial sss1(0,1);
SlowSoftSerial sss2(2,3);

void setup() {
  Serial.begin(9600);   // USB port for debug messages

}

void loop() {
  Serial.println("Activating port 1");
  sss1.begin(9600);
  sss1.println("This message goes out port 1");
  sss1.flush();
  sss1.end();

  Serial.println("Activating port 2");
  sss2.begin(1200);
  sss2.println("Now on port 2 at a different baud rate!");
  sss2.flush();
  sss2.end();

  Serial.println("Repeating in a few seconds ...);
  delay(5000);

}
