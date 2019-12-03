# SlowSoftSerial serial library for Teensyduino

SlowSoftSerial is another alternative serial port library for
a Teensy 3.x board under the Arduino environment.

It was originally created for the
[Cadetwriter](https://github.com/IBM-1620/Cadetwriter) project,
because the hardware serial ports on Teensy 3.5 don't work at
standard baud rates slower than 1200 baud, and AltSoftSerial
doesn't work on the same pins as the Serial1 UART or at all the
needed word length, parity, and stop bit settings.

## Features

* Wide range of (slower) baud rates from 1 baud to 9600+ baud

* RX and TX on ANY two GPIO pins on the Teensy

* Simultaneous receive and transmit

* Full range of data word length, parity, and stop bit settings
per the Arduino serial API definition: 5N1, 6N1, 7N1, 8N1, 5N2,
6N2, 7N2, 8N2, 5E1, 6E1, 7E1, 8E1, 5E2, 6E2, 7E2, 8E2, 5O1, 6O1,
7O1, 8O1, 5O2, 6O2, 7O2, and 8O2

* Arbitrary non-standard baud rates like 45.45 baud or 123.45 baud

* Support for inverted signaling voltages

* Imposes minimal (< 3 microsecond) interrupt latency on other libraries

* Can work simultaneously with USB Serial, hardware UARTs Serial1 - Serial6,
and AltSoftSerial

* Standard Arduino Serial API interface, so most everything works
the way you expect

## Limitations

* Sensitive to interrupt latency from other libraries. Speed limit
of 9600 baud assumes other libraries impose up to 15 microseconds of
interrupt latency. Higher speeds (up to about 28800) are possible if
no other libraries are adding latency.

* Uses two of the Periodic Interrupt Timers (of which there are only
four on the Teensy 3.x)

* Only one active SlowSoftSerial port at a time is currently supported,
but you can have multiple ports defined and switch between them.

* No support (yet) for built-in flow control handshaking (because
Cadetwriter does flow control at the application level)

## Installation

Assuming you're already programming a Teensy 3.x board under the
Arduino IDE environment,

* Clone or download this repository

* Copy or move the SlowSoftSerial folder into your Arduino libraries
directory in your Arduino sketchbook. You can find or change the
location of your sketchbook in the Arduino IDE's Preferences, under
Settings.

* Restart the Arduino IDE

## Documentation

See [SlowSoftSerial Design Notes](SlowSoftSerial%20design%20notes.pdf)

## Examples

Example sketches demonstrating SlowSoftSerial port are included in
the library. Once you've installed the library, you can open the
examples from the Files, Examples menu in the Arduino IDE.

* __SendPoemDemo__ transmits (only) through a SlowSoftSerial port.
It first sends the poem in random short bursts, exercising the
port's ability to start and stop transmitting on any character
boundary. Then it transmits the same poem at full speed, and repeats
forever. It simultaneously sends the same characters to the USB Serial
port, so you can monitor its progress in the Arduino IDE's serial
monitor.

* __SerialPassthrough__ connects a SlowSoftSerial port to the USB
Serial port, in both directions. Wire up your port to a terminal
emulator and you can type back and forth between the terminal emulator
and the Arduino IDE's serial monitor. Try pasting a longer text into
the terminal emulator (but note, there is no flow control built in
to SlowSoftSerial at this time).

* __DuelingPorts__ demonstrates the use of multiple SlowSoftSerial
ports (with only one being active at a time).

* __VariousParameters__ demonstrates the use of all the various
data word length, parity type, and stop bit length parameters
supported by SlowSoftSerial. It sends a short text message with
each parameter setting, while simultaneously trying to receive
with the same setting. You can watch the transmissions with a
terminal emulator or serial analyzer, or you can hook up a jumper
between the RX and TX pins and see the results in the USB serial
monitor.

## License

This project is licensed under GPLv3.

## Maturity = Early Alpha

SlowSoftSerial just started working at all on 2019-12-01. It has not
yet been subjected to extensive testing, and there is more work to be
done. It might work for you, if you're feeling adventurous.
