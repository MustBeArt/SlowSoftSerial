// Controller Driver for Automated Testing of SlowSoftSerial
// See README.md for discussion and protocol definition.
//
// This program runs as test controller on a Raspberry Pi Pico (first generation),
// connected via serial port to the target platform (UUT for Unit Under Test).
// It is in charge of sequencing the UUT through (some of) its paces.
//
// 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"
#include "hardware/irq.h"
#include "hardware/regs/intctrl.h"
#include "SlowSoftSerial.h"

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define UART_IRQ UART1_IRQ

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

// By convention, we start every test at 9600 baud, 8N1
// These definitions are for the local UART
#define INITIAL_BAUD_RATE 9600
#define INITIAL_WORD_WIDTH 8
#define INITIAL_PARITY UART_PARITY_NONE
#define INITIAL_STOP_BITS 1
// and these definitions are for the UUT
double current_baud = 9600.0;
int current_width = SSS_SERIAL_DATA_8;
int current_parity = SSS_SERIAL_PARITY_NONE;
int current_stopbits = SSS_SERIAL_STOP_BIT_1;

unsigned char width_masks[] = { 0x00, 0x1F, 0x3F, 0x7F, 0xFF };
#define CURRENT_WIDTH_MASK (width_masks[current_width >> 8])  // convert current_width to a mask
#define CURRENT_WIDTH_BITS ((current_width >> 8) + 4)         // convert current_width to # of bits

// In serial configuration changes, 0 means leave that parameter alone
#define STET 0

// Pin for on-board LED
#define LED_PIN PICO_DEFAULT_LED_PIN

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

int word_width = INITIAL_WORD_WIDTH;
#define WORD_WIDTH_MASK   (0xFF >> (8-word_width))

// Interrupt-driver UART buffers for receive and transmit
#define RX_BUF_LEN  64    // arbitrary
unsigned char rx_buf[RX_BUF_LEN];
volatile int rx_head, rx_tail;

#define TX_BUF_LEN  64    // arbitrary
unsigned char tx_buf[TX_BUF_LEN];
volatile int tx_head, tx_tail;


// I believe the SDK's handling of UART receive interrupts is wrong for the FIFO enabled
// case. They enable the receive interrupt (which fires when the FIFO reaches a level)
// but they don't enable the timeout interrupt (which fires when the FIFO is non-empty
// but below the trigger level for some duration of time). Thus, the last few characters
// do not get received. This is a patched version of their interrupt enabler that takes
// care of that, or so I hope.
/*! \brief Setup UART interrupts
 *  \ingroup hardware_uart
 *
 * Enable the UART's interrupt output. An interrupt handler will need to be installed prior to calling
 * this function.
 *
 * \param uart UART instance. \ref uart0 or \ref uart1
 * \param rx_has_data If true an interrupt will be fired when the RX FIFO contain data.
 * \param tx_needs_data If true an interrupt will be fired when the TX FIFO needs data.
 */
static inline void my_uart_set_irq_enables(uart_inst_t *uart, bool rx_has_data, bool tx_needs_data) {
    uart_get_hw(uart)->imsc = (!!tx_needs_data << UART_UARTIMSC_TXIM_LSB) |
                              (!!rx_has_data << UART_UARTIMSC_RXIM_LSB) |
                              (!!rx_has_data << UART_UARTIMSC_RTIM_LSB);  // PTW there it is
    if (rx_has_data) {
        // Set minimum threshold
        hw_write_masked(&uart_get_hw(uart)->ifls, 0 << UART_UARTIFLS_RXIFLSEL_LSB,
                        UART_UARTIFLS_RXIFLSEL_BITS);
    }
    if (tx_needs_data) {
        // Set maximum threshold
        hw_write_masked(&uart_get_hw(uart)->ifls, 0 << UART_UARTIFLS_TXIFLSEL_LSB,
                        UART_UARTIFLS_TXIFLSEL_BITS);
    }
}


