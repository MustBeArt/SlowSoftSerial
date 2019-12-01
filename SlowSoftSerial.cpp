#include "Arduino.h"
#include "SlowSoftSerial.h"
#include "HardwareSerial.h"

#define _SSS_START_LEVEL (_inverse ? HIGH : LOW)
#define _SSS_STOP_LEVEL  (_inverse ? LOW : HIGH)

// Well, this is ugly. I need to be able to use the IntervalTimer class,
// which needs a callback function. I can't directly use a member function,
// unless it's static. But static member functions can't access the instance
// members. Thanks, Bjarne. So, since we can tolerate having only a single
// instance of this class (ugh! yuck!), we will save a pointer to it here.
// That way a static member function can invoke a non-static member function
// to do the work when the IntervalTimer fires.
SlowSoftSerial *instance_p = NULL;

SlowSoftSerial::SlowSoftSerial(uint8_t rxPin, uint8_t txPin, bool inverse) {
    _rxPin = rxPin;
    _txPin = txPin;
    _inverse = inverse;
    instance_p = this;  // if user goes rogue and makes two, bad things will happen.
}


void SlowSoftSerial::begin(double baudrate, uint16_t config) {
    _tx_timer.end();     // just in case begin is called out of sequence

    if (baudrate < _SSS_MIN_BAUDRATE) {
        return;
    }
    _baud_microseconds = 1000000.0/baudrate;
    
    switch (config) {
        case SSS_SERIAL_5N1:
            _num_bits_to_send = 6;
            _stop_bits = 0x20;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0xE0;
            break;

        case SSS_SERIAL_6N1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7N1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8N1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0x00;
            break;

        case SSS_SERIAL_5N2:
            _num_bits_to_send = 7;
            _stop_bits = 0x60;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0xE0;
            break;

        case SSS_SERIAL_6N2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7N2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8N2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0;
            _parity = SSS_SERIAL_PARITY_NONE;
            _forbidden_bits = 0x00;
            break;

        case SSS_SERIAL_5E1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0xE0;
            break;

        case SSS_SERIAL_6E1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7E1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8E1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0x00;
            break;

        case SSS_SERIAL_5E2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0xE0;
            break;

        case SSS_SERIAL_6E2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7E2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8E2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_EVEN;
            _forbidden_bits = 0x00;
            break;

        case SSS_SERIAL_5O1:
            _num_bits_to_send = 7;
            _stop_bits = 0x40;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_ODD;
           _forbidden_bits = 0xE0;
           break;

        case SSS_SERIAL_6O1:
            _num_bits_to_send = 8;
            _stop_bits = 0x80;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7O1:
            _num_bits_to_send = 9;
            _stop_bits = 0x100;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8O1:
            _num_bits_to_send = 10;
            _stop_bits = 0x200;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0x00;
            break;

        case SSS_SERIAL_5O2:
            _num_bits_to_send = 8;
            _stop_bits = 0xC0;
            _parity_bit = 0x20;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0xE0;
            break;

        case SSS_SERIAL_6O2:
            _num_bits_to_send = 9;
            _stop_bits = 0x180;
            _parity_bit = 0x40;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0xC0;
            break;

        case SSS_SERIAL_7O2:
            _num_bits_to_send = 10;
            _stop_bits = 0x300;
            _parity_bit = 0x80;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0x80;
            break;

        case SSS_SERIAL_8O2:
            _num_bits_to_send = 11;
            _stop_bits = 0x600;
            _parity_bit = 0x100;
            _parity = SSS_SERIAL_PARITY_ODD;
            _forbidden_bits = 0x00;
           break;

        default:
            return;     // failure, unrecognized configuration word
    }

    digitalWrite(_txPin, _SSS_STOP_LEVEL);
    pinMode(_txPin, OUTPUT);
    pinMode(_rxPin, _inverse ? INPUT_PULLDOWN : INPUT_PULLUP);

    _tx_buffer_count = 0;
    _tx_write_index = 0;
    _tx_read_index = 0;
    _tx_enabled = true;
}


void SlowSoftSerial::end() {
    _tx_timer.end();   // called first to avoid any conflict for variables
    _rx_timer.end();
    pinMode(_txPin, INPUT);
    pinMode(_rxPin, INPUT);

    _tx_buffer_count = 0;
    _tx_enabled = false;
}


int SlowSoftSerial::available(void) {
    return 0;       //!!! receive not implemented yet
}


int SlowSoftSerial::availableForWrite(void) {
    return _SSS_TX_BUFFER_SIZE - _tx_buffer_count;
}


int SlowSoftSerial::peek(void) {
    return -1;      //!!! receive not implemented yet
}


int SlowSoftSerial::read(void) {
    return -1;      //!!! receive not implemented yet
}


void SlowSoftSerial::flush(void) {
    while (_tx_buffer_count > 0) {
        yield();
    }
}


// Determine the number of 1 bits in the character.
// Return 1 if it's an odd number, 0 if it's an even number.
static bool parity_is_odd(uint8_t chr) {
    chr = chr ^ (chr >> 4);     // isn't this clever?
    chr = chr ^ (chr >> 2);
    chr = chr ^ (chr >> 1);
    return (chr & 0x01);
}


uint16_t SlowSoftSerial::_add_parity(uint8_t chr) {
    uint16_t data_word = chr;

    switch (_parity) {
        case SSS_SERIAL_PARITY_ODD:
            if (!parity_is_odd(chr)) {
                data_word |= _parity_bit;
            }
            break;

        case SSS_SERIAL_PARITY_EVEN:
            if (parity_is_odd(chr)) {
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



void SlowSoftSerial::_tx_handler(void) {
    uint16_t data_as_sent;

    if (_tx_bit_count > 0) {
        digitalWrite(_txPin, _tx_data_word & 0x01);
        _tx_data_word >>= 1;
        _tx_bit_count--;
    }

    else if (_tx_enabled && _tx_buffer_count > 0) {
        data_as_sent = _tx_buffer[_tx_read_index++];
        if (_tx_read_index >= _SSS_TX_BUFFER_SIZE) {
            _tx_read_index = 0;
        }
        _tx_buffer_count--;
        digitalWrite(_txPin, _SSS_START_LEVEL);
        _tx_data_word = data_as_sent;
        _tx_bit_count = _num_bits_to_send;
    }

    else {
        _tx_running = false;
        _tx_timer.end();
    }
}


void _tx_trampoline(void) {
    instance_p->_tx_handler();
}


// runs only in foreground
void SlowSoftSerial::_be_transmitting(void) {
    uint16_t data_as_sent;

    if (_tx_enabled && !_tx_running && _tx_buffer_count > 0) {
        // Safe here, because the ISR is not running
        data_as_sent = _tx_buffer[_tx_read_index++];
        if (_tx_read_index >= _SSS_TX_BUFFER_SIZE) {
            _tx_read_index = 0;
        }
        _tx_buffer_count--;

        _tx_timer.begin(_tx_trampoline, _baud_microseconds); //whoa
        _tx_running = true;

        digitalWrite(_txPin, _SSS_START_LEVEL);
        _tx_data_word = data_as_sent;
        _tx_bit_count = _num_bits_to_send;

    }
}


size_t SlowSoftSerial::write(uint8_t chr) {
    uint16_t data_as_sent;

    // Reject any character too big for the current word size
    if ((chr & _forbidden_bits) != 0) {
        return 0;
    }

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

    _be_transmitting();

    return 1;     // We "sent" the one character
}

