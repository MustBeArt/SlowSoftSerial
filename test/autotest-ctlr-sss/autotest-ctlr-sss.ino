// Controller Driver for Automated Testing of SlowSoftSerial
// See README.md for discussion and protocol definition.
//
// This program runs as test controller on a Teensy running SlowSoftSerial,
// connected via serial port to the target platform (UUT for Unit Under Test),
// which is also a Teensy running SlowSoftSerial.
//
// It is in charge of sequencing the UUT through (some of) its paces.
//


#include <LibPrintf.h>
#include "SlowSoftSerial.h"


#define puts(x) printf(x "\n");

#define BUILTIN_LED 13

// By convention, we start every test at 9600 baud, 8N1
// Current Serial Parameters
double current_baud = 9600.0;
int current_width = SSS_SERIAL_DATA_8;
int current_parity = SSS_SERIAL_PARITY_NONE;
int current_stopbits = SSS_SERIAL_STOP_BIT_1;
uint16_t current_serial_config = SSS_SERIAL_8N1;
unsigned int charactersize_mask = 0x00ff;

// Here's our serial port connected to the controller.
SlowSoftSerial sss(0,1);

unsigned char width_masks[] = { 0x00, 0x1F, 0x3F, 0x7F, 0xFF };
#define CURRENT_WIDTH_MASK (width_masks[current_width >> 8])  // convert current_width to a mask
#define CURRENT_WIDTH_BITS ((current_width >> 8) + 4)         // convert current_width to # of bits

// In serial configuration changes, 0 means leave that parameter alone
#define STET 0

// Packet Command Structure
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

#define MAX_DATA_LEN  10000
#define	BUFLEN (MAX_DATA_LEN*2+10)  // big enough for all bytes to be transposed
unsigned char buffer[BUFLEN];


// Special characters for framing
#define	FEND		0x10	// Frame End
#define	FESC		0x1B	// Frame Escape
#define	TFEND		0x1C	// Transposed frame end
#define	TFESC		0x1D	//Transposed frame escape

#define CHARACTERS_IN_CRC 8 // 32-bit CRC, sent 4 bits per character

int word_width = 8;
#define WORD_WIDTH_MASK   (0xFF >> (8-word_width))

// What to do when there's nothing to do
void tight_loop_contents(void) {
}


// Get one character from the interrupt-driven receive buffer,
// if one is available before the specified absolute timeout.
//
// Returns true if a characters was received, false if not.
// The character is placed in *chr_p
//
bool serial_getc_timeout(unsigned long tmax, unsigned char *chr_p) {
  while (1) {
    if (sss.available()) {
      *chr_p = sss.read();
      return true;
    } else if (millis() > tmax) {
      return false;     // timeout before a character was available
    } else {
      tight_loop_contents();
    }
  }
}


// Get one complete frame of data from the serial port, and place it in buf,
// if the complete frame arrives within a reasonable time based on the current
// communications parameters and the expected response size. Expected size
// includes only the data characters and not the header, framing, stuffing,
// or CRC.
//
// Returns the number of bytes placed in buf, or 0 if the frame was
// ill-formed or if a complete frame was not received.
int get_frame_with_expected_data_size(unsigned char *buf, int expected_size_in_characters)
{
  unsigned char	*bufp;
  unsigned char	chr;
  uint32_t timeout = 10 + ((long)expected_size_in_characters*2 + 10) * (CURRENT_WIDTH_BITS + 4) * 1000 / current_baud;
  unsigned long timeout_time = millis() + timeout;

  newframe:

  bufp = buf;

  // eat bytes up to first FEND
  while (serial_getc_timeout(timeout_time, &chr) && chr != FEND)
    ;

  // eat as many FENDs as we find
  while (serial_getc_timeout(timeout_time, &chr) && chr == FEND)
    ;

  // fill the buffer with received characters, with de-escaping
  while ((chr != FEND) && (bufp < buf+BUFLEN)) {
    if (chr == FESC) {
      (void) serial_getc_timeout(timeout_time, &chr); // don't worry about timeout
      if (chr == TFESC) {
        *bufp++ = FESC;		// put escaped character in buffer
      } else if (chr == TFEND) {
        *bufp++ = FEND;
      } else {
        puts("Ill-formed frame");
        return 0;			// ill-formed frame
      }
    } else {
      *bufp++ = (unsigned char)chr;		// put unescaped character in buffer
    }

    if ( ! serial_getc_timeout(timeout_time, &chr)) { // Get next character
      printf("Frame timeout %ldms\n", timeout);
      return 0;       // timeout before a full frame arrives
    }
  }

  if (bufp >= buf+BUFLEN)
    {
    puts("Warning: frame is too big!  Discarded.\n");
    goto newframe;
    }

  // puts("Good frame");
  return (bufp - buf);			// return length of buffer
}


