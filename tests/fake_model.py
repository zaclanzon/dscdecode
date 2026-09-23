#!/usr/bin/env python3
"""Stand-in for the reference model, for testing tools/compare_model only.

It accepts the model's command line (-F CONFIG) and file conventions but does
no DSC work of its own. FUNCTION 1 writes FAKE_MODEL_FIXTURE's PPS and payload
as NAME.dsc, whatever the image. FUNCTION 2 decodes NAME.dsc with dscdecode
into NAME.out.ppm, then applies FAKE_MODEL_CORRUPT="x,y,component,delta" if set.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def settings(cfg):
    out = {}
    for line in Path(cfg).read_text().splitlines():
        line = line.split('//')[0].split()
        if len(line) >= 2:
            out.setdefault(line[0], line[1])
    return out


def main():
    assert sys.argv[1] == '-F'
    s = settings(sys.argv[2])
    names = Path(s['SRC_LIST']).read_text().split()
    for name in names:
        stem = Path(name).stem
        if s['FUNCTION'] == '1':
            fixture = Path(os.environ.get('FAKE_MODEL_FIXTURE', ROOT / 'tests/fixtures/color_crop'))
            data = b'DSCF' + fixture.with_suffix('.pps').read_bytes() + fixture.with_suffix('.bin').read_bytes()
            Path(stem + '.dsc').write_bytes(data)
        elif s['FUNCTION'] == '2':
            data = Path(name).read_bytes()
            Path('fake.pps').write_bytes(data[4:132])
            Path('fake.bin').write_bytes(data[132:])
            exe = os.environ.get('DSCDECODE_BIN', str(ROOT / 'dscdecode'))
            subprocess.run([exe, 'fake.pps', 'fake.bin', stem + '.out.ppm'], check=True)
            corrupt = os.environ.get('FAKE_MODEL_CORRUPT')
            if corrupt:
                x, y, c, delta = (int(v) for v in corrupt.split(','))
                ppm = bytearray(Path(stem + '.out.ppm').read_bytes())
                w = int(ppm.split(b'\n')[1].split()[0])
                start = len(b'\n'.join(ppm.split(b'\n')[:3])) + 1
                ppm[start + 3 * (y * w + x) + c] = (ppm[start + 3 * (y * w + x) + c] + delta) % 256
                Path(stem + '.out.ppm').write_bytes(ppm)
    print('fake model: done')


if __name__ == '__main__':
    main()
