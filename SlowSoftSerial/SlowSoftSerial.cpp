#include "Arduino.h"
#include "SlowSoftSerial.h"

#define _SSS_START_LEVEL (_inverse ? HIGH : LOW)
#define _SSS_STOP_LEVEL  (_inverse ? LOW : HIGH)
#define CTS_ASSERTED     (_inverse ? (digitalRead(_ctsPin) == HIGH) : (digitalRead(_ctsPin) == LOW))

// Operations of receive processing. These go in the op table to schedule
// processing that occurs on receive timer interrupts. See design notes.
#define _SSS_OP_NULL        0
#define _SSS_OP_START       1
#define _SSS_OP_CLEAR       2
#define _SSS_OP_VOTE0       3
#define _SSS_OP_VOTE1       4
#define _SSS_OP_SHIFT       5
#define _SSS_OP_STOP        6
#define _SSS_OP_FINAL       7

// Well, this is ugly. I need to be able to use callback functions for both
// the IntervalTimer and the pin change interrupt. These cannot be member
// functions, unless they are static, and static member functions can't
// access the instance members. Thanks, C++.
// We can tolerate a limitation to only a single serial port, reluctantly,
// so we can save a pointer to the single instance here. That way a static
// member function (the "trampoline") can invoke a non-static member
// callback function.
// The limitation to a single serial port is not too severe, since each
// serial port uses up two precious timers. Teensy processors have either
// two or four timers, so the best we could do would be two serial ports
// anyway. That capability could be added.

SlowSoftSerial *instance_p = NULL;
int SlowSoftSerial::_active_count = 0;    // don't allow more than 1

// Forward.
static void _rx_start_trampoline(void);
static void _rx_timer_trampoline(void);
static void _tx_trampoline(void);

///////////////////////////////////////////////////////////////////////
//  Public Member Functions
///////////////////////////////////////////////////////////////////////

SlowSoftSerial::SlowSoftSerial(uint8_t rxPin, uint8_t txPin, bool inverse) {
    _rxPin = rxPin;
    _txPin = txPin;
    _inverse = inverse;
    _instance_active = false;
}


void SlowSoftSerial::begin(double baudrate, uint16_t config) {
    if (_active_count > 0) {
        return;
    }

    _tx_timer.end();     // just in case begin is called out of sequence

    if (baudrate < _SSS_MIN_BAUDRATE) {
        return;
    }
    _baud_microseconds = 1000000.0/baudrate;
    _rx_microseconds = 250000.0/baudrate;       // 4x the baud rate
    
    switch (config) {
        case SSS_SERIAL_5N1:
            _num_bits_to_send = 6;
            _stop_bits = 0x20;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x10;
            _fill_op_table(5,1);
            break;

        case SSS_SERIAL_6N1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,1);
            break;

        case SSS_SERIAL_7N1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,1);
            break;

        case SSS_SERIAL_8N1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,1);
            break;

        case SSS_SERIAL_5N2:
            _num_bits_to_send = 7;
            _stop_bits = 0x60;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x10;
            _fill_op_table(5,2);
            break;

        case SSS_SERIAL_6N2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,2);
            break;

        case SSS_SERIAL_7N2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,2);
            break;

        case SSS_SERIAL_8N2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,2);
            break;

        case SSS_SERIAL_5E1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,1);
            break;

        case SSS_SERIAL_6E1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,1);
            break;

        case SSS_SERIAL_7E1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,1);
            break;

        case SSS_SERIAL_8E1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,1);
            break;

        case SSS_SERIAL_5E2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,2);
            break;

        case SSS_SERIAL_6E2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,2);
            break;

        case SSS_SERIAL_7E2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,2);
            break;

        case SSS_SERIAL_8E2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,2);
            break;

        case SSS_SERIAL_5O1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,1);
           break;

        case SSS_SERIAL_6O1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,1);
            break;

        case SSS_SERIAL_7O1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,1);
            break;

        case SSS_SERIAL_8O1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,1);
            break;

        case SSS_SERIAL_5O2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,2);
            break;

        case SSS_SERIAL_6O2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,2);
            break;

        case SSS_SERIAL_7O2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,2);
            break;

        case SSS_SERIAL_8O2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_ODD;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,2);
           break;

        case SSS_SERIAL_5M1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,1);
           break;

        case SSS_SERIAL_6M1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,1);
            break;

        case SSS_SERIAL_7M1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,1);
            break;

        case SSS_SERIAL_8M1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,1);
            break;

        case SSS_SERIAL_5M2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,2);
            break;

        case SSS_SERIAL_6M2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,2);
            break;

        case SSS_SERIAL_7M2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,2);
            break;

        case SSS_SERIAL_8M2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_MARK;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,2);
           break;

        case SSS_SERIAL_5S1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,1);
           break;

        case SSS_SERIAL_6S1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,1);
            break;

        case SSS_SERIAL_7S1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,1);
            break;

        case SSS_SERIAL_8S1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,1);
            break;

        case SSS_SERIAL_5S2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x1F;
            _rx_shiftin_bit = 0x20;
            _fill_op_table(6,2);
            break;

        case SSS_SERIAL_6S2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x3F;
            _rx_shiftin_bit = 0x40;
            _fill_op_table(7,2);
            break;

        case SSS_SERIAL_7S2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0x7F;
            _rx_shiftin_bit = 0x80;
            _fill_op_table(8,2);
            break;

        case SSS_SERIAL_8S2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_SPACE;
            _databits_mask = 0xFF;
            _rx_shiftin_bit = 0x100;
            _fill_op_table(9,2);
           break;

        default:
            return;     // failure, unrecognized configuration word
    }

    // Initialize transmit
    // Writing both before and after eliminates a potential glitch.
    digitalWriteFast(_txPin, _SSS_STOP_LEVEL);
    pinMode(_txPin, OUTPUT);
    digitalWriteFast(_txPin, _SSS_STOP_LEVEL);

    _tx_buffer_count = 0;
    _tx_write_index = 0;
    _tx_read_index = 0;
    _tx_bit_count = 0;
    _rts_attached = false;
    _cts_attached = false;
    _tx_enabled = true;
    _tx_running = false;

    // Initialize receive
    pinMode(_rxPin, _inverse ? INPUT_PULLDOWN : INPUT_PULLUP);

    _rx_buffer_count = 0;
    _rx_write_index = 0;
    _rx_read_index = 0;

    // Keep track of our one and only active instance
    _instance_active = true;
    _active_count = 1;
    instance_p = this;

    attachInterrupt(digitalPinToInterrupt(_rxPin), _rx_start_trampoline, _inverse ? RISING : FALLING);
}


