"""Loopback directory with signed fixture and a compression-sized test record."""
import argparse
import os
from pathlib import Path
import threading
import time
import RNS

from directory_service import DirectoryService, exclusive_state
from directory_store import packed, unpacked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--startup-delay', type=float, default=5)
    parser.add_argument('--empty', action='store_true', help='Start without browse fixtures for author-maintenance acceptance')
    parser.add_argument('--run-seconds', type=float, default=180)
    parser.add_argument('--query-capacity',type=int,help='Test-only snapshot capacity, 1..256')
    parser.add_argument('--browse-fixture-count',type=int,default=0,help='Additional local pagination fixtures, 0..64')
    args = parser.parse_args()
    if not 0 < args.run_seconds <= 86400:
        parser.error('run-seconds must be within 0..86400')
    if args.query_capacity is not None and not 1<=args.query_capacity<=256:
        parser.error('query-capacity must be within 1..256')
    if not 0<=args.browse_fixture_count<=64 or args.empty and args.browse_fixture_count:
        parser.error('browse-fixture-count requires fixtures and must be within 0..64')
    args.state.mkdir(parents=True, exist_ok=True)
    config = args.state / 'rns'
    config.mkdir(exist_ok=True)
    (config / 'config').write_text('[reticulum]\nshare_instance = No\nenable_transport = Yes\n'
                                 '[interfaces]\n[[Browser test]]\nenabled = Yes\ntype = TCPServerInterface\n'
                                 f'listen_ip = 127.0.0.1\nlisten_port = {args.port}\n')
    with exclusive_state(args.state / 'service'):
        service = DirectoryService(args.state / 'service', config, 'Browser acceptance directory')
        if args.query_capacity is not None:
            service.store.max_snapshots=args.query_capacity
        if os.environ.get('GEOCACHING_NETWORK_DEBUG') == '1':
            RNS.loglevel = RNS.LOG_DEBUG
        if not args.empty:
            fixture = Path(__file__).resolve().parents[2] / 'modules/core_geocaching/tests/fixtures/signed-cache-v1.bin'
            signed = unpacked(fixture.read_bytes())
            source = os.urandom(16)
            assert unpacked(service.store.handle(source, packed([1, 0, 1, os.urandom(16), 8192, [signed]])))[4] == 200
            now = int(time.time())
            record = [1, service.identity.get_public_key(), os.urandom(16), 1, None, 0, 310000000, 1210000000,
                      'Browser Resource <&>', ('<b>plain text</b> &' * 200)[:2048], '&' * 512, 3, 4, 2, now, now]
            raw = packed(record)
            signed = [raw, service.identity.sign(b'trailmate.geocache/sign/v1\0' + raw)]
            assert unpacked(service.store.handle(source, packed([1, 0, 1, os.urandom(16), 8192, [signed]])))[4] == 200
            (args.state / 'large-signed.bin').write_bytes(packed(signed))
            for index in range(args.browse_fixture_count):
                record[2]=os.urandom(16)
                record[8]='Local pagination fixture '+str(index)
                record[9]=''
                record[10]=''
                raw=packed(record)
                signed=[raw,service.identity.sign(b'trailmate.geocache/sign/v1\0'+raw)]
                assert unpacked(service.store.handle(source,packed([1,0,1,os.urandom(16),8192,[signed]])))[4]==200

        if args.query_capacity is not None:
            import json
            handle=service.store.handle
            def observed_handle(source,raw):
                response=handle(source,raw)
                operation=unpacked(raw)[2]
                status=unpacked(response)[4] if response else None
                print(json.dumps({'event':'query_capacity','operation':operation,'status':status,
                                  'snapshots':service.store.db.execute('SELECT COUNT(*) FROM snapshots').fetchone()[0]}),flush=True)
                return response
            service.store.handle=observed_handle

        def stop_when_requested():
            while not service.stopped.wait(.1):
                if (args.state / 'stop').exists():
                    service.stopped.set()

        threading.Thread(target=stop_when_requested, daemon=True).start()
        service.run(startup_delay=args.startup_delay, run_seconds=args.run_seconds)


if __name__ == '__main__':
    main()
