# Packet-Based Test Framework for SlowSoftSerial

SlowSoftSerial is a bit-banged serial port driver that supports a wide variety of baud rates, parity settings, stop bits, etc. Testing all of these modes is quite tedious to do by hand. This test framework is an attempt to test SlowSoftSerial more thoroughly by automating the process.

The idea is to connect two microcontrollers together through serial ports. One device is a Teensy (or other supported target) running SlowSoftSerial, and is referred to as the unit under test or UUT. The other device could be anything with a serial port, and is referred to as the controller. Both devices start with a standard serial port configuration (9600 8N1), and exchange a stream of framed, CRC-checked packets. The controller sends command packets and the UUT responds. Some command packets include instructions to change baud rate, parity, stop bits, etc., so that the controller can sequence the UUT through its repertoire of modes. The UUT always acknowledges using the old settings before making the specified change, so that the controller always knows what's happening.

Initial work is being done with a Raspberry Pi Pico board (first generation) with the RP2040 microcontroller as the controller, since it has hardware UART support for baud rates in SlowSoftSerial's range. This is a wholly independent implementation of a serial port, which helps to validate SlowSoftSerial's implementation. However, the RP2040's UART does not support 1.5 stop bits, or mark or space parity, so another approach will be needed to exercise all the modes of SlowSoftSerial. This could involve the programmable I/O (PIO) controller on the RP2040 in place of the UART, or it could involve a second Teensy running SlowSoftSerial (at the expense of independence), or it could involve some other device. TBD.

## Packet Format

### Framing and Data Transparency

All packets are sent using a binary-transparent framing format based on that used in [SLIP](https://tools.ietf.org/html/rfc1055) (Serial Line IP) and [KISS TNC](http://www.ax25.net/kiss.aspx) protocols. These formats use a special character that is only transmitted to mark the beginning and the end of every frame, which is called FEND for _Frame End_. When that character appears in the data stream, it is replaced with a two-character sequence, introduced by a special character that is only transmitted for this purpose. That character is called FESC for _Frame Escape_, and it also must be replaced with a two-character sequence when it appears in the data stream. The second character of a two-character sequence is either TFEND for _Transposed Frame End_ or TFESC for _Transposed Frame Escape_. TFEND and TFESC do not need to be treated specially when they appear in the data stream; they only have special meaning when immediately following a FESC character.

The standard values for these special characters are 0xC0, 0xDB, 0xDC, and 0xDD. These values were probably chosen because they don't appear in ASCII text, but there's a problem. They can only be used when the data word width is at least 8 bits. SlowSoftSerial supports word widths down to 5 bits, so we must choose non-standard values that fit within 5 bits. Here are the ones we use:

|  Char | Code |
|-------|------|
|  FEND | 0x10 |
|  FESC | 0x1B |
| TFEND | 0x1C |
| TFESC | 0x1D |

### Error Checking

All packets are sent with a 32-bit CRC error check at the end. Packets that do not pass the CRC check are ignored.

The CRC must also be transmitted in a way that is compatible with data word widths as narrow as 5 bits. We transmit the CRC as 8 characters, each containing 4 bits of the CRC. The CRC bits are the least significant bits in the characters, and the remaining 1 to 4 bits in the character are zero. The first character contains the four most significant bits of the CRC, and so on; the last character of the packet contains the four least significant bits of the CRC.

That means that the 0x10 bit of every character in the CRC encoding is 0. Since that bit is a 1 in every special character used for framing, the characters of the CRC need not be processed for data transparency in the framing process.

### Packet Format

more here soon