void SlowSoftSerial::end(bool releasePins) {
    if (!_instance_active) {
        return;
    }

    _tx_timer.end();    // called first to avoid any conflict for variables
    _rx_timer.end();
    detachInterrupt(digitalPinToInterrupt(_rxPin));

    if (releasePins == SSS_RELEASE_PINS) {
        pinMode(_txPin, INPUT);
        pinMode(_rxPin, INPUT);
        if (_cts_attached) {
            pinMode(_ctsPin, INPUT);
        }
    }

    _tx_buffer_count = 0;
    _tx_enabled = false;
    _tx_running = false;
    _cts_attached = false;

    // this instance is no longer active, so it's OK to activate another one
    instance_p = NULL;
    _active_count = 0;
    _instance_active = false;
}


int SlowSoftSerial::available(void) {
    return _rx_buffer_count;
}


int SlowSoftSerial::availableForWrite(void) {
    return _SSS_TX_BUFFER_SIZE - _tx_buffer_count;
}


int SlowSoftSerial::peek(void) {
    int chr;

    if (_rx_buffer_count > 0) {
        chr = _rx_buffer[_rx_read_index];
        // If we were going to check receive parity, this would be the place to do it.
        // However, the Arduino serial API has no notion of checking for errors.
        return chr & _databits_mask;
    } else {
        return -1;      // nothing available to peek
    }
}


int SlowSoftSerial::read(void) {
    int chr = peek();

    if (chr != -1) {
        if (++_rx_read_index >= _SSS_RX_BUFFER_SIZE) {
            _rx_read_index = 0;
        }

        noInterrupts();
        _rx_buffer_count--;
        interrupts();
    }

    return chr;
}


// NOTE: flush() can take unbounded time to complete
//       if CTS flow control is in use.
void SlowSoftSerial::flush(void) {
    while (_tx_buffer_count > 0 || _tx_running) {
        yield();
    }
}


size_t SlowSoftSerial::write(uint8_t chr) {
    uint16_t data_as_sent;

    // What should we do with characters that don't fit in the word size?
    // Probably the least surprising and confusing thing is to send them
    // anyway, truncated to the word size.
    chr &= _databits_mask;

    data_as_sent = _add_parity(chr) | _stop_bits;
    if (_inverse) {
        data_as_sent ^= 0xFFFF;
    }

    // Arduino Stream semantics require a blocking write()
    while (_tx_buffer_count >= _SSS_TX_BUFFER_SIZE) {   // assumed atomic
        yield();
    }

    // add this character to the transmit buffer
    // This is safe, because tx_write_index is not used by the ISR.
    _tx_buffer[_tx_write_index++] = data_as_sent;
    if (_tx_write_index >= _SSS_TX_BUFFER_SIZE) {
        _tx_write_index = 0;
    }

    noInterrupts();
    _tx_buffer_count++;
    interrupts();

    // Start the baud rate interrupt if it isn't already running
    // Note: we waste a baud before starting to transmit, in order
    // to keep the transmit logic all in one place (the interrupt)
    if (!_tx_running) {
        if (_tx_timer.begin(_tx_trampoline, _baud_microseconds)) {
            _tx_running = true;
        }
    }

    return 1;     // We "sent" the one character
}


