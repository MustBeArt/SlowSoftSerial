#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 9600

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

// GPIO defines
// Example uses GPIO 2
#define GPIO 2


#define	BUFLEN 600		/* big enough for all bytes to be transposed */

		/* Special characters for KISS */

#define	FEND		0300	/* Frame End */
#define	FESC		0333	/* Frame Escape */
#define	TFEND		0334	/* Transposed frame end */
#define	TFESC		0335	/* Transposed frame escape */


/***************************************************************\
*                                                               *
* get_frame                                                     *
*                                                               *
* This function gets one complete KISS frame of data from       *
* the specified input file, and places it in buf.  It returns   *
* the number of bytes placed in buf, or 0 if the frame was      *
* ill-formed or if no data is available.                        *
*                                                               *
* See the ARRL 6th Computer Networking Conference Proceedings,  *
* "The KISS TNC: A simple Host-to-TNC communications protocol"  *
* by Mike Chepponis, K3MC, and Phil Karn, KA9Q, for details of  *
* the KISS framing standard.                                    *
*                                                               *
\***************************************************************/

int get_frame(unsigned char *buf, FILE *infile)
{
unsigned char	*bufp;
int	chr;
int	count = 0;

newframe:

bufp = buf;

while (!ferror(infile) && !feof(infile) && ((chr = getc(infile)) != FEND))
	;				/* eat bytes up to first FEND */

while (!ferror(infile) && !feof(infile) && ((chr = getc(infile)) == FEND))
	;				/* eat as many FENDs as we find */

chr = getc(infile);		/* get past the nul frame-type byte */

while (!ferror(infile) && !feof(infile) && (chr != FEND) && bufp < buf+BUFLEN)
	{
	if (chr == FESC)
		{
		chr = getc(infile);
		if (chr == TFESC)
			*bufp++ = FESC;		/* put escaped character in buffer */
		else if (chr == TFEND)
			*bufp++ = FEND;
		else
			return 0;			/* ill-formed frame */
		}
	else
		*bufp++ = (unsigned char)chr;		/* put character in buffer */

	chr = getc(infile);
	}

if (bufp >= buf+BUFLEN)
	{
	fprintf(stderr, "Warning: KISS frame is too big!  Discarded.\n");
	goto newframe;
	}

return (bufp - buf);			/* return length of buffer */
}



void put_frame(unsigned char *buf, int len)
{
int	i;

uart_putc_raw(UART_ID, FEND);				/*  all frames begin with FEND */

for (i=0; i<len; i++)
	{
	switch (buf[i])				/*  translate for data transparency */
		{
		case FEND:
			uart_putc_raw(UART_ID, FESC);
			uart_putc_raw(UART_ID, TFEND);
			break;
		case FESC:
			uart_putc_raw(UART_ID, FESC);
			uart_putc_raw(UART_ID, TFESC);
			break;
		default:
			uart_putc_raw(UART_ID, buf[i]);
			break;
		}
	}

uart_putc_raw(UART_ID, FEND);				/*  all frames end with FEND */
}

// Thanks https://excamera.com/sphinx/article-crc.html for the CRC routine
static uint32_t crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

unsigned long crc_update(unsigned long crc, uint8_t data)
{
    uint8_t tbl_idx;
    tbl_idx = crc ^ (data >> (0 * 4));
    crc = crc_table[tbl_idx & 0x0f] ^ (crc >> 4);
    tbl_idx = crc ^ (data >> (1 * 4));
    crc = crc_table[tbl_idx & 0x0f] ^ (crc >> 4);
    
    return crc;
}

unsigned long crc_string(char *s)
{
  unsigned long crc = ~0L;
  while (*s)
    crc = crc_update(crc, *s++);
  crc = ~crc;
  return crc;
}

bool check_packet_crc(unsigned char *buf, int len)
{
  unsigned long crc = ~0L;
  unsigned long packet_crc;
  
  for (int i=0; i < (len-4); i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  packet_crc = buf[len-4] << 24 | buf[len-3] << 16 | buf[len-2] << 8 | buf[len-1];
  return crc == packet_crc;
}

int add_packet_crc(unsigned char *buf, int len)
{
  unsigned long crc = ~0L;
  
  for (int i=0; i < len; i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  buf[len] = (crc >> 24) & 0xff;
  buf[len+1] = (crc >> 16) & 0xff;
  buf[len+2] = (crc >> 8) & 0xff;
  buf[len+3] = crc & 0xff;

  return(len+4);
}

unsigned char test_frame[] = "Hello world, this is a dumb test frame.";

int main()
{
    uint actual_baudrate;
    unsigned char buffer[BUFLEN];

    stdio_init_all();

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    

    // GPIO initialisation.
    // We will make this GPIO an input, and pull it up by default
    gpio_init(GPIO);
    gpio_set_dir(GPIO, GPIO_IN);
    gpio_pull_up(GPIO);
    
    puts("Hello, this is the Slow Soft Serial tester");

    uart_puts(UART_ID, "Hello UART number one!\r\n");
    uart_tx_wait_blocking(UART_ID);
    sleep_ms(100);

    for (int i=0; i <= strlen(test_frame); i++) {
        strcpy(buffer, test_frame);

        put_frame(buffer, add_packet_crc(buffer, i));
        puts("Sent dumb test frame with crc");

        sleep_ms(500);
    }

    return 0;
}
