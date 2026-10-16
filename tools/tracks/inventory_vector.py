"""Deterministic canonical CMP1 inventory vector; no identity or network use."""
import json
from tools.tracks.protocol import packed
row=[4294967296,b'o'*32,b'r'*32,1,0,None,b'f'*32,1024,1]
print(json.dumps({'hex':packed([4294967296,4294967296,0,[row]]).hex()}))