void SlowSoftSerial::attachCts(uint8_t pin_number) {
    _ctsPin = pin_number;
    _cts_attached = true;
    pinMode(_ctsPin, _inverse ? INPUT_PULLUP : INPUT_PULLDOWN);
}


///////////////////////////////////////////////////////////////////////
//  Transmit Private Functions
///////////////////////////////////////////////////////////////////////

// Determine the number of 1 bits in the character.
// Return 1 if it's an odd number, 0 if it's an even number.
static bool _parity_is_odd(uint8_t chr) {
    chr = chr ^ (chr >> 4);     // isn't this clever? Not original with me.
    chr = chr ^ (chr >> 2);
    chr = chr ^ (chr >> 1);
    return (chr & 0x01);
}


uint16_t SlowSoftSerial::_add_parity(uint8_t chr) {
    uint16_t data_word = chr;

    switch (_parity) {
        case SSS_SERIAL_PARITY_ODD:
            if (!_parity_is_odd(chr)) {
                data_word |= _parity_bit;
            }
            break;

        case SSS_SERIAL_PARITY_EVEN:
            if (_parity_is_odd(chr)) {
                data_word |= _parity_bit;
            }
            break;

        case SSS_SERIAL_PARITY_MARK:
            data_word |= _parity_bit;
            break;

        case SSS_SERIAL_PARITY_SPACE:
        default:
            break;
    }

    return data_word;
}


// Handle the transmit interrupt
// Interrupt occurs once per baud while we're actively transmitting
// or waiting for handshaking to allow transmitting.
void SlowSoftSerial::_tx_handler(void) {
    uint16_t data_as_sent;

    if (_tx_bit_count > 0) {
        // We're in the middle of sending a character, keep sending it
        digitalWriteFast(_txPin, _tx_data_word & 0x01);
        _tx_data_word >>= 1;
        _tx_bit_count--;
        return;
    }

    if (_tx_buffer_count == 0) {
        // Nothing more to transmit right now, shut it down
        _tx_running = false;
        _tx_timer.end();
        digitalWriteFast(_txPin, _SSS_STOP_LEVEL);  // just to be sure
        return;
    }

    if (!_tx_enabled || (_cts_attached && !CTS_ASSERTED)) {
        // we are not allowed to transmit right now.
        // keep the timer interrupt running, we'll poll.
        return;
    }

    // Get the next character and begin to send it
    data_as_sent = _tx_buffer[_tx_read_index++];
    if (_tx_read_index >= _SSS_TX_BUFFER_SIZE) {
        _tx_read_index = 0;
    }
    _tx_buffer_count--;
    digitalWriteFast(_txPin, _SSS_START_LEVEL);
    _tx_data_word = data_as_sent;
    _tx_bit_count = _num_bits_to_send;
}


void _tx_trampoline(void) {
    instance_p->_tx_handler();
}


///////////////////////////////////////////////////////////////////////
//  Receive Private Functions
///////////////////////////////////////////////////////////////////////

// Create the operations schedule table. See design notes.
// This controls what happens on each RX timer event during a
// single character of reception.
// rxbits includes data bits and parity bits, if any.
void SlowSoftSerial::_fill_op_table(int rxbits, int stopbits) {
    int i = 0;
    int bit;

    _rx_op_table[i++] = _SSS_OP_START;
    _rx_op_table[i++] = _SSS_OP_START;
    _rx_op_table[i++] = _SSS_OP_START;
    _rx_op_table[i++] = _SSS_OP_CLEAR;
    for (bit=0; bit < rxbits; bit++) {
        _rx_op_table[i++] = _SSS_OP_VOTE0;
        _rx_op_table[i++] = _SSS_OP_VOTE1;
        _rx_op_table[i++] = _SSS_OP_VOTE1;
        _rx_op_table[i++] = _SSS_OP_SHIFT;
    }
    _rx_op_table[i++] = _SSS_OP_STOP;
    _rx_op_table[i++] = _SSS_OP_STOP;
    if (stopbits == 2) {
        _rx_op_table[i++] = _SSS_OP_STOP;
        _rx_op_table[i++] = _SSS_OP_STOP;
        _rx_op_table[i++] = _SSS_OP_STOP;
        _rx_op_table[i++] = _SSS_OP_STOP;
    }
    _rx_op_table[i++] = _SSS_OP_FINAL;
}


