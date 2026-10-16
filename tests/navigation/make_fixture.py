"""A directed junction where a prohibited turn requires a visible detour."""
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from tmap_pack import write

write({
    "name": "Trail Mate navigation junction",
    "graph": {
        "nodes": [
            {"id": 1, "point": [0, 0]},
            {"id": 2, "point": [0.001, 0]},
            {"id": 3, "point": [0.002, 0]},
            {"id": 4, "point": [0.001, 0.001]},
        ],
        "edges": [
            {"id": "approach", "from": 1, "to": 2},
            {"id": "direct", "from": 2, "to": 3},
            {"id": "detour-a", "from": 2, "to": 4},
            {"id": "detour-b", "from": 4, "to": 3},
        ],
        "restrictions": [
            {"from": "approach", "to": "direct", "access": 1, "kind": "no"}
        ],
    },
}, Path(sys.argv[2]))