// UART Receive and Transmit Interrupt Handler
// This handler just adds some buffering to allow for interrupt latency.
// It doesn't do any processing.
//
// This interrupt handler is written as if multiple characters could come in
// per interrupt, which could happen if we had UART FIFOs enabled. However, if
// we do that, we don't seem to get a final interrupt at the end of an incoming
// frame. Rather than try to get clever about running the FIFO dry, we'll just
// hope that taking an interrupt for every character is fast enough for our
// purposes.
//
void on_uart_irq(void)
{
  while (uart_is_readable(UART_ID)) {
    rx_buf[rx_head] = uart_getc(UART_ID);
    rx_head = (rx_head + 1) % RX_BUF_LEN;
    if (rx_head == rx_tail) {               // check for overflow
      rx_tail = (rx_tail + 1) % RX_BUF_LEN; // discard oldest char (is this best?)
    }
  }

  while (uart_is_writable(UART_ID)) {
    if (tx_head != tx_tail) {
      uart_putc_raw(UART_ID, tx_buf[tx_tail]);
      tx_tail = (tx_tail+1) % TX_BUF_LEN;
    } else {                                        // transmit buffer is empty now
      my_uart_set_irq_enables(UART_ID, true, false);   // disable TX interrupts
      break;
    }
  }
}


// Blocking UART transmit through the interrupt-driven buffer
void serial_putc(unsigned char c) {
  // wait for there to be some room in the buffer
  while (tx_tail == (tx_head+1) % TX_BUF_LEN) {
    tight_loop_contents();
  }

  tx_buf[tx_head] = c;
  tx_head = (tx_head+1) % TX_BUF_LEN;

  my_uart_set_irq_enables(UART_ID, true, true);  // make sure TX interrupts are enabled
}


// Get one character from the interrupt-driven receive buffer,
// if one is available before the specified absolute timeout.
//
// Returns true if a characters was received, false if not.
// The character is placed in *chr_p
//
bool serial_getc_timeout(absolute_time_t tmax, unsigned char *chr_p) {
  while (rx_head == rx_tail) {
    if (absolute_time_diff_us(get_absolute_time(), tmax) < 0) {
      return false;     // timeout before a character was available
    } else {
      tight_loop_contents();
    }
  }

  *chr_p = rx_buf[rx_tail];     // get a character
  rx_tail = (rx_tail+1) % RX_BUF_LEN;
  return true;
}


