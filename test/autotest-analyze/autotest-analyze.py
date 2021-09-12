import array
import string
import struct
import sys
import typing
import zlib
from collections import namedtuple
from typing import List, Optional, Tuple, Union

DUMP_CHARACTERS: int = 0

# tolerance in bauds for edge placement and glitch bursts
epsilon: float = 1.0 / 16.0

# default serial parameters; all traces should start with these
baudrate: float = 9600.0
onebaud: float = 1.0 / baudrate
num_data_bits: int = 8  # could be 5, 6, 7, 8
num_data_and_parity_bits: int = 8  # could be 5, 6, 7, 8, or 9
num_stop_bits: float = 1.0  # could be 1, 1.5, or 2
parity_type: str = "N"  # could be N, O, E, M, S

# Special characters for framing packets in SlowSoftSerial test protocol
FEND: int = 0x10  # Frame End (and beginning)
FESC: int = 0x1B  # Frame Escape
TFEND: int = 0x1C  # Transposed Frame End
TFESC: int = 0x1D  # Transposed Frame Escape

# initial conditions for interpreting packets
in_packet: bool = False
in_escape_seq: bool = False
packet: bytearray = bytearray()
packet_start_time: float = 0

# PARAM packet decoding
# 4-bit fields need 16 entries for safe lookup
width_decode: List[int] = [0, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
parity_decode: List[str] = ["X", "E", "O", "N", "M", "S", "X", "X", "X", "X", "X", "X", "X", "X", "X", "X"]
stop_decode: List[float] = [0, 1, 1.5, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

# For parsing Saleae Logic 2.0 binary-format capture files.
# see https://support.saleae.com/faq/technical-faq/binary-export-format-logic-2
TYPE_DIGITAL = 0
TYPE_ANALOG = 1
expected_version = 0

DigitalData = namedtuple(
    "DigitalData",
    ("initial_state", "begin_time", "end_time", "num_transitions", "transition_times"),
)


def parse_digital(f: typing.BinaryIO) -> DigitalData:
    """Parse a logic analyzer capture file in Saleae Logic 2.0 binary format"""
    # Parse header
    identifier = f.read(8)
    if identifier != b"<SALEAE>":
        raise Exception("Not a saleae file")

    version, datatype = struct.unpack("=ii", f.read(8))

    if version != expected_version or datatype != TYPE_DIGITAL:
        raise Exception("Unexpected data type: {}".format(datatype))

    # Parse digital-specific data
    initial_state, begin_time, end_time, num_transitions = struct.unpack("=iddq", f.read(28))

    # Parse transition times
    transition_times = array.array("d")
    transition_times.fromfile(f, num_transitions)

    return DigitalData(initial_state, begin_time, end_time, num_transitions, transition_times)


def decode_32bit_value(packet: bytearray) -> int:
    """Decode a 32-bit value from the 4 LSBits of the last 8 bytes, as used for
    both CRCs and integer parameters in the SlowSoftSerial test protocol"""

    if len(packet) != 8:
        return 0

    value = (
        packet[-8] << 28
        | packet[-7] << 24
        | packet[-6] << 20
        | packet[-5] << 16
        | packet[-4] << 12
        | packet[-3] << 8
        | packet[-2] << 4
        | packet[-1]
        | packet[-8] & 0xF0
    )
    return value


def check_crc(packet: bytearray) -> bool:
    """Check the CRC in a packet in SlowSoftSerial test protocol format
    and return True if the CRC is good. Diagnose a bad CRC and return False."""

    if len(packet) < 10:
        return False

    received = decode_32bit_value(packet[-8:])
    crc = zlib.crc32(packet[:-8]) & 0xFFFFFFFF

    if received == crc:
        return True
    else:
        print("BAD CRC", hex(received), "should be", hex(crc))
        return False


def describe_packet(packet: bytearray, start_time: float, end_time: float) -> str:
    """Analyze the contents of a packet in SlowSoftSerial test protocol format
    and return a one-line description of the packet. Also, process the PARAMS Response
    packet to enable this program to follow changes in baud rate and serial configuration."""

    global baudrate, onebaud, num_data_bits, num_data_and_parity_bits, num_stop_bits, parity_type

    # We might need end_time one day, but for now it is unused.
    del end_time

    description = f"{start_time:12.06f}: "  # column-aligned timestamp for each packet (up to 99,999 seconds)

    if len(packet) < 10:
        description += "TOO SHORT"
        return description

    if not check_crc(packet):
        description += "BAD CRC"
        return description

    if packet[0] == 0:
        description += "CMD "
    elif packet[0] == 1:
        description += "RSP "
    elif packet[0] == 2:
        description += "DBG "
    else:
        description += "UNK"
        return description

    if packet[1] == 0:
        description += "NOP "
        if len(packet) > 10:
            description += "+"
            description += str(len(packet) - 10)
    elif packet[1] == 1:
        description += "ID"
        if packet[0] == 1 and len(packet) > 10:
            description += ": "
            for ch in packet[2:-9]:
                description += chr(ch)
    elif packet[1] == 2:
        description += "ECHO "
        if len(packet) > 10:
            description += "+"
            description += str(len(packet) - 10)
    elif packet[1] == 3:
        description += "BABBLE"
        if packet[0] == 0:
            if len(packet) != 18:
                description += "(wrong length)"
            else:
                description += ": "
                description += str(decode_32bit_value(packet[2:10]))
        elif packet[0] == 1:
            description += ": "
            description += str(len(packet) - 10)
    elif packet[1] == 4:
        description += "PARAMS "
        if len(packet) != 26:
            description += "(wrong length)"
        else:
            proposed_baudrate = decode_32bit_value(packet[2:10]) / 1000.0
            config = decode_32bit_value(packet[10:18])
            width = width_decode[(config >> 8) & 0x0F]
            parity = parity_decode[config & 0x0F]
            stop = stop_decode[(config >> 4) & 0x0F]
            description += f"PARAMS {proposed_baudrate:5.3f} baud, {width}{parity}{stop}"

            if packet[0] == 1:  # change takes effect after the response packet
                baudrate = proposed_baudrate
                onebaud = 1.0 / baudrate
                num_data_bits = width
                num_data_and_parity_bits = width
                if parity != "N":
                    num_data_and_parity_bits += 1
                num_stop_bits = stop
                parity_type = parity
    elif packet[1] == 0x1F:
        description += "EXT "
    else:
        description += "UNK"

    return description


def process_character(char: int, char_start_time: float, char_end_time: float) -> None:
    """Process a single successfully-received character"""

    global in_packet, packet, in_escape_seq, packet_start_time

    if DUMP_CHARACTERS:
        if char == FEND:
            print("🤨", end=" ")
        elif chr(char) in string.printable and chr(char) not in string.whitespace:
            print(chr(char), end=" ")
        else:
            print(f"{char:02x}", end=" ")

    if not in_packet:  # looking for starting flag
        if char == FEND:  # Here's the start of a packet
            if len(packet) != 0:  # found some garbage between packets
                print(f"Non-packet data, {len(packet)} characters")
                packet = bytearray()  # discard the garbage, start empty
            in_packet = True
            packet_start_time = char_start_time
        else:
            packet.extend(char.to_bytes(1, "big"))  # save non-packet garbage
    else:
        if char == FEND:  # here's the proper end of the packet
            print(describe_packet(packet, packet_start_time, char_end_time))
            packet = bytearray()  # discard processed packet
            in_packet = False
        else:
            if char == FESC:
                in_escape_seq = True
                return  # this was the first of a two-byte sequence
            elif in_escape_seq:
                in_escape_seq = False
                if char == TFESC:  # TFESC
                    char = FESC  # transpose to FESC
                elif char == TFEND:  # TFEND
                    char = FEND  # transpose to FEND
                else:  # ill-formed framing
                    in_packet = False
                    return
            packet.extend(char.to_bytes(1, "big"))  # add the (de-escaped) character to packet


def strip_parity(char: int) -> Tuple[int, bool]:
    """Check the parity bit (if called for in the current configuration).
    Return a tuple of which the first element is the data bits received
    (even if parity did not check) and the second is a boolean that is
    True when parity was correct."""

    parity_bit = (char & (1 << num_data_bits)) != 0
    data_bits = char & ((1 << num_data_bits) - 1)
    computed_even_parity = bool((0x6996 >> ((data_bits ^ (data_bits >> 4)) & 0x0F)) & 0x01)

    if parity_type == "N":
        return data_bits, True
    elif parity_type == "M":
        if parity_bit == 0:
            return data_bits, False
    elif parity_type == "S":
        if parity_bit == 1:
            return data_bits, False
    elif parity_type == "E":
        if parity_bit != computed_even_parity:
            return data_bits, False
    elif parity_type == "O":
        if parity_bit == computed_even_parity:
            return data_bits, False
    else:
        print(f"Unknown parity type {parity_type}")
        sys.exit(1)

    return data_bits, True


def receive_char_from(data: DigitalData, start_bit_transition: int) -> int:
    """Start at transition index and process one character's worth of transitions,
    returning the index of the presumed start bit of the next character."""

    t0 = data.transition_times[start_bit_transition]
    transition = start_bit_transition + 1
    previous_tbaud = 0
    level_before = 1  # logical level of the signal BEFORE this transition
    START_BIT: int = -1
    state: int = START_BIT  # after START_BIT, state counts up from 0 to data bits + parity bits
    char: int = 0  # start with an empty character; we'll OR in a 1 when we see a high bit
    # if parity is in use, we'll put that bit in here too, above the MSbit

    while transition < data.num_transitions:
        t = data.transition_times[transition]
        tbaud = (t - t0) / onebaud  # time in bauds from beginning of start bit
        level_before = 0 if level_before else 1  # keep track of current signal value

        # diagnose transitions that come much too soon, with respect to baud rate
        if tbaud < previous_tbaud + epsilon:
            print(f"Glitch transition at {t=}")
            # do not update previous_tbaud; we don't want the glitch window to drift.
            # otherwise ignore this transition
            transition += 1
            continue
        previous_tbaud = tbaud

        if state == START_BIT:
            if tbaud < (1 - epsilon) or level_before == 1:
                # framing error: transition during a start bit or finished start bit with signal high
                print(f"Framing error: broken start bit at {t=}")
                return start_bit_transition + 2  # earliest possible start bit candidate
            else:
                # start bit ended normally, now we're looking for data bit 0
                state = 0
                # fall through to check for data bit(s)

        while state in range(0, num_data_and_parity_bits):
            if tbaud < (1 + state + epsilon):
                # transition was valid but at the start of the current data bit, carry on
                transition += 1
                break
            elif tbaud < (1 + state + 1 - epsilon):
                # framing error: transition inside the data bit.
                print(f"Framing error: transition during data bit at {t=}")
                return start_bit_transition + 2  # earliest possible start bit candidate
            else:
                if level_before == 1:
                    char |= 1 << state  # high bit received, put it into the character
                state += 1
        if state in range(0, num_data_and_parity_bits):
            continue

        # Now we've seen all the data and parity bits, start looking for a valid stop bit.
        if tbaud < (1 + state + epsilon):
            # transition was at the beginning of the stop bit
            # look to the next transition
            transition += 1
            # if there is a next transition before the end of the capture
            if transition < data.num_transitions:
                continue
            # else: end of capture terminates this stop bit successfully, we assume
        elif tbaud < (1 + state + num_stop_bits - epsilon):
            # framing error: transition inside the stop bit(s)
            print(f"Framing error: transition during stop bit(s) at {t=}")
            return start_bit_transition + 2  # earliest possible start bit candidate
        elif level_before == 0:
            # framing error: wrong level during stop bit(s)
            print(f"Framing error: wrong level during stop bit(s) at {t=}")
            return start_bit_transition + 2  # earliest possible start bit candidate

        data_bits, parity_good = strip_parity(char)
        if parity_good:
            process_character(data_bits, t0, t0 + onebaud * (1 + num_data_and_parity_bits + num_stop_bits))
        else:
            print(f"Parity error at {t=}")

        return transition
        # This is the transition that successfully concluded the stop bits of
        # the current character. Thus, it is the presumed start bit of the next character.
        # Unless the character ended with the end of the capture, in which case
        # this is one past the end of the array.

    return data.num_transitions  # one past the last transition in the data array


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("SlowSoftSerial test protocol captured trace analyzer")
        print(f"  Usage: {sys.argv[0]} file1 file2")
        sys.exit()

    filename0 = sys.argv[1]
    filename1 = sys.argv[2]
    print("Opening " + filename0 + " and " + filename1)

    with open(filename0, "rb") as f:
        data0 = parse_digital(f)

    with open(filename1, "rb") as f:
        data1 = parse_digital(f)

    if data0.initial_state != data1.initial_state:
        print("Initial states don't match")
        sys.exit(1)
        # Matching initial states; we will assume this is the idle state (1).
        # This automatically handles inverted or non-inverted logic levels,
        # provided that the capture starts when the line is idle in both directions.

    if data0.initial_state == 0:
        print("Detected inverted logic at beginning of trace")

    if data0.begin_time != data1.begin_time:
        print("Begin times don't match")
        sys.exit(1)

    if data0.end_time != data1.end_time:
        print("End times don't match")
        sys.exit(1)

    n0 = 0
    t0 = data0.transition_times[n0]

    n1 = 0
    t1 = data1.transition_times[n1]

    while n0 < data0.num_transitions or n1 < data1.num_transitions:
        if t0 < t1:
            t0_char_end = t0 + onebaud * (1 + num_data_and_parity_bits + num_stop_bits)
            n0 = receive_char_from(data0, n0)
            if n0 < data0.num_transitions:
                t0 = data0.transition_times[n0]
                if t0_char_end > t1:
                    print(f"Doubletalk at t={t1}")
                    sys.exit(1)
            elif n0 == data0.num_transitions:
                t0 = data0.end_time
        else:
            t1_char_end = t1 + onebaud * (1 + num_data_and_parity_bits + num_stop_bits)
            n1 = receive_char_from(data1, n1)
            if n1 < data1.num_transitions:
                t1 = data1.transition_times[n1]
                if t1_char_end > t0:
                    print(f"Doubletalk at t={t0}")
                    sys.exit(1)
            elif n1 == data1.num_transitions:
                t1 = data1.end_time

    print(f"{data0.end_time:12.06f}: End of capture")