// Transmit a frame through the interrupt-driven tx buffer,
// adding framing on the fly.
//
// Blocks in sss.write() if the frame is larger than the space
// available in the transmit buffer.
void put_frame(unsigned char *buf, int len)
{
  int	i;

  sss.write(FEND);				// all frames begin with FEND

  for (i=0; i<len; i++)
    {
    switch (buf[i])				//  translate for data transparency
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

  sss.write(FEND);				//  all frames end with FEND
}


// Wrapper for put_frame that lights up the on-board LED
// while transmission is in progress. Note that this means
// the function doesn't return until all the characters
// have gone out on the wire.
void put_frame_with_LED(unsigned char *buf, int len)
{
  digitalWrite(BUILTIN_LED, 1);
  put_frame(buf, len);
  sss.flush();
  digitalWrite(BUILTIN_LED, 0);
}


// Standard table used in CRC computation
static uint32_t crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};


// Standard CRC computation routine, process a single byte each time.
unsigned long crc_update(unsigned long crc, uint8_t data)
{
    uint8_t tbl_idx;
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
        | (buf[0] & 0xF0)   // catch any stray high bits in the top nybble
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
  buf[7] = (value      ) & 0x0f;
}


// Check the CRC found in the last 8 characters of the buffer.
//
// Returns true if the CRC checks.
//
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


