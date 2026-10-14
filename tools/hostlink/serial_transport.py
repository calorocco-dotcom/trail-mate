"""Bounded serial exchanges for the read-only HostLink probe."""
import binascii
import json
import os
import struct
import sys
import time
import serial

port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
port.port = sys.argv[1]
port.dtr = True
port.rts = True
diagnostics = os.environ.get('TRAIL_MATE_SERIAL_DIAGNOSTICS') == '1'
diagnostic_line = bytearray()
try:
    port.open()
    for line in sys.stdin:
        command = json.loads(line)
        outgoing = bytes.fromhex(command['hex'])
        port.write(outgoing)
        deadline = time.monotonic() + 8
        buffered = bytearray()
        result = None
        while time.monotonic() < deadline and result is None:
            buffered.extend(port.read(512))
            while len(buffered) >= 8:
                if buffered[:3] != b'HL\x01':
                    if diagnostics:
                        byte = buffered[0]
                        if byte == 10:
                            if diagnostic_line.startswith(b'[Geocaching][WebConfirm]'):
                                print(diagnostic_line.decode('ascii', errors='replace').strip(), file=sys.stderr, flush=True)
                            diagnostic_line.clear()
                        elif 32 <= byte <= 126 and len(diagnostic_line) < 256:
                            diagnostic_line.append(byte)
                        elif byte != 13:
                            diagnostic_line.clear()
                    del buffered[0]
                    continue
                sequence, size = struct.unpack_from('<HH', buffered, 4)
                if size > 512:
                    del buffered[0]
                    continue
                if len(buffered) < size + 10:
                    break
                frame = bytes(buffered[:size + 10])
                del buffered[:size + 10]
                if binascii.crc_hqx(frame[:-2], 0xffff) != int.from_bytes(frame[-2:], 'little'):
                    continue
                if sequence == command['sequence'] and frame[3] == command['response']:
                    result = frame.hex()
                    break
        print(json.dumps({'hex': result, 'error': None if result else 'TIMEOUT'}), flush=True)
except Exception as error:
    print(json.dumps({'error': type(error).__name__ + ': ' + str(error)}), flush=True)
    sys.exit(1)
finally:
    if port.is_open:
        port.close()
