#!/usr/bin/env python3
"""Restricted, hand-specified DSC 1.1 syntax fixtures, NOT a general encoder.

Derivation: DSC 1.1 Tables 4-6/6-1, sections 6.4.1, 6.6.1, 7.7 and Annex E.
Only fixed patterns at QP zero with MMAP/MPP/ICH syntax are supported.
Expected pixels come directly from the mathematical pattern, never a decoder.
No reference-model source or executable is used.
"""
from pathlib import Path
import hashlib
import json

OUT = Path(__file__).parent / "fixtures"
W, H = 96, 3


def pps(width, height=H, slice_width=W):
    p = bytearray(128)
    def word(i, v):
        p[i:i+2] = v.to_bytes(2, "big")
    p[0] = 0x11
    p[3] = 0x89                 # 8 bpc, 9-bit line buffer
    p[4] = 0x10                 # convert_rgb, CBR, BP disabled
    p[5] = 128                  # 8.0 bpp, four fractional bits
    extra = 246
    while (slice_width*H*8-extra) % 48:
        extra -= 1
    groups = ((slice_width+2)//3)*H
    slice_offset = ((8192-6144+extra)*2048+groups-1)//groups
    for i, v in [(6,height),(8,width),(10,H),(12,slice_width),(14,slice_width),
                 (16,512),(18,256),(22,0),(24,1),(28,0),
                 (30,slice_offset),(32,6144),(34,4096+extra),(38,8192)]:
        word(i,v)
    p[21] = 32
    p[36] = p[37] = 15         # No flatness flag when master QP is zero
    p[40] = 6
    p[41] = p[42] = 11
    p[43] = 0x33
    p[44:58] = bytes([14,28,42,56,70,84,98,105,112,119,121,123,125,126])
    # All 15 min-QP/max-QP/BPG-offset triples zero. Pins the QP to zero.
    return bytes(p)


def signed_size(n):
    if n == 0:
        return 0
    return next(k for k in range(1,10) if -(1 << (k-1)) <= n < (1 << (k-1)))


def clamp(n, lo, hi):
    return min(hi,max(lo,n))


def slice_syntax(gray, mode='mmap'):
    units = []
    last_size = [0,0,0]
    for y in range(H):
        for x in range(0,W,3):
            if mode == 'ich' and units:
                # Initial P group inserts three128 entries. The last inserted
                # entry is index0; repeatedly referring to it preserves128.
                prefix = '0'*9 if len(units) == 1 else '1'
                units.append([prefix+'00000','00000','00000'])
                continue
            residual = []
            left = gray[y][x-1] if x else 128
            for j in range(3):
                if not y:
                    prediction = clamp(left+sum(residual),0,255)
                else:
                    # At QP0 QuantDivisor/2 truncates to zero: blend = original.
                    above = gray[y-1][x:x+j+1]
                    corner = gray[y-1][x-1] if x else 128
                    prediction = clamp(left+above[-1]-corner+sum(residual),
                                       min([left]+above),max([left]+above))
                residual.append(gray[y][x+j]-prediction)
            sizes = [signed_size(v) for v in residual]
            if max(sizes) >= 8:
                residual = [gray[y][x+j]-128 for j in range(3)]
                sizes = [8,8,8]  # Explicit midpoint predictor, DSC 6.4.4.2
            adjusted = min(last_size[0],7)
            size = max(adjusted,max(sizes))
            ybits = '0'*(size-adjusted)+'1'
            if size:
                ybits += ''.join(format(v & ((1<<size)-1),f'0{size}b') for v in residual)
            last_size[0] = (sizes[0]+sizes[1]+2*sizes[2]+2)//4
            units.append([ybits,'1','1'])  # Co=Cg=256, every chroma residual zero
    return units


def multiplex(units, capacity=W*H):
    # Table4-6: before each group, refill each SSP with48 bits if fullness<36.
    streams = [''.join(u[c] for u in units) for c in range(3)]
    offset, fullness = [0]*3, [0]*3
    words = []
    for u in units:
        for c in range(3):
            if fullness[c] < 36:
                words.append(streams[c][offset[c]:offset[c]+48].ljust(48,'0'))
                offset[c] += 48
                fullness[c] += 48
        for c in range(3):
            fullness[c] -= len(u[c])
            assert fullness[c] >= 0
    raw = ''.join(words)
    assert len(raw) <= capacity*8, (len(raw),capacity*8)
    padded = raw.ljust(capacity*8,'0')
    return int(padded,2).to_bytes(len(padded)//8,'big'), len(raw), streams


def color_units(rgb):
    """Fixed QP0 construction using independent forward RGB transform (§6.1)."""
    height, width = len(rgb), len(rgb[0])
    samples = []
    for row in rgb:
        converted = []
        for r,g,b in row:
            co = r-b
            t = b+(co//2)
            cg = g-t
            converted.append((t+cg//2,co+256,cg+256))
        samples.append(converted)
    units, previous_size = [], [0,0,0]
    for y in range(height):
        for x in range(0,width,3):
            group = []
            for c,depth in enumerate((8,9,9)):
                midpoint = 1 << (depth-1)
                left = samples[y][x-1][c] if x else midpoint
                residual = []
                for j in range(3):
                    if x+j >= width:
                        residual.append(0)
                        continue
                    if y == 0:
                        prediction = clamp(left+sum(residual),0,(1<<depth)-1)
                    else:
                        above = [samples[y-1][k][c] for k in range(x,x+j+1)]
                        corner = samples[y-1][x-1][c] if x else midpoint
                        prediction = clamp(left+above[-1]-corner+sum(residual),
                                           min([left]+above),max([left]+above))
                    residual.append(samples[y][x+j][c]-prediction)
                sizes = [signed_size(v) for v in residual]
                if max(sizes) >= depth:
                    residual = [samples[y][x+j][c]-midpoint if x+j<width else 0 for j in range(3)]
                    sizes = [depth]*3
                adjusted = min(previous_size[c],depth-1)
                size = max(adjusted,max(sizes))
                bits = '0'*(size-adjusted)+('1' if c==0 or size<depth else '')
                if size:
                    bits += ''.join(format(v&((1<<size)-1),f'0{size}b') for v in residual)
                group.append(bits)
                previous_size[c] = (sizes[0]+sizes[1]+2*sizes[2]+2)//4
            units.append(group)
    return units


def color_fixture(manifest):
    name, width, height, sw, sh = 'color_crop',187,5,95,3
    def pixel(x,y):
        return (80+x//8,100+y,150-x//16)
    payloads, details = [], []
    for sy in range(2):
        for sx in range(2):
            rgb = [[pixel(min(sx*sw+x,width-1),min(sy*sh+y,height-1))
                    for x in range(sw)] for y in range(sh)]
            units = color_units(rgb)
            payload,used,streams = multiplex(units,sw*sh)
            index = sy*2+sx
            payloads.append(payload)
            (OUT/f'{name}.slice{index}.bin').write_bytes(payload)
            (OUT/f'{name}.slice{index}.syntax.txt').write_text('\n'.join(' '.join(u) for u in units)+'\n')
            details.append({'slice':index,'mux_bits':used,'syntax_bits':[len(t) for t in streams]})
    framed = b''.join(payloads[sy*2+sx][y*sw:(y+1)*sw]
                      for sy in range(2) for y in range(sh) for sx in range(2))
    expected = f'P6\n{width} {height}\n255\n'.encode()+bytes(
        c for y in range(height) for x in range(width) for c in pixel(x,y))
    (OUT/f'{name}.pps').write_bytes(pps(width,height,sw))
    (OUT/f'{name}.bin').write_bytes(framed)
    (OUT/f'{name}.expected.ppm').write_bytes(expected)
    manifest[name] = {'width':width,'height':height,'slices':4,'details':details,
                      'payload_sha256':hashlib.sha256(framed).hexdigest(),
                      'expected_sha256':hashlib.sha256(expected).hexdigest()}


def main():
    OUT.mkdir(exist_ok=True)
    manifest = {}
    for name,count,mode,pattern in [
        ('flat',1,'mmap',lambda x,y:128),
        ('gradient',1,'mmap',lambda x,y:80+x+y),
        ('checker',2,'mmap',lambda x,y:120+16*((x//5+y)%2)),
        ('mpp',1,'mpp',lambda x,y:([0,255,0][x] if y == 0 and x < 3 else 128)),
        ('ich',1,'ich',lambda x,y:128),
    ]:
        width = W*count
        pixels = [[pattern(x,y) for x in range(width)] for y in range(H)]
        payloads = []
        info = []
        for s in range(count):
            source = [r[s*W:(s+1)*W] for r in pixels]
            units = slice_syntax(source,mode)
            payload,used,streams = multiplex(units)
            payloads.append(payload)
            (OUT/f'{name}.slice{s}.bin').write_bytes(payload)
            # Readable bit-level provenance. Every line is one group, Y Co Cg.
            (OUT/f'{name}.slice{s}.syntax.txt').write_text(
                '\n'.join(' '.join(u) for u in units)+'\n')
            info.append({'slice':s,'mux_bits':used,'syntax_bits':[len(t) for t in streams]})
        framed = b''.join(p[y*W:(y+1)*W] for y in range(H) for p in payloads)
        expected = f'P6\n{width} {H}\n255\n'.encode()+bytes(v for r in pixels for g in r for v in (g,g,g))
        (OUT/f'{name}.pps').write_bytes(pps(width))
        (OUT/f'{name}.bin').write_bytes(framed)
        (OUT/f'{name}.expected.ppm').write_bytes(expected)
        manifest[name] = {'width':width,'height':H,'slices':count,'details':info,
                          'payload_sha256':hashlib.sha256(framed).hexdigest(),
                          'expected_sha256':hashlib.sha256(expected).hexdigest()}
    color_fixture(manifest)
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__ == '__main__':
    main()
