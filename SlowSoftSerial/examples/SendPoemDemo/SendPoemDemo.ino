#include "SlowSoftSerial.h"

#define BAUDRATE  9600

#define RX_PIN 0
#define TX_PIN 1
#define CTS_PIN 18

SlowSoftSerial sss((uint8_t)RX_PIN, (uint8_t)TX_PIN);

//********************************************************
//
// Main sketch
//
//********************************************************

const char poem[] =
"\n\n\nI met a traveler from an antique land\n"
"Who said: two vast and trunkless legs of stone\n"
"Stand in the desert . . . Near them, on the sand,\n"
"Half sunk, a shattered visage lies, whose frown,\n"
"And wrinkled lip, and sneer of cold command,\n"
"Tell that its sculptor well those passions read\n"
"Which yet survive, stamped on these lifeless things,\n"
"The hand that mocked them, and the heart that fed:\n"
"And on the pedestal these words appear:\n"
"\"My name is Ozymandias, king of kings:\n"
"Look on my works, ye Mighty, and despair!\"\n"
"Nothing beside remains.  Round the decay\n"
"Of that colossal wreck, boundless and bare\n"
"The lone and level sands stretch far away.\n     ";

void send_poem_randomly(void) {
  int len = strlen(poem) - 5;
  int i = 0;
  int n, j;
  long t;

  while (i < len) {
    t = random(0,150000);  // microseconds
    n = random(1,5);
    if (t > 0) {
      delayMicroseconds(t);
    }
    for (j=0; j < n; j++) {
      Serial.write(poem[i]);
      sss.write(poem[i++]);
    }
  }
}

void send_poem_fast(void) {
  int len = strlen(poem);
  int i = 0;

  for (i=0; i < len; i++) {
    Serial.write(poem[i]);
    sss.write(poem[i]);
  }
}

void setup() {
  sss.begin(BAUDRATE);
  Serial.begin(BAUDRATE);

  while (!Serial);

  sss.attachCts((uint8_t)CTS_PIN);
}


void loop() {

  while (1) {
    send_poem_randomly();
    send_poem_fast();
    delay(2000);
  }

}
