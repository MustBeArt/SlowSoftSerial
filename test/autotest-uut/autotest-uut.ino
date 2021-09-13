// Unit-Under-Test Driver for Automated Testing of SlowSoftSerial
// See README.md for discussion and protocol definition.
// 
// This Arduino (Teensyduino) sketch runs on the target platform,
// such as a Teensy 3.x or 4.x board. Another program runs on
// another host connected via the SlowSoftSerial port, which we
// call the controller. The controller is in charge of sequencing
// this program through (some of) its paces.
//
// We make some effort to surface explanatory info onto the serial
// link, but only what's easy. The assumption is that a serial
// analyzer is available to observe what went wrong.
//
// This program also displays some trace information on the console
// (the USB serial port on the target board). This can be minimal,
// or it can include a full trace of packets sent and received.

#include "SlowSoftSerial.h"

#define BUILTIN_LED 13

// Do you want noisy packet trace output?
#define PACKET_TRACE

const char VERSION_INFO[] = "SlowSoftSerial Tester 0.02";
const char DBG_MSG_UNKNOWN_COMMAND_CODE[] = "Unknown command code";
const char DBG_MSG_INVALID_PARAMS[] = "Invalid baud rate or serial params";

/* Special characters for KISS */
#define FEND    0x10  /* Frame End */
#define FESC    0x1B  /* Frame Escape */
#define TFEND   0x1C  /* Transposed frame end */
#define TFESC   0x1D  /* Transposed frame escape */

#define CHARACTERS_IN_CRC 8 // 32-bit CRC, sent 4 bits per character

// Packet Command Structure
#define HEADER_LEN 2
//   First Byte:
#define DIR_CMD 0
#define DIR_RSP 1
#define DIR_DBG 2
//   Second Byte:
#define CMD_NOP    0
#define CMD_ID     1
#define CMD_ECHO   2
#define CMD_BABBLE 3
#define CMD_PARAMS 4
#define CMD_EXT    0x1f

// Protocol spec requires us to handle ECHO or BABBLE payloads of up to 10,000 characters.
#define PACKET_BUF_SIZE (10000 + HEADER_LEN + 2*CHARACTERS_IN_CRC)
unsigned char packet_buf[PACKET_BUF_SIZE];

#define NUMBER_OF_VALID_SERIAL_CONFIGS  60
uint16_t valid_serial_configs[NUMBER_OF_VALID_SERIAL_CONFIGS] = {
                SSS_SERIAL_5N1,  SSS_SERIAL_6N1,  SSS_SERIAL_7N1,  SSS_SERIAL_8N1,
                SSS_SERIAL_5N15, SSS_SERIAL_6N15, SSS_SERIAL_7N15, SSS_SERIAL_8N15,
                SSS_SERIAL_5N2,  SSS_SERIAL_6N2,  SSS_SERIAL_7N2,  SSS_SERIAL_8N2,
                SSS_SERIAL_5E1,  SSS_SERIAL_6E1,  SSS_SERIAL_7E1,  SSS_SERIAL_8E1,
                SSS_SERIAL_5E15, SSS_SERIAL_6E15, SSS_SERIAL_7E15, SSS_SERIAL_8E15,
                SSS_SERIAL_5E2,  SSS_SERIAL_6E2,  SSS_SERIAL_7E2,  SSS_SERIAL_8E2,
                SSS_SERIAL_5O1,  SSS_SERIAL_6O1,  SSS_SERIAL_7O1,  SSS_SERIAL_8O1,
                SSS_SERIAL_5O15, SSS_SERIAL_6O15, SSS_SERIAL_7O15, SSS_SERIAL_8O15,
                SSS_SERIAL_5O2,  SSS_SERIAL_6O2,  SSS_SERIAL_7O2,  SSS_SERIAL_8O2,
                SSS_SERIAL_5M1,  SSS_SERIAL_6M1,  SSS_SERIAL_7M1,  SSS_SERIAL_8M1,
                SSS_SERIAL_5M15, SSS_SERIAL_6M15, SSS_SERIAL_7M15, SSS_SERIAL_8M15,
                SSS_SERIAL_5M2,  SSS_SERIAL_6M2,  SSS_SERIAL_7M2,  SSS_SERIAL_8M2,
                SSS_SERIAL_5S1,  SSS_SERIAL_6S1,  SSS_SERIAL_7S1,  SSS_SERIAL_8S1,
                SSS_SERIAL_5S15, SSS_SERIAL_6S15, SSS_SERIAL_7S15, SSS_SERIAL_8S15,
                SSS_SERIAL_5S2,  SSS_SERIAL_6S2,  SSS_SERIAL_7S2,  SSS_SERIAL_8S2,
                };