// Get one complete frame of data from the serial port, and place it in buf,
// if the complete frame arrives within a reasonable time based on the current
// communications parameters and the expected response size. Expected size
// includes only the data characters and not the header, framing, stuffing,
// or CRC.
//
// Returns the number of bytes placed in buf, or 0 if the frame was
// ill-formed or if a complete frame was not received.
int get_frame_with_expected_data_size(unsigned char *buf, int expected_size_in_characters)\
{
  unsigned char	*bufp;
  unsigned char	chr;
  int	count = 0;
  uint32_t timeout = 10 + ((long)expected_size_in_characters*2 + 10) * (CURRENT_WIDTH_BITS + 4) * 1000 / current_baud;
  absolute_time_t timeout_time = make_timeout_time_ms(timeout);

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
// Blocks in serial_putc() if the frame is larger than the space
// available in the transmit buffer.
void put_frame(unsigned char *buf, int len)
{
  int	i;

  serial_putc(FEND);				// all frames begin with FEND

  for (i=0; i<len; i++)
    {
    switch (buf[i])				//  translate for data transparency
      {
      case FEND:
        serial_putc(FESC);
        serial_putc(TFEND);
        break;
      case FESC:
        serial_putc(FESC);
        serial_putc(TFESC);
        break;
      default:
        serial_putc(buf[i]);
        break;
      }
    }

  serial_putc(FEND);				//  all frames end with FEND
}


// Wrapper for put_frame that lights up the on-board LED
// while transmission is in progress. Note that this means
// the function doesn't return until all the characters
// have gone out on the wire.
void put_frame_with_LED(unsigned char *buf, int len)
{
  gpio_put(LED_PIN, 1);
  put_frame(buf, len);
  uart_tx_wait_blocking(UART_ID);
  gpio_put(LED_PIN, 0);
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
  int response_len;
  int max_tries = 3;    // try receiving response several times

  nop_cmd[len-1] ^= 1;   // insert bit error
  put_frame_with_LED(nop_cmd, len);
  // We do not expect a response to a packet with a bad CRC.
  sleep_ms(30);     // leave a gap in the timeline for readability
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
      printf("Set baud=%.03f config=0x%04x\n", floor(baud), config);
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

  // Send the command and get a response
  set_params(new_baud, new_width | new_parity | new_stopbits);

  current_baud = new_baud;
  current_width = new_width;
  current_parity = new_parity;
  current_stopbits = new_stopbits;

  // translate parameters for the local UART
  uint uart_baud = (uint)(current_baud + 0.5);    // Note we're mangling any fractional part
  uint uart_data_bits = (current_width >> 8) + 4;         // valid for 5 through 8
  uint uart_stop_bits = (current_stopbits == SSS_SERIAL_STOP_BIT_1) ? 1 : 2; // no support for 1.5
  uart_parity_t uart_parity = UART_PARITY_NONE;
  switch (current_parity) {
    case SSS_SERIAL_PARITY_NONE: uart_parity = UART_PARITY_NONE; break;
    case SSS_SERIAL_PARITY_EVEN: uart_parity = UART_PARITY_EVEN; break;
    case SSS_SERIAL_PARITY_ODD: uart_parity = UART_PARITY_ODD; break;
    default: break;     // No support for MARK or SPACE parity
  }

  // Switch over the local UART
  uart_set_baudrate(UART_ID, (uint)current_baud);
  uart_set_format(UART_ID, uart_data_bits, uart_stop_bits, uart_parity);

  // Wait for UUT to execute the change
  sleep_ms(1);
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


int main()
{
  uint actual_baudrate;
  unsigned long iteration;

  stdio_init_all();

  // Set the TX and RX pins by using the function select on the GPIO
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  
  // Set up our UART
  uart_init(UART_ID, INITIAL_BAUD_RATE);
  uart_set_hw_flow(UART_ID, false, false);
  uart_set_format(UART_ID, INITIAL_WORD_WIDTH, INITIAL_STOP_BITS, INITIAL_PARITY);
  uart_set_fifo_enabled(UART_ID, true);   // it'd be nice if this had documentation
  irq_set_exclusive_handler(UART_IRQ, on_uart_irq);
  irq_set_enabled(UART_IRQ, true);
  my_uart_set_irq_enables(UART_ID, true, false); // IRQ for receive
            // we will enable the transmit IRQ when we've buffered something to transmit
  rx_head = rx_tail = 0;
  tx_head = tx_tail = 0;

  // Set up LED to blink when transmitting
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);

  sleep_ms(3000);     // Wait for serial terminal to be ready
  puts("Hello, this is the Slow Soft Serial test controller");

  // Transmitting some stuff here seems to unstick the UART.
  // !!! Figure out why and do something more elegant.
  uart_puts(UART_ID, "Hello UART number one!\r\n");
  uart_tx_wait_blocking(UART_ID);

  // Begin test procedures
  get_a_nop_response();   // establish communication with UUT
  puts("UUT NOP heard");

  //set_params(9600.0, 0x0413); // stay at 8N1 for now
  change_params(STET, STET, STET, STET);  // don't really change for now

  send_nop_with_junk();   // emit some stuff to test frame escaping
  send_nop_with_bad_crc();// emit some stuff to test CRC checking

  obtain_uut_info();      // ask the UUT for its identity and display

  change_params(1200, STET, STET, STET);  // try a real baud rate change
  send_nop_with_junk();
  obtain_uut_info();
  change_params(9600, STET, STET, STET);
  send_nop_with_junk();

  try_packet_echo(10);
  try_packet_echo(10);
  try_packet_echo(10);
  printf("ECHO 10 worked\n");
  try_packet_echo(100);
  printf("ECHO 100 worked\n");
  try_packet_echo(1000);
  printf("ECHO 1000 worked\n");
  try_packet_echo(10000);
  printf("ECHO 10,000 worked\n");

  try_babble(100);
  printf("BABBLE 100 worked\n");
  try_babble(1000);
  printf("BABBLE 1000 worked\n");
  try_babble(10000);
  printf("BABBLE 10000 worked\n");

  puts("Test completed.");

  while (1) {
    tight_loop_contents();
  }

  return 0;
}
