"""Capture navigation diagnostics over HostLink USB CDC or a passive UART."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import time
from navigation_capture_transport import encode_frame, FrameDecoder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=45)
    parser.add_argument('--uart', action='store_true', help='Passive text UART; do not send HELLO')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be between 1 and 60')
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
    port.port = args.port
    # Native CDC connection signals, no bootloader/reset sequence.
    port.dtr = not args.uart
    port.rts = not args.uart
    args.output.parent.mkdir(parents=True, exist_ok=True)
    prefixes = ('[Navigation]', '[SD]', '[TMAP][SD]', '[USB]', '[USB_MSC]')
    buffered = bytearray()
    decoder = FrameDecoder()
    lines = 0
    try:
        port.open()
        if not args.uart:
            # The sole command is HELLO: no configuration, SD or navigation writes.
            port.write(encode_frame(1))
            deadline = time.monotonic() + 5
            acknowledged = False
            while time.monotonic() < deadline and not acknowledged:
                acknowledged = any(kind == 2 for kind, _ in decoder.feed(port.read(512)))
            if not acknowledged:
                raise RuntimeError('No HostLink HELLO_ACK; navigation capture has not started')
        with args.output.open('w', encoding='utf-8') as log:
            log.write(f'# Navigation serial capture {datetime.now(timezone.utc).isoformat()} port={args.port}\n')
            log.flush()
            started = time.monotonic()
            print(f'Capturing {args.port} for {args.seconds:g}s; perform the navigation action now.', flush=True)
            while time.monotonic() - started < args.seconds:
                incoming = port.read(512)
                if args.uart:
                    buffered.extend(incoming)
                    messages = []
                    while b'\n' in buffered:
                        raw, _, remaining = buffered.partition(b'\n')
                        buffered = bytearray(remaining)
                        messages.append(raw)
                    if len(buffered) > 8192:
                        buffered.clear()
                else:
                    messages = [payload for kind, payload in decoder.feed(incoming) if kind == 0x83]
                for raw in messages:
                    text = raw.decode('utf-8', errors='replace').strip()
                    if not text.startswith(prefixes):
                        continue
                    line = f'{time.monotonic() - started:8.3f} {text}'
                    log.write(line + '\n')
                    log.flush()
                    print(line, flush=True)
                    lines += 1
            if buffered.startswith(b'[Navigation]'):
                log.write('# Incomplete trailing Navigation line: reproduction capture ended before newline\n')
        print(f'Capture finished: {lines} diagnostic lines; {args.output}', flush=True)
    finally:
        if port.is_open:
            port.close()


if __name__ == '__main__':
    main()