void SlowSoftSerial::_rx_start_handler(void) {

    if (_rx_timer.begin(_rx_timer_trampoline, _rx_microseconds)) {
        detachInterrupt(digitalPinToInterrupt(_rxPin));
        _rx_op = 0;     // start at the 0th operation in the table
    } else {
        // timer not available, but there isn't much we can do.
        // continue to try every time we see a start bit!
    }
}


static void _rx_start_trampoline(void) {
    instance_p->_rx_start_handler();
}


void SlowSoftSerial::_rx_timer_handler(void) {

    // The worst case execution path through this routine probably determines
    // the impact we have on interrupt latency for other handlers in the system,
    // so we've gone to some length to keep each path short.
    // A switch statement might or might not be slower than a jump table,
    // depending on compiler optimization. Switch statements are easier,
    // and the difference is not likely to be huge.

    switch (_rx_op_table[_rx_op++]) {
        case _SSS_OP_START:
            // We are somewhere in the middle of the start bit.
            // Just make sure it's still a valid start bit.
            if (digitalRead(_rxPin) != (_inverse ? HIGH : LOW)) {
                // stop the timer and go back to waiting for a start bit.
                // must have been noise, or baud rate error, or something.
                _rx_timer.end();
                attachInterrupt(digitalPinToInterrupt(_rxPin), _rx_start_trampoline, _inverse ? RISING : FALLING);
            }
            break;

        case _SSS_OP_CLEAR:
            // We have reached the end of the stop bit. So far, so good.
            // This interrupt is on top of a possible data transition, so we
            // can't meaningfully sample the RX pin. We can just get set up
            // for receiving the data bits.
            _rx_data_word = 0;
            break;
            
        case _SSS_OP_VOTE0:
            // We're ready to take the first sample of a data or parity bit.
            _rx_bit_value = digitalRead(_rxPin);
            break;
            
        case _SSS_OP_VOTE1:
            // We're still in the middle of a data or parity bit.
            // Just make sure it hasn't changed on us.
            if (digitalRead(_rxPin) != _rx_bit_value) {
                // stop the timer and go back to waiting for a start bit.
                // must have been noise, or baud rate error, or something.
                _rx_timer.end();
                attachInterrupt(digitalPinToInterrupt(_rxPin), _rx_start_trampoline, _inverse ? RISING : FALLING);
            }
            break;
            
        case _SSS_OP_SHIFT:
            // We have reached the end of a data or parity bit.
            // This interrupt is on top of a possible data transition, so we
            // can't meaningfully sample the RX pin. We can just shift the
            // new bit in. The LS bit arrives first, so we have to shift right
            // to get the bits in the right order. 
            _rx_data_word >>= 1;
            if (_rx_bit_value) {
                _rx_data_word |= _rx_shiftin_bit;
            }
            break;
            
        case _SSS_OP_STOP:
            // We are somewhere in the middle of the stop bit.
            // Just make sure it's a valid stop bit.
            if (digitalRead(_rxPin) != (_inverse ? LOW : HIGH)) {
                // stop the timer and go back to waiting for a start bit.
                // must have been noise, or baud rate error, or something.
                _rx_timer.end();
                attachInterrupt(digitalPinToInterrupt(_rxPin), _rx_start_trampoline, _inverse ? RISING : FALLING);
            }
            break;
            
        case _SSS_OP_FINAL:
            // We have reached the last sample point near the end of the stop bit.
            // This will be our last timer event for this character, because the earliest
            // possible start bit for the next character comes at the same instant as the
            // next timer event would.
            // We'll check one last time that the stop bit is valid, and then we'll
            // wrap up processing for this received character. Either way, we'll set up
            // for receiving the next character.
            if (digitalRead(_rxPin) == (_inverse ? LOW : HIGH)) {
                // stop bit passed the last check, no timing errors on this character!
                // We store the data and parity bits. If there's to be any parity checking,
                // it must occur as the characters are read out of the buffer (and not in
                // interrupt context).
                if (_rx_buffer_count < _SSS_RX_BUFFER_SIZE) {
                    _rx_buffer[_rx_write_index++] = _rx_data_word;
                    if (_rx_write_index >= _SSS_RX_BUFFER_SIZE) {
                        _rx_write_index = 0;
                    }
                    
                    _rx_buffer_count++;
                }
            }
            // stop the timer and go back to waiting for a start bit.
            _rx_timer.end();
            attachInterrupt(digitalPinToInterrupt(_rxPin), _rx_start_trampoline, _inverse ? RISING : FALLING);
            break;
            
        case _SSS_OP_NULL:
        default:
            break;
    }

}


void _rx_timer_trampoline(void) {
    instance_p->_rx_timer_handler();
}
