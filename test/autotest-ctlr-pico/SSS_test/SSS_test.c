// Controller Driver for Automated Testing of SlowSoftSerial
// See README.md for discussion and protocol definition.
//
// This program runs as test controller on a Raspberry Pi Pico (first generation),
// connected via serial port to the target platform (UUT for Unit Under Test).
// It is in charge of sequencing the UUT through (some of) its paces.
//
// 

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"
#include "hardware/irq.h"
#include "hardware/regs/intctrl.h"

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define UART_IRQ UART1_IRQ

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

// By convention, we start every test at 9600 baud, 8N1
#define INITIAL_BAUD_RATE 9600
#define INITIAL_WORD_WIDTH 8
#define INITIAL_PARITY UART_PARITY_NONE
#define INITIAL_STOP_BITS 1

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

#define STANDARD_TIMEOUT  5000 // milliseconds

#define	BUFLEN 20010	// big enough for all bytes to be transposed
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
  while (tx_tail == (tx_head+1) & TX_BUF_LEN) {
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
// if the complete frame arrives within the timeout period in milliseconds.
// Returns the number of bytes placed in buf, or 0 if the frame was
// ill-formed or if a complete frame was not received.
int get_frame_with_timeout(unsigned char *buf, uint32_t timeout)
{
  unsigned char	*bufp;
  unsigned char	chr;
  int	count = 0;
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
      puts("Frame timeout");
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
      response_len = get_frame_with_timeout(buffer, STANDARD_TIMEOUT);
      if ((response_len >= (2 + CHARACTERS_IN_CRC))
          && (buffer[0] == DIR_RSP)
          && (buffer[1] == CMD_NOP)
          && check_packet_crc(buffer, response_len)) {
        return;
      }
    }
  }
}

void send_nop_with_junk(void)
{
  unsigned char nop_cmd[12 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_NOP, 'a', 0x10, 'b', 0x1b, 'c', 0x1c, 'd', 0x1d, 'e', 0x1e };
  int len = add_packet_crc(nop_cmd, 12);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  while (1) {
    put_frame_with_LED(nop_cmd, len);

    for (int i=0; i < max_tries; i++) {
      response_len = get_frame_with_timeout(buffer, STANDARD_TIMEOUT);
      if ((response_len >= (2 + CHARACTERS_IN_CRC))
          && (buffer[0] == DIR_RSP)
          && (buffer[1] == CMD_NOP)
          && check_packet_crc(buffer, response_len)) {
        return;
      }
    }
  }
}

void send_nop_with_bad_crc(void)
{
  unsigned char nop_cmd[12 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_NOP, 'a', 0x10, 'b', 0x1b, 'c', 0x1c, 'd', 0x1d, 'e', 0x1e };
  int len = add_packet_crc(nop_cmd, 12);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  nop_cmd[len-1] ^= 1;   // insert bit error

  while (1) {
    put_frame_with_LED(nop_cmd, len);

    for (int i=0; i < max_tries; i++) {
      response_len = get_frame_with_timeout(buffer, STANDARD_TIMEOUT);
      if ((response_len >= (2 + CHARACTERS_IN_CRC))
          && (buffer[0] == DIR_RSP)
          && (buffer[1] == CMD_NOP)
          && check_packet_crc(buffer, response_len)) {
        return;
      }
    }
  }
}


// Get identification info from the UUT and print it.
// We will keep trying forever if the UUT does not respond.
//
// Since we're printing out a message carried in the packet,
// this is only useful for serial word widths of 7 or 8.
//
void obtain_uut_info(void) {
  unsigned char id_cmd[2 + CHARACTERS_IN_CRC] = { DIR_CMD, CMD_ID };
  int len = add_packet_crc(id_cmd, 2);
  int response_len;
  int max_tries = 3;    // try receiving response several times

  while (1) {
    put_frame_with_LED(id_cmd, len);

    for (int i=0; i < max_tries; i++) {
      response_len = get_frame_with_timeout(buffer, STANDARD_TIMEOUT);
      if ((response_len >= (2 + CHARACTERS_IN_CRC))
          && (buffer[0] == DIR_RSP)
          && (buffer[1] == CMD_ID)
          && check_packet_crc(buffer, response_len)) {
        buffer[response_len] = 0;   // null terminate response
        printf("UUT Info: %s\n", buffer+2);
        return;
      }
    }
  }
}


unsigned char test_frame[] = { DIR_CMD, CMD_NOP, 'H', 'e', 'l', 'l', 'o', ' ', 'N', 'O', 'P', 0 };
int test_frame_length = sizeof(test_frame);

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
  sleep_ms(100);

  // Begin test procedures
  get_a_nop_response();   // establish communication with UUT
  puts("UUT NOP heard");


  send_nop_with_junk();   // emit some stuff to test frame escaping
  // send_nop_with_bad_crc();// emit some stuff to test CRC checking

  obtain_uut_info();      // ask the UUT for its identity and display


/*
  // dummy testing
  iteration = 0;
  for (int i=0; i < 5; i++) {
    for (int j=0; j < 1000; j++) {
      get_a_nop_response();
  }
    printf("%ld thousand NOP transactions so far\n", ++iteration);
  }
*/

  puts("Test completed.");

  while (1) {
    tight_loop_contents();
  }

  return 0;
}
