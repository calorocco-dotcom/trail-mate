"""Loopback acceptance wrapper; real production service and graceful stop file."""
import argparse
from pathlib import Path
import threading
import time
import json
import msgpack
from tools.tracks.directory_service import TrackDirectoryService, exclusive_state
from tools.tracks.region_catalog import RegionCatalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--rns-config', type=Path, required=True)
    parser.add_argument('--regions-file', type=Path, required=True)
    parser.add_argument('--stop-file', type=Path, required=True)
    parser.add_argument('--run-seconds', type=int, default=1200)
    parser.add_argument('--audit-file-transfers', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.run_seconds <= 10800:
        parser.error('run-seconds must be between 1 and 10800')
    regions = RegionCatalog.from_file('test', args.regions_file)
    with exclusive_state(args.state):
        service = TrackDirectoryService(args.state, args.rns_config, 'Loopback track directory', regions=regions)
        handle = service.application.handle

        def observed_handle(author, raw):
            request = msgpack.unpackb(raw, raw=False)
            started = time.monotonic()
            response = handle(author, raw)
            reply = msgpack.unpackb(response, raw=False)
            if args.audit_file_transfers and reply[4] == 200 and (request[2] == 4 or request[2] == 19 and request[5][0] == 0):
                digest = request[5][0] if request[2] == 4 else request[5][1]
                # Public ownership/hash/offset only. No token, keys or payload.
                print(json.dumps({'event':'file_begin','owner':author.hex(),'hash':digest.hex(),
                                  'offset':reply[5][1],'sealed':reply[5][2]}),flush=True)
            print(json.dumps({'event': 'request', 'operation': request[2],
                              'status': reply[4], 'requestBytes': len(raw),
                              'responseBytes': len(response),
                              'elapsedMs': round((time.monotonic()-started)*1000)}), flush=True)
            return response

        service.application.handle = observed_handle

        def stop_requested():
            while not service.stopped.wait(.1):
                if args.stop_file.exists():
                    service.stopped.set()

        threading.Thread(target=stop_requested, daemon=True).start()
        # Whole multi-directory lifecycle has dozens of real LXMF transfers.
        # Normal termination uses the orchestrator's explicit stop file.
        service.run(args.run_seconds)


if __name__ == '__main__':
    main()
