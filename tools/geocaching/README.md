# Geocaching Directory Service and Interoperability Tools

`directory_service.py` is the persistent directory service under development.
It exchanges Geocaching v1 requests through native Reticulum/LXMF, implements
capabilities, publish, query, get and sync, and automatically fetches, verifies
and stores objects from capability-verified public directories. There is no
HTTP business-query interface.

`interop_peer.py` remains a separate, capacity-limited test tool. Its
`directory` mode serves in-memory samples and must not be deployed as a
persistent service.

## Startup

Install the pinned dependencies in a Python virtual environment:

```sh
python -m venv .venv
# After activating the virtual environment:
python -m pip install -r tools/geocaching/interop-requirements.txt
python tools/geocaching/directory_service.py --state /private/geocaching --rns-config /private/reticulum --name "Trail Mate directory"
```

On Windows, invoke `.venv\Scripts\python.exe` directly if preferred.
`--rns-config` points to an **existing** Reticulum configuration directory
containing `config`. The service does not generate or overwrite the user's
network configuration. That configuration determines connections to public,
radio or existing transport nodes.

`--state` contains private identity keys, LXMF state, the SQLite database and
an OS process lock. Restrict it to the service account and do not commit it to
Git. Only one service process may use a state directory. A corrupt identity
file prevents startup rather than automatically replacing the identity.

Normal operation continues until SIGINT/SIGTERM. Tests can set a deadline with
`--run-seconds` and fix the first-announce delay with `--startup-delay 0..60`.
Production defaults are a random initial delay of 0–60 seconds, periodic
announces every six hours ±20%, and a minimum 30-minute interval between
content-update announces.

## Current implementation

- SQLite WAL with FULL synchronization. Signed objects, public indexes, change
  logs and publication results commit together. Failed persistence does not
  send a success response. An identical request replays its original result;
  different content with the same request ID does not overwrite that request.
- Query snapshots and sync cursors persist. New publications during pagination
  do not move the captured synchronization watermark. Restart preserves the
  directory identity, epoch, objects, snapshots, request results and replication
  progress.
- Version conflicts freeze normal queries for that ID while retaining and
  propagating signed evidence. Exact historical revisions remain readable.
- Automatic discovery checks the same-identity binding between discovery and
  LXMF delivery destinations. Replication checks capabilities, downloads exact
  hashes and verifies signatures independently. Advance the cursor only after
  a complete page persists. Unknown-result retries retain original request
  bytes, and restart does not skip incomplete objects. Incorrect hashes,
  invalid signatures and nonconforming pages quarantine the source.
- If a first direct message precedes the sender's identity announce, retain
  the original LXMF message within a bounded queue and wait up to 60 seconds.
  After obtaining the identity, reverify the full signature. Unknown identities
  or invalid signatures never execute requests.
- Limits: 256 peers, one active replication request, 32 queued inbound messages
  and 32 in-flight outbound messages. Peer timeout backoff is one minute,
  five minutes, 30 minutes and six hours; normal polling is every six hours.
  One replication round has a 256 KiB application-byte budget, retaining an
  incomplete page for later rounds.
- Default capacities: 100,000 objects, 1,000,000 request results and 256 snapshots,
  each with at most 100,000 items. Retain request results for 30 days, pagination
  snapshots for seven days and final sync checkpoints for 30 days. Objects and
  logs are not automatically purged; at capacity, refuse additions rather than
  deleting data whose retention has been promised.

These describe local implementation and test boundaries, not completed
long-term public-operation acceptance. Further operational work includes source
quotas, total disk limits and reclamation, low-bandwidth replication policy,
bad-source recovery, evidence from multiple sources and deployment metrics.

The browser Worker, raw-packet WSS bridge and Web map are implemented. The
[GitHub Pages map](https://vicliu624.github.io/trail-mate/geocaching/) has verified
public test-point queries, author signatures and GPX download. RNS/LXMF runs
inside the browser rather than replacing business communication with HTTP.

As of 2026-10-14, the existing NAS directory/bridge deployment has been updated
while preserving its identity, epoch and records. Public Geocaching reads,
author signatures and directory publication proofs pass through the maintained
WSS endpoint. This supersedes the temporary development-tunnel status of the
initial 2026-09-24 deployment. Real-Wio create/update/archive acceptance for the
new Web maintenance workflow remains pending.

See [deployment instructions](deploy/README.md) and the
[public Web and device scheduling acceptance record](../../docs/locales/zh-Hans/geocaching-lxmf/26-公网网页与设备调度验收.md)
for configuration and the boundaries of device/public-network evidence.

## Verification without compiling firmware

From the repository root, in an environment with the dependencies above:

```sh
python -m unittest discover -s tools/geocaching -p "test_directory_*.py" -v
python tools/geocaching/test_directory_network.py acceptance --work .codex-build/directory-network-run
python tools/geocaching/test_directory_replication_network.py acceptance --origin .codex-build/directory-network-run --work .codex-build/directory-replication-run
```

Use a fresh `--work` directory for each run to avoid overwriting evidence.
The second network test reads the first test's published object and restarts
the source service with its original directory identity. It does not copy the
source private key into the destination directory. All test interfaces bind
only to `127.0.0.1`; scripts bound and close their processes. Windows child
processes run with hidden windows. Logs and test identities stay in their
respective working directories.

Storage/scheduling tests use real Ed25519 signatures and SQLite. They cover
rollback after receipt persistence fails, pagination snapshots, conflict
evidence, incremental watermarks, restart during operations, incorrect-version
quarantine and request-timeout retries.

The first network test uses an independent publisher, directory process and
fresh reader after restart. The second verifies automatic replication and
retrieval of the same signed object by a new reader after the source is offline
and the destination restarts.

## Local verification record: 2026-09-24

- Ten storage/replication scheduling checks passed, as did two checks for late
  identity arrival and invalid-signature rejection.
- Independent publisher → persistent directory → fresh reader after restart
  passed v1 publication, identical-request replay, query/get, v2 update,
  historical v1 retrieval, sync baseline and empty incremental sync.
  Logs: `.codex-build/directory-network-20260924-a/`.
- Two directories discovered each other through native announces and replicated
  v2 over LXMF. After source shutdown and replica restart, a fresh reader still
  queried and retrieved the same signed bytes.
  Logs: `.codex-build/directory-replication-20260924-c/`.
- These were local multi-process RNS/LXMF checks over TCP. They did not replace
  business communication with HTTP and did not include firmware compilation,
  flashing, physical radio links or public deployment acceptance in that run.