// Current Serial Parameters
double current_baud_rate = 9600.0;
uint16_t current_serial_config = SSS_SERIAL_8N1;
unsigned int charactersize_mask = 0x00ff;

// Here's our serial port connected to the controller.
SlowSoftSerial sss(0,1);


// Convert the serial port's read() method, which may not be blocking,
// into one that definitely is.
int blocking_read(void)
{
  while (! sss.available());
  return sss.read();
}


// Get one complete frame of data from the serial port, and places it in buf.
//
// Returns the number of bytes placed in buf, or 0 if the frame was ill-formed
// or if no data is available.
int get_frame(unsigned char *buf, int max_length)
{
unsigned char *bufp;
int chr;

bufp = buf;

// Eat bytes up to the first FEND
while (blocking_read() != FEND);

// Eat as many FENDs as we find
while ((chr = blocking_read()) == FEND);

while ((chr != FEND) && (bufp <= buf+max_length)) {
  if (chr == FESC) {
    chr = blocking_read();
    if (chr == TFESC) {
      *bufp++ = FESC;       // put escaped character in buffer
    } else if (chr == TFEND) {
      (*bufp++ = FEND);
    } else {
      Serial.println("Ill formed frame");
      return 0;             // ill-formed frame
    }
  } else {
    *bufp++ = (unsigned char)chr;
  }

  chr = blocking_read();
  }

if (bufp > buf+max_length)
  {
  Serial.println("Frame too long");
  return 0;               // frame too big for buffer
  }

return (bufp - buf);      // return length of buffer
}


// Table used to expedite computation of 32-bit CRC
static uint32_t crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};


// One byte worth of CRC computation
uint32_t crc_update(uint32_t crc, byte data)
{
    byte tbl_idx;
    tbl_idx = crc ^ (data >> (0 * 4));
    crc = crc_table[tbl_idx & 0x0f] ^ (crc >> 4);
    tbl_idx = crc ^ (data >> (1 * 4));
    crc = crc_table[tbl_idx & 0x0f] ^ (crc >> 4);
    
    return crc;
}


// The protocol encodes integers, including the CRC used for error detection,
// in the least significant four bits of eight consecutive characters. This
// encoding makes it work with serial word sizes less than 8 bits.
uint32_t decode_uint32(unsigned char *buf) {
  uint32_t value;

  // The buffer contains 8 characters which are supposed to be 4 bits wide.
  // We don't check, so if any characters are > 0x0F, that will cause the
  // result to be wrong. That's what we want for a CRC check.
  value = buf[0] << 28
        | buf[1] << 24
        | buf[2] << 20
        | buf[3] << 16
        | buf[4] << 12
        | buf[5] << 8
        | buf[6] << 4
        | buf[7]
        | (buf[0] & 0xF0)     // Catch any stray high bits in the top nybble
        ;

  return value;
}

void encode_uint32(unsigned char *buf, uint32_t value) {
  buf[0] = (value >> 28) & 0x0f;
  buf[1] = (value >> 24) & 0x0f;
  buf[2] = (value >> 20) & 0x0f;
  buf[3] = (value >> 16) & 0x0f;
  buf[4] = (value >> 12) & 0x0f;
  buf[5] = (value >>  8) & 0x0f;
  buf[6] = (value >>  4) & 0x0f;
  buf[7] = value & 0x0f;
}


