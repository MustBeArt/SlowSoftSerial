# Packet-Based Test Framework for SlowSoftSerial

SlowSoftSerial is a bit-banged serial port driver that supports a wide variety of baud rates, parity settings, stop bits, etc. Testing all of these modes is quite tedious to do by hand. This test framework is an attempt to test SlowSoftSerial more thoroughly by automating the process.

The idea is to connect two microcontrollers together through serial ports. One device is a Teensy (or other supported target) running SlowSoftSerial, and is referred to as the unit under test or UUT. The other device could be anything with a serial port, and is referred to as the controller. Both devices start with a standard serial port configuration (9600 8N1), and exchange a stream of framed, CRC-checked packets. The controller sends command packets and the UUT responds. Some command packets include instructions to change baud rate, parity, stop bits, etc., so that the controller can sequence the UUT through its repertoire of modes. The UUT always acknowledges using the old settings before making the specified change, so that the controller always knows what's happening.

The goal is that, when every test is passing, the operator need only monitor status reports from the controller to verify that all is well. We assume that when things go wrong, the operator is able to monitor the serial communications between the UUT and the controller and debug. No attempt is made to enable the controller to diagnose problems on its own, or to correct them on the fly. The controller is only expected to detect problems, report the fact, and stop.

Initial work was done with a Raspberry Pi Pico board (first generation) with the RP2040 microcontroller as the controller, since it has hardware UART support for baud rates in SlowSoftSerial's range. This is a wholly independent implementation of a serial port, which helps to validate SlowSoftSerial's implementation. However, the RP2040's UART API does not support 1.5 stop bits, or mark or space parity, so another approach will be needed to exercise all the modes of SlowSoftSerial. This could be done with the programmable I/O (PIO) controller on the RP2040 in place of the UART, but that implementation would likely be buggier than SlowSoftSerial. I haven't been able to find another microcontroller or computer serial port that is flexible enough to test everything in SlowSoftSerial.

The fallback is to use a second Teensy running SlowSoftSerial as the test controller. This is not an independent implementation in any sense, though. To achieve some degree of independence, the data in both directions is captured using a Saleae logic analyzer. The async serial analyzer provided by Saleae is helpful in manually confirming many details of the various configurations. To make this __much__ easier, a plug-in for Saleae Logic 2 was written to display test protocol packets based on the characters found by Saleae's async serial analyzer. [SlowSoftSerial Test Packets Decoder](https://github.com/MustBeArt/SlowSoftSerial-test-packets-decoder)

Once again, though, this configuration is not flexible enough to fully test SlowSoftSerial, because of limitations in Saleae's async analyzer. So, the raw data captured by the logic analyzer is exported in Saleae's Logic 2.0 binary format, and analyzed in detail offline by the Python script found in the [autotest-analyze](https://github.com/MustBeArt/SlowSoftSerial/tree/testrig/test/autotest-analyze) directory. That program understands enough of the test protocol to follow along as the controller walks the UUT through any combination of baud rates and serial configurations supported by SlowSoftSerial.

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

We use this 8-character encoding of the CRC even when using a data word width of 8 bits.

### Packet Format

The first character (after the leading FEND) of each packet is a command/response indicator.

|Value|Description|
|-----|-----------|
| 0x00| Command sent from controller to UUT
| 0x01| Response sent from UUT to controller
| 0x02| Debug info sent by UUT, not requested by controller|
|Other values| Reserved|

Normally, the UUT is silent unless responding to a command from the controller, and the command specifies what response is required. However, the UUT may send a debug packet at any time.

The second character is a command code. The controller may send any command code,
but the UUT is required to respond with the same command code it is responding to. The rest of the response depends on the command.

|Value|Name|Description|
|-----|----|-----------|
| 0x00|NOP| No operation.|
| 0x01|ID|Request UUT version info.|
| 0x02|ECHO|CTLR sends any number of arbitrary characters, and the UUT echoes them back verbatim.|
| 0x03|BABBLE|CTLR sends a length, and the UUT sends back that many pseudo-random characters.|
| 0x04|PARAMS|Change any or all of the baud rate, word width, stop bits, and parity type.|
|0x1F|EXT|Reserved for expansion of the command codes.|

### Packet Definitions

#### NOP Packet
The NOP packet does nothing but trigger an acknowledgement from the UUT.

The CTLR may include any number of additional characters in the NOP packet. The UUT includes such characters in the CRC check, but does not process them or echo them back.

If the CRC check passes, the UUT shall respond with a two-byte NOP response packet.

#### ID Packet
The ID packet requests version information from the UUT.

If the CRC check passes, the UUT shall respond with an ID response packet. After the command code, it shall include a human-readable ASCII message describing its platform, SlowSoftSerial library version, and UUT program version. (This is only useful with 7- or 8-bit word widths.)

#### ECHO Packet
The ECHO packet requires the UUT to send back the entire contents of the packet.

If the CRC check passes, the UUT shall respond with an ECHO response packet. The packet shall be identical to the command packet, except for the command/response indicator.

The UUT shall be able to respond to ECHO packets of at least 10,000 characters.

The UUT is not permitted to begin responding before checking the CRC, so it may not stream the response without first buffering the entire packet.

#### BABBLE Packet
The BABBLE packet requires the UUT to send a pseudo-random block of characters of a specified length. The UUT may use any method to generate the characters. The CTLR relies on the CRC to check their correctness.

The CTLR encodes the length the same way CRCs are encoded, as 8 characters each containing 4 bits of the value, most significant nibble first.

#### PARAMS Packet
The PARAMS packet requires the UUT to first respond with a PARAMS response packet, echoing back the parameters received, and then switch to using the specified baud rate, data word width, number of stop bits, and parity type.

| Field | Characters | Description |
|-------|------------|-------------|
| Baud rate | 8 | Baud rate in millibauds per second, encoded the same way as CRCs. For example, 9600 baud is 9,600,000 millibaud, which is 00927c00 in hex, so it would be encoded as characters 00 00 09 02 07 0c 00 00 |
| Config | 8 | Serial configuration word as defined in SlowSoftSerial.h, encoded the same way as CRCs. For example, 8N1 is 0413 in hex, so it would be encoded as characters 00 00 00 00 00 04 01 03 |

