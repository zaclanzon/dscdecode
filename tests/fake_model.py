#!/usr/bin/env python3
"""Stand-in for the reference model, for testing tools/compare_model only.

It accepts the model's command line (-F CONFIG) and file conventions but does
no DSC work of its own. FUNCTION 1 writes FAKE_MODEL_FIXTURE's PPS and payload
as NAME.dsc, whatever the image. FUNCTION 2 decodes NAME.dsc with dscdecode
(readings from FAKE_MODEL_READINGS, space-separated NAME=VALUE) into
NAME.out.ppm, then applies FAKE_MODEL_CORRUPT="x,y,component,delta" if set;
a listed NAME.ppm decodes NAME.dsc, as version 1.31a does. FUNCTION 0 does
both, without writing NAME.dsc. With FAKE_MODEL_CRASH_DECODE set, FUNCTION 2
dies from SIGSEGV, as version 1.48 does on native 4:2:2 and 4:2:0 streams.
Without arguments it prints a version banner (FAKE_MODEL_VERSION, default 0).
A YCbCr stream is decoded into NAME.out.yuv, or for 4:4:4 into the model's
8-bit DPX layout (NAME.out.dpx); FAKE_MODEL_CORRUPT then changes the first
byte of the image data by delta.
"""
import os
from pathlib import Path
import signal
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
    if len(sys.argv) == 1:
        print(f"fake model version {os.environ.get('FAKE_MODEL_VERSION', '0')}")
        return
    assert sys.argv[1] == '-F'
    s = settings(sys.argv[2])
    names = Path(s['SRC_LIST']).read_text().split()
    for name in names:
        stem = Path(name).stem
        if s['FUNCTION'] in ('0', '1'):
            fixture = Path(os.environ.get('FAKE_MODEL_FIXTURE', ROOT / 'tests/fixtures/color_crop'))
            data = b'DSCF' + fixture.with_suffix('.pps').read_bytes() + fixture.with_suffix('.bin').read_bytes()
            if s['FUNCTION'] == '1':
                Path(stem + '.dsc').write_bytes(data)
                continue
        if s['FUNCTION'] == '2' and os.environ.get('FAKE_MODEL_CRASH_DECODE'):
            os.kill(os.getpid(), signal.SIGSEGV)
        if s['FUNCTION'] in ('0', '2'):
            if s['FUNCTION'] == '2':
                data = Path(stem + '.dsc' if name.endswith('.ppm') else name).read_bytes()
            pps = data[4:132]
            Path('fake.pps').write_bytes(pps)
            Path('fake.bin').write_bytes(data[132:])
            exe = os.environ.get('DSCDECODE_BIN', str(ROOT / 'build' / 'release' / 'dscdecode'))
            readings = [a for r in os.environ.get('FAKE_MODEL_READINGS', '').split()
                        for a in ('--reading', r)]
            corrupt = os.environ.get('FAKE_MODEL_CORRUPT')
            if not pps[4] & 0x10:
                native = pps[0] & 15 == 2 and pps[88] & 3
                subprocess.run([exe, *readings, 'fake.pps', 'fake.bin', 'fake.yuv'], check=True)
                image = bytearray(Path('fake.yuv').read_bytes())
                if corrupt:
                    image[0] = (image[0] + int(corrupt.split(',')[3])) % 256
                if native or pps[4] & 0x08:
                    Path(stem + '.out.yuv').write_bytes(image)
                else:
                    # 8-bit 4:4:4: Cb, Y, Cr per pixel, bytes reversed in
                    # each 32-bit word (tools/compare_model, dpx_to_yuv).
                    w, h = pps[8] << 8 | pps[9], pps[6] << 8 | pps[7]
                    n = w * h
                    stream = bytearray()
                    for i in range(n):
                        stream += bytes([image[n + i], image[i], image[2 * n + i]])
                    stream += bytes(-len(stream) % 4)
                    body = b''.join(stream[i:i + 4][::-1] for i in range(0, len(stream), 4))
                    header = bytearray(8192)
                    header[0:4] = b'SDPX'
                    header[4:8] = (8192).to_bytes(4, 'big')
                    header[800] = 102
                    Path(stem + '.out.dpx').write_bytes(bytes(header) + body)
                continue
            subprocess.run([exe, *readings, 'fake.pps', 'fake.bin', stem + '.out.ppm'], check=True)
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
