#pragma once

#include <inttypes.h>
#include "Stream.h"
#include "IntervalTimer.h"

#define _SSS_TX_BUFFER_SIZE 64
#define _SSS_RX_BUFFER_SIZE 64

#define _SSS_MIN_BAUDRATE 1.0       // arbitrary; don't divide by zero.


// Definitions for databits, parity, and stopbits configuration word.
// These are taken from the official Arduino API, but I've had to rename
// them because other serial libraries are not consistent with these
// official values.
#define SSS_SERIAL_PARITY_EVEN   (0x1ul)
#define SSS_SERIAL_PARITY_ODD    (0x2ul)
#define SSS_SERIAL_PARITY_NONE   (0x3ul)
#define SSS_SERIAL_PARITY_MARK   (0x4ul)
#define SSS_SERIAL_PARITY_SPACE  (0x5ul)
#define SSS_SERIAL_PARITY_MASK   (0xFul)

#define SSS_SERIAL_STOP_BIT_1    (0x10ul)
#define SSS_SERIAL_STOP_BIT_1_5  (0x20ul)
#define SSS_SERIAL_STOP_BIT_2    (0x30ul)
#define SSS_SERIAL_STOP_BIT_MASK (0xF0ul)

#define SSS_SERIAL_DATA_5        (0x100ul)
#define SSS_SERIAL_DATA_6        (0x200ul)
#define SSS_SERIAL_DATA_7        (0x300ul)
#define SSS_SERIAL_DATA_8        (0x400ul)
#define SSS_SERIAL_DATA_MASK     (0xF00ul)

#define SSS_SERIAL_5N1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6N1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7N1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8N1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5N2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6N2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7N2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8N2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_NONE  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5E1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6E1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7E1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8E1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5E2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6E2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7E2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8E2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_EVEN  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5O1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6O1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7O1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8O1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5O2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6O2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7O2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8O2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_ODD   | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5M1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6M1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7M1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8M1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5M2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6M2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7M2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8M2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_MARK  | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5S1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6S1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7S1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8S1           (SSS_SERIAL_STOP_BIT_1 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_8)
#define SSS_SERIAL_5S2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_5)
#define SSS_SERIAL_6S2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_6)
#define SSS_SERIAL_7S2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_7)
#define SSS_SERIAL_8S2           (SSS_SERIAL_STOP_BIT_2 | SSS_SERIAL_PARITY_SPACE | SSS_SERIAL_DATA_8)

class SlowSoftSerial : public Stream
{
  public:
    SlowSoftSerial(uint8_t rxPin, uint8_t txPin, bool inverse = false);
    ~SlowSoftSerial() { end(); }

    // Note we use a floating point value for baud rate. We can do that!
    // And if legacy code passes an integral value, it just gets converted.
    void begin(double baudrate, uint16_t config);
    void begin(double baudrate) { begin(baudrate, SSS_SERIAL_8N1); }

    void end();
    int available(void);
    int availableForWrite(void);
    int peek(void);
    int read(void);
    void flush(void);
    size_t write(uint8_t);
    using Print::write; // pull in write(str) and write(buf, size) from Print

    operator bool() { return true; };   // we are always ready

    // listen is a SoftwareSerial thing, since it can only receive on one port.
    // We don't have that limitation, though we are limited by the number of timers.
    bool listen() { return false; }
    bool isListening() { return true; }

    // Unfortunately, this has to be public because of the horrific workaround
    // needed to register a callback with IntervalTimer.
    void _tx_handler(void);

  private:
    uint16_t _add_parity(uint8_t chr);
    void _be_transmitting(void);
    void _tx_isr(void);

    // port configuration
    double _baud_microseconds;     // one baud in microseconds
    uint16_t _parity;               // use definitions above, like ones in HardwareSerial.h
    uint8_t _num_bits_to_send;      // includes parity and stop bit(s) but not start bit
    uint16_t _parity_bit;           // bitmask for the parity bit; 0 if no parity
    int16_t _stop_bits;             // bits to OR in to data word
    uint8_t _forbidden_bits;        // bitmask of bits that don't fit in the word size
    uint8_t _rxPin;
    uint8_t _txPin;
    bool _inverse;
    IntervalTimer _tx_timer;
    IntervalTimer _rx_timer;    

    // transmit buffer and its variables
    volatile int _tx_buffer_count;
    int _tx_write_index;
    int _tx_read_index;
    int _tx_buffer[_SSS_TX_BUFFER_SIZE];

    // transmit state
    int _tx_data_word;
    int _tx_bit_count;
    bool _tx_enabled = true;
    bool _tx_running = false;

    // receive buffer and its variables

    // receive state


};