// Given a buffer whose final 8 bytes contain an encoded CRC,
// check that the CRC matches the one we compute from all the
// bytes in the buffer before the CRC.
bool check_packet_crc(unsigned char *buf, int len)
{
  uint32_t crc = ~0L;
  uint32_t packet_crc;
  
  for (int i=0; i < (len-CHARACTERS_IN_CRC); i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  packet_crc = decode_uint32(buf + len - CHARACTERS_IN_CRC);

  // Check for CRC equality and return the boolean result
  return crc == packet_crc;
}


// Given a buffer with room at the end for 8 more bytes,
// compute the CRC of all the bytes in the buffer and
// encode the CRC into 8 bytes after the buffer.
int add_packet_crc(unsigned char *buf, int len)
{
  uint32_t crc = ~0L;
  
  for (int i=0; i < len; i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  encode_uint32(buf+len, crc);

  return(len+CHARACTERS_IN_CRC);
}


// Given a buffer of bytes, output it as a frame using the
// modified SLIP/KISS framing protocol specified in README.md.
void put_frame(unsigned char *buf, int len)
{
  int  i;

  digitalWrite(BUILTIN_LED, 1); // LED on for transmitting

  sss.write(FEND);        /*  all frames begin with FEND */

  for (i=0; i<len; i++)
    {
    switch (buf[i])       /*  translate for data transparency */
      {
      case FEND:
        sss.write(FESC);
        sss.write(TFEND);
        break;
      case FESC:
        sss.write(FESC);
        sss.write(TFESC);
        break;
      default:
        sss.write(buf[i]);
        break;
      }
    }

  sss.write(FEND);       /*  all frames end with FEND */

  sss.flush();                    // Wait for transmission to complete
  digitalWrite(BUILTIN_LED, 0);   // turn off LED
  
#ifdef PACKET_TRACE
  dump_buf(buf, len, 1);
#endif
}


// Dump information about the packet that's currently
// in the global packet_buf.
#ifdef PACKET_TRACE
void dump_packet_buf(int len, bool crc_good) {
  dump_buf(packet_buf, len, crc_good);
}


// Dump information about the packet in any buffer.
void dump_buf(unsigned char *buf, int len, bool crc_good) {
  static long msg_count = 0;
  char fbuf[20];    // local formatting buffer
  
  Serial.print("Frame: ");
  Serial.print(msg_count++);

  if (len >= HEADER_LEN+CHARACTERS_IN_CRC) {
    switch(buf[0]) {
      case DIR_CMD:   Serial.print("  Cmd");
                      break;
      case DIR_RSP:   Serial.print("  Rsp");
                      break;
      case DIR_DBG:   Serial.print("  Dbg");
                      break;
      default:        sprintf(fbuf, " 0x%2X", (int)buf[0]);
                      Serial.print(fbuf);
                      break;
    }

    switch(packet_buf[1]) {
      case CMD_NOP:      Serial.print("    NOP");
                         break;
      case CMD_ID:       Serial.print("     ID");
                         break;
      case CMD_ECHO:     Serial.print("   ECHO");
                         break;
      case CMD_BABBLE:   Serial.print(" BABBLE");
                         break;
      case CMD_PARAMS:   Serial.print(" PARAMS");
                         break;
      default:           sprintf(fbuf, "   0x%2X", (int)buf[1]);
                         Serial.print(fbuf);
                         break;
    }
  }

  Serial.print(" Len: ");
  Serial.print(len);
  Serial.print(" CRC: ");
  Serial.print(crc_good ? "Good" : "Bad");
  Serial.println();
}
#endif // PACKET_TRACE


// Fill up a buffer with random nonsense, for the BABBLE command.
// Limit the character values to what will fit into the current
// serial word size.
void random_fill(unsigned char *buf, int len) {
  int i;
  int charactersize_max = charactersize_mask + 1;   // e.g., for 5 bits 0b00011111 + 1 = 0b00100000,
                                                    // which is the (exclusive) max value of a character

  for (i=0; i < len; i++) {
    buf[i] = random(charactersize_max);
  }
}


// Check that the received value for baud rate is plausible.
// The received baud rate is an integer in millibaud, and the
// range of rates we'll allow is 1 to 115200 baud.
bool validate_baud_rate(uint32_t value) {
  if (value < 1000 || value > 115200000) {
    Serial.print("Bogus baud rate ");
    Serial.println(value);
    return false;
  }
    
  return true;
}


// Check that the received value for serial configuration is
// one of the ones we know how to do.
bool validate_serial_config(uint32_t value) {
  int i;

  for (i=0; i < NUMBER_OF_VALID_SERIAL_CONFIGS; i++) {
    if (value == valid_serial_configs[i]) {
      return true;
    }
  }

  Serial.print("Bogus serial config 0x");
  Serial.println(value, HEX);
  
  return false;    // Didn't match any known serial configuration
}


// Change baud rate and serial configuration in response to PARAMS packet
void change_serial_params(double baud, uint16_t config) {
  sss.end(false);       // Stop serial port but don't release the pins
  sss.begin(baud, config);
}


// Arduino one-time setup at startup.
void setup() {
  Serial.begin(9600);
  sss.begin(9600, SSS_SERIAL_8N1);

  pinMode(BUILTIN_LED, OUTPUT);   // LED to flash on transmit
  digitalWrite(BUILTIN_LED, 0);

  while (!Serial);

  Serial.println(VERSION_INFO);
  Serial.println("Waiting for frames ...");
}


// Arduino main loop, runs forever.
void loop() {
  int len;
  bool crc_good;
  uint32_t babble_length, baud_rate, serial_config;   // received packet parameters decoded
  
  len = get_frame(packet_buf, PACKET_BUF_SIZE);

  if (len > 8) {
    crc_good = check_packet_crc(packet_buf, len);
  } else {
    crc_good = 0;
  }

#ifdef PACKET_TRACE
  dump_packet_buf(len, crc_good);
#endif

  if (crc_good && packet_buf[0] == DIR_CMD) {
        switch(packet_buf[1]) {
          case CMD_NOP:      packet_buf[0] = DIR_RSP;
                             // packet_buf[1] = CMD_NOP;
                             // Ignore any further packet contents
                             put_frame(packet_buf, add_packet_crc(packet_buf, HEADER_LEN)); // NOP response
                             break;
                             
          case CMD_ID:       packet_buf[0] = DIR_RSP;
                             // packet_buf[1] = CMD_ID;
                             memcpy(packet_buf+HEADER_LEN, VERSION_INFO, strlen(VERSION_INFO) + 1);
                             put_frame(packet_buf, add_packet_crc(packet_buf, HEADER_LEN + strlen(VERSION_INFO) + 1)); // ID response
                             break;
                             
          case CMD_ECHO:     packet_buf[0] = DIR_RSP;
                             // packet_buf[1] = CMD_ECHO;
                             // leave the entire packet payload alone and echo it
                             put_frame(packet_buf, add_packet_crc(packet_buf, len-CHARACTERS_IN_CRC));  // ECHO response
                             break;
                             
          case CMD_BABBLE:   if (len >= HEADER_LEN + CHARACTERS_IN_CRC) {
                                babble_length = decode_uint32(packet_buf+HEADER_LEN);
                                if (babble_length <= PACKET_BUF_SIZE-(HEADER_LEN+2*CHARACTERS_IN_CRC)) {
                                  packet_buf[0] = DIR_RSP;
                                  // packet_buf[1] = CMD_BABBLE;
                                  // leave the babble length alone
                                  random_fill(packet_buf+HEADER_LEN+CHARACTERS_IN_CRC, babble_length);
                                  put_frame(packet_buf, add_packet_crc(packet_buf, HEADER_LEN + CHARACTERS_IN_CRC + babble_length));
                                }
                              }
                              break;
                             
          case CMD_PARAMS:   if (  (len >= HEADER_LEN + 2 * CHARACTERS_IN_CRC)
                                && (baud_rate = decode_uint32(packet_buf+HEADER_LEN))
                                && (serial_config = decode_uint32(packet_buf+HEADER_LEN+CHARACTERS_IN_CRC))
                                && validate_baud_rate(baud_rate)
                                && validate_serial_config(serial_config)
                                ) {
                                packet_buf[0] = DIR_RSP;
                                // packet_buf[1] = CD_PARAMS;
                                // leave the parameters alone and echo them back
                                put_frame(packet_buf, add_packet_crc(packet_buf, len-CHARACTERS_IN_CRC));     // PARAMS ack response
                                change_serial_params(0.001 * (double)baud_rate, (uint16_t)serial_config);
                             } else {
                               packet_buf[0] = DIR_DBG;
                               // leave the CMD_PARAMS command code and parameters in packet_buf[1]
                               memcpy(packet_buf+HEADER_LEN+2*CHARACTERS_IN_CRC, DBG_MSG_INVALID_PARAMS, strlen(DBG_MSG_INVALID_PARAMS));
                               put_frame(packet_buf, add_packet_crc(packet_buf, HEADER_LEN + 2*CHARACTERS_IN_CRC + strlen(DBG_MSG_INVALID_PARAMS)));
                             }
                             break;
                             
          default:           packet_buf[0] = DIR_DBG;
                             // leave the bad command code in packet_buf[1]
                             memcpy(packet_buf+HEADER_LEN, DBG_MSG_UNKNOWN_COMMAND_CODE, strlen(DBG_MSG_UNKNOWN_COMMAND_CODE));
                             put_frame(packet_buf, add_packet_crc(packet_buf, HEADER_LEN + strlen(DBG_MSG_UNKNOWN_COMMAND_CODE)));
                             break;
    }
  }
}
