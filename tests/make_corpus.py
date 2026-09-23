#!/usr/bin/env python3
"""Package existing independently derived vectors as fuzzer seeds."""
from pathlib import Path
root=Path(__file__).resolve().parent
out=root/'corpus'
out.mkdir(exist_ok=True)
for d in ('fixtures','discriminators'):
    for p in (root/d).glob('*.pps'):
        bitstream=p.with_suffix('.bin')
        if bitstream.exists():
            (out/p.stem).write_bytes(p.read_bytes()+bitstream.read_bytes())