// Given a buffer with extra room reserved at the end for
// a CRC, compute the CRC and write it into the buffer.
//
// Returns the new length of the buffer.
//
int add_packet_crc(unsigned char *buf, int len)
{
  unsigned long crc = ~0L;
  
  for (int i=0; i < len; i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  encode_uint32(buf+len, crc);

  return(len+CHARACTERS_IN_CRC);
}


// Declare a test failure.
// This means just stop and do nothing. Let the user analyze.
void failure(void)
{
  printf("Test failed.\n");
  while (1) {
    tight_loop_contents();
  }
}


//-------------  Test Routines --------------------

// Send a NOP and try to receive a NOP response.
// We will keep trying forever if the UUT does not respond.
// This is suitable for starting up a fresh connection.
void get_a_nop_response(void)
{
  unsigned char nop_cmd[2 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_NOP };
  int len = add_packet_crc(nop_cmd, 2);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  while (1) {
    put_frame_with_LED(nop_cmd, len);

    for (int i=0; i < max_tries; i++) {
      response_len = get_frame_with_expected_data_size(buffer, 0);
      if ((response_len >= (2 + CHARACTERS_IN_CRC))
          && (buffer[0] == DIR_RSP)
          && (buffer[1] == CMD_NOP)
          && check_packet_crc(buffer, response_len)) {
        return;
      }
    }
  }
}


// Send a NOP command with some extra bytes in the payload.
// This is permitted by the spec. The UUT is supposed to ignore them
// and not include them in the response.
void send_nop_with_junk(void)
{
  unsigned char nop_cmd[12 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_NOP, 'a', 0x10, 'b', 0x1b, 'c', 0x1c, 'd', 0x1d, 'e', 0x1e };
  int len = add_packet_crc(nop_cmd, 12);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  put_frame_with_LED(nop_cmd, len);

  for (int i=0; i < max_tries; i++) {
    response_len = get_frame_with_expected_data_size(buffer, 0);
    if ((response_len >= (2 + CHARACTERS_IN_CRC))
        && (buffer[0] == DIR_RSP)
        && (buffer[1] == CMD_NOP)
        && check_packet_crc(buffer, response_len)) {
      return;
    }
  }

  printf("NOP with junk failed\n");
  failure();
}


// Send a NOP command with a bad CRC, to demonstrate CRC checking.
void send_nop_with_bad_crc(void)
{
  unsigned char nop_cmd[12 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_NOP, 'a', 0x10, 'b', 0x1b, 'c', 0x1c, 'd', 0x1d, 'e', 0x1e };
  int len = add_packet_crc(nop_cmd, 12);

  nop_cmd[len-1] ^= 1;   // insert bit error
  put_frame_with_LED(nop_cmd, len);
  // We do not expect a response to a packet with a bad CRC.
  delay(30);     // leave a gap in the timeline for readability
}


// Get identification info from the UUT and print it.
//
// Since we're printing out a message carried in the packet,
// this is only useful for serial word widths of 7 or 8.
//
void obtain_uut_info(void) {
  unsigned char id_cmd[2 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_ID };
  int len = add_packet_crc(id_cmd, 2);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  put_frame_with_LED(id_cmd, len);

  for (int i=0; i < max_tries; i++) {
    response_len = get_frame_with_expected_data_size(buffer, 256);
    if ((response_len >= (2 + CHARACTERS_IN_CRC))
        && (buffer[0] == DIR_RSP)
        && (buffer[1] == CMD_ID)
        && check_packet_crc(buffer, response_len)) {
      buffer[response_len] = 0;   // null terminate response
      printf("UUT Info: %s\n", buffer+2);
      return;
    }
  }

  printf("Obtain UUT Info failed.\n");
  failure();
}


// Send a PARAMS packet and wait for the response.
void set_params(double baud, uint16_t config) {

  unsigned char params_cmd[2 + CHARACTERS_IN_CRC * 3] = { DIR_CMD, CMD_PARAMS };
  uint32_t millibaud = baud * 1000;
  int response_len;
  int max_tries = 3;    // try receiving response several times

  encode_uint32(params_cmd+2, millibaud);
  encode_uint32(params_cmd+2+8, config);
  add_packet_crc(params_cmd, 18);

  put_frame_with_LED(params_cmd, 26);

  for (int i=0; i < max_tries; i++) {
    response_len = get_frame_with_expected_data_size(buffer, 16);
    if ((response_len == 26)
        && (buffer[0] == DIR_RSP)
        && (memcmp(buffer+1, params_cmd+1, 17) == 0)
        && check_packet_crc(buffer, 26)) {
      printf("Set baud=%.03f config=0x%04x\n", baud, config);
      return;
    }
  }

  printf("No response to set params command\n");
  failure();
}


// Complete a change in speed or serial parameters. This includes sending
// the command packet, getting the response, and transitioning the local
// UART to the new settings.
// If any argument is set to 0, that means leave that setting alone.
// The encoding of each config argument is per the SlowSoftSerial specification,
// which implies the non-zero ones can be ORed together to make a config code.
void change_params(double baud, int width, int parity, int stopbits) {

  double new_baud = (baud == STET) ? current_baud : baud;
  int new_width = (width == STET) ? current_width : width;
  int new_parity = (parity == STET) ? current_parity : parity;
  int new_stopbits = (stopbits == STET) ? current_stopbits : stopbits;
  uint16_t new_config = new_width | new_parity | new_stopbits;

  // Send the command and get a response
  set_params(new_baud, new_config);

  current_baud = new_baud;
  current_width = new_width;
  current_parity = new_parity;
  current_stopbits = new_stopbits;
  current_serial_config = new_config;

  // Switch over the local serial port
  sss.end();
  sss.begin(current_baud, current_serial_config);

  // Wait for UUT to execute the change
  delay(1);
}


// Send an ECHO command of the specified length and receive the response.
//
// We don't check the actual echoed data against the sent data, because of
// (feared) memory constraints. Instead, we just check the CRC.
void try_packet_echo(int len) {
  int response_len;
  int max_tries = 3;    // try receiving response several times
  int final_length;

  if (len > MAX_DATA_LEN) {
    printf("ECHO length is too long.\n");
    return;
  }

  // Create an ECHO packet
  buffer[0] = DIR_CMD;
  buffer[1] = CMD_ECHO;
  for (int i=2; i < len+2; i++) {
    buffer[i] = rand() & CURRENT_WIDTH_MASK;
  }
  final_length = add_packet_crc(buffer, len+2);

  put_frame_with_LED(buffer, final_length);

  for (int i=0; i < max_tries; i++) {
    response_len = get_frame_with_expected_data_size(buffer, final_length);
    if ((response_len == final_length)
        && (buffer[0] == DIR_RSP)
        && (buffer[1] == CMD_ECHO)
        && check_packet_crc(buffer, response_len)) {
      return;
    }
  }

  printf("No response to ECHO command\n");
  failure();
}


// Send a BABBLE command of a specified length and receive the response.
//
// We send the BABBLE command just once, but try several times to receive the
// response; this allows for the UUT to send debug packets or other unexpected
// responses without failing the test.
void try_babble(int len) {
  unsigned char babble_cmd[2 + CHARACTERS_IN_CRC + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_BABBLE };
  int response_len;
  int max_tries = 3;    // try receiving response several times
  int sent_length, recv_length;

  if (len > MAX_DATA_LEN) {
    printf("BABBLE length is too long.\n");
    return;
  }

  // Create a BABBLE command packet
  encode_uint32(babble_cmd+2, len);
  sent_length = add_packet_crc(babble_cmd, 10);
  recv_length = sent_length + len;

  put_frame_with_LED(babble_cmd, sent_length);

  for (int i=0; i < max_tries; i++) {
    response_len = get_frame_with_expected_data_size(buffer, recv_length);
    if ((response_len == recv_length)
        && (buffer[0] == DIR_RSP)
        && (memcmp(buffer+1, babble_cmd+1, 9) == 0)
        && check_packet_crc(buffer, response_len)) {
      return;
    }
  }

  printf("No response to BABBLE command\n");
  failure();
}


void cycle_all_params(void)
{
  double baud_rates[] = {9600, 4800, 2400, 1200, 300, 150, 110, 45.45};
  int num_baud_rates = 8;

  int word_widths[] = { SSS_SERIAL_DATA_8,
                        SSS_SERIAL_DATA_7,
                        SSS_SERIAL_DATA_6,
                        SSS_SERIAL_DATA_5,
                      };
  int num_word_widths = 4;

  int parity_modes[] = { SSS_SERIAL_PARITY_NONE,
                         SSS_SERIAL_PARITY_EVEN,
                         SSS_SERIAL_PARITY_ODD,
                         SSS_SERIAL_PARITY_MARK,
                         SSS_SERIAL_PARITY_SPACE,
                       };
  int num_parity_modes = 5;

  int stopbit_modes[] = { SSS_SERIAL_STOP_BIT_1,
                          SSS_SERIAL_STOP_BIT_1_5,
                          SSS_SERIAL_STOP_BIT_2,
                        };
  int num_stopbit_modes =  3;

  for (int baud_i = 0; baud_i < num_baud_rates; baud_i++) {
    for (int width_j = 0; width_j < num_word_widths; width_j++) {
      for (int parity_k = 0; parity_k < num_parity_modes; parity_k++) {
        for (int stopbits_l = 0; stopbits_l < num_stopbit_modes; stopbits_l++) {
          change_params(baud_rates[baud_i],
                        word_widths[width_j],
                        parity_modes[parity_k],
                        stopbit_modes[stopbits_l]);
          try_packet_echo(100);
        }
      }
    }
  }

  // Set params back to nominal
  change_params(9600.0, SSS_SERIAL_DATA_8, SSS_SERIAL_PARITY_NONE, SSS_SERIAL_STOP_BIT_1);
  try_packet_echo(100);

}


// Arduino one-time setup at startup.
void setup() {
  Serial.begin(9600);
  sss.begin(9600, SSS_SERIAL_8N1);

  pinMode(BUILTIN_LED, OUTPUT);   // LED to flash on transmit
  digitalWrite(BUILTIN_LED, 0);

  while (!Serial);

  Serial.println("Slow Soft Serial test controller 0.2");

  // First emit some unformatted stuff for sanity check
  sss.write("Hello UART number one!\r\n");
  sss.flush();

  // Begin test procedures
  get_a_nop_response();   // establish communication with UUT
  puts("UUT NOP heard");

  //set_params(9600.0, 0x0413); // stay at 8N1 for now
  change_params(STET, STET, STET, STET);  // don't really change for now

  send_nop_with_junk();   // emit some stuff to test frame escaping
  send_nop_with_bad_crc();// emit some stuff to test CRC checking

  obtain_uut_info();      // ask the UUT for its identity and display

  for (int i=0; i < 2; i++) {
    change_params(1200, STET, STET, STET);  // try a real baud rate change
    send_nop_with_junk();
    obtain_uut_info();
    change_params(9600, STET, STET, STET);
    send_nop_with_junk();
  }

  try_packet_echo(10);
  printf("ECHO 10 worked\n");
  try_packet_echo(100);
  printf("ECHO 100 worked\n");
  //try_packet_echo(1000);
  //printf("ECHO 1000 worked\n");
  //try_packet_echo(10000);
  //printf("ECHO 10,000 worked\n");

  try_babble(100);
  printf("BABBLE 100 worked\n");
  //try_babble(1000);
  //printf("BABBLE 1000 worked\n");
  //try_babble(10000);
  //printf("BABBLE 10000 worked\n");

  cycle_all_params();

  puts("Test completed.");
}

void loop() {
  tight_loop_contents();
}
