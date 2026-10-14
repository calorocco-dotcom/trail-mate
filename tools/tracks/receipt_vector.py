"""Public-only interop fixture; no private material is emitted."""
import json
from pathlib import Path
import subprocess
import sys
import RNS
from tools.tracks.protocol import verify_manifest
from tools.tracks.receipt import sign_receipt

executable=Path('.codex-build/hostlink-codec/native/track_manifest_sign'+('.exe' if sys.platform=='win32' else ''))
manifest=[bytes.fromhex(line) for line in subprocess.check_output([str(executable.resolve())],text=True).splitlines()]
identity=RNS.Identity()
epoch,request=b'e'*16,b'r'*16
receipt=sign_receipt(identity,epoch,request,verify_manifest(manifest),5000000000)
print(json.dumps({'manifest':[value.hex() for value in manifest],'receipt':[value.hex() for value in receipt],
                  'directory':identity.get_public_key().hex(),'epoch':epoch.hex(),'request':request.hex()}))
