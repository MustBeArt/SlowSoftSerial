#include "SlowSoftSerial.h"

SlowSoftSerial sss(0,1);

    /* Special characters for KISS */

#define FEND    0x10  /* Frame End */
#define FESC    0x1B  /* Frame Escape */
#define TFEND   0x1C  /* Transposed frame end */
#define TFESC   0x1D  /* Transposed frame escape */

#define CHARACTERS_IN_CRC 8 // 32-bit CRC, sent 4 bits per character

#define PACKET_BUF_SIZE 1000
unsigned char packet_buf[PACKET_BUF_SIZE];

int blocking_read(void)
{
  while (! sss.available());
  return sss.read();
}

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

int get_frame(unsigned char *buf, int max_length)
{
unsigned char *bufp;
int chr;

bufp = buf;

// Eat bytes up to the first FEND
while (blocking_read() != FEND);

// Eat as many FENDs as we find
while ((chr = blocking_read()) == FEND);

while ((chr != FEND) && (bufp < buf+max_length)) {
  if (chr == FESC) {
    chr = blocking_read();
    if (chr == TFESC) {
      *bufp++ = FESC;       // put escaped character in buffer
    } else if (chr == TFEND) {
      (*bufp++ = FEND);
    } else {
      return 0;             // ill-formed frame
    }
  } else {
    *bufp++ = (unsigned char)chr;
  }

  chr = blocking_read();
  }

if (bufp >= buf+max_length)
  {
  return 0;               // frame too big for buffer
  }

return (bufp - buf);      // return length of buffer
}

static uint32_t crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

unsigned long crc_update(unsigned long crc, byte data)
{
    byte tbl_idx;
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
  
  for (int i=0; i < (len-CHARACTERS_IN_CRC); i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  packet_crc = buf[len-CHARACTERS_IN_CRC] << 28
             | buf[len-CHARACTERS_IN_CRC+1] << 24
             | buf[len-CHARACTERS_IN_CRC+2] << 20
             | buf[len-CHARACTERS_IN_CRC+3] << 16
             | buf[len-CHARACTERS_IN_CRC+4] << 12
             | buf[len-CHARACTERS_IN_CRC+5] << 8
             | buf[len-CHARACTERS_IN_CRC+6] << 4
             | buf[len-CHARACTERS_IN_CRC+7];

  return crc == packet_crc;
}


int add_packet_crc(unsigned char *buf, int len)
{
  unsigned long crc = ~0L;
  
  for (int i=0; i < len; i++) {
    crc = crc_update(crc, buf[i]);
  }
  crc = ~crc;
  buf[len]   = (crc >> 28) & 0x0f;
  buf[len+1] = (crc >> 24) & 0x0f;
  buf[len+2] = (crc >> 20) & 0x0f;
  buf[len+3] = (crc >> 16) & 0x0f;
  buf[len+4] = (crc >> 12) & 0x0f;
  buf[len+5] = (crc >>  8) & 0x0f;
  buf[len+6] = (crc >>  4) & 0x0f;
  buf[len+7] = crc & 0x0f;

  return(len+CHARACTERS_IN_CRC);
}


void dump_packet_buf(int len, bool crc_good) {
  static long msg_count = 0;
  char fbuf[20];    // local formatting buffer
  
  Serial.print("Frame: ");
  Serial.print(msg_count++);

  if (len >= 2+CHARACTERS_IN_CRC) {
    switch(packet_buf[0]) {
      case 0:   Serial.print("  Cmd");
                break;
      case 1:   Serial.print("  Rsp");
                break;
      case 2:   Serial.print("  Dbg");
                break;
      default:  sprintf(fbuf, " 0x%2X", (int)packet_buf[0]);
                Serial.print(fbuf);
                break;
    }

    switch(packet_buf[1]) {
      case 0:   Serial.print("    NOP");
                break;
      case 1:   Serial.print("     ID");
                break;
      case 2:   Serial.print("   ECHO");
                break;
      case 3:   Serial.print(" BABBLE");
                break;
      case 4:   Serial.print(" PARAMS");
                break;
      default:  sprintf(fbuf, "   0x%2X", (int)packet_buf[1]);
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


void setup() {
  Serial.begin(9600);
  sss.begin(9600, SSS_SERIAL_8N1);

  while (!Serial);

  Serial.println("SlowSoftSerial Tester 0.01");
  Serial.println("Waiting for frames ...");


}

void loop() {
  int len;
  bool crc_good;
  
  len = get_frame(packet_buf, PACKET_BUF_SIZE);

  if (len > 8) {
    crc_good = check_packet_crc(packet_buf, len);
  } else {
    crc_good = 0;
  }

  dump_packet_buf(len, crc_good);
}
