#!/usr/bin/env python3
"""dvdecode: decode the CB1 Dolby Vision tunnel metadata packets from a raw
3840x2160 BGRX frame (as written by fbgrab-gl) and check each packet CRC-32.

usage: dvdecode.py FRAME.raw [PACKETS]

Tunnel layout (CB1 pack_gles300.frag): G = Y>>4, R = C>>4,
B = (Y & 15) | (C & 15) << 4. Metadata bits ride in the C LSB XOR parity,
in 1024-bit packets repeated 3 times from pixel 0.
"""
import sys
raw=open(sys.argv[1],'rb').read(3840*4*8)  # first 8 rows are enough
def par(v):
    v^=v>>16; v^=v>>8; v^=v>>4; v^=v>>2; v^=v>>1; return v&1
bits=[]
for i in range(len(raw)//4):
    B,G,R=raw[i*4],raw[i*4+1],raw[i*4+2]
    y=(G<<4)|(B&15); c=(R<<4)|(B>>4)
    bits.append((c&1)^par(c>>1)^par(y))
def tobytes(bb):
    return bytes(int(''.join(map(str,bb[k:k+8])),2) for k in range(0,len(bb),8))
def crc32_mpeg2(data):
    crc=0xffffffff
    for b in data:
        crc^=b<<24
        for _ in range(8):
            crc=((crc<<1)^0x04c11db7)&0xffffffff if crc&0x80000000 else (crc<<1)&0xffffffff
    return crc
for pk in range(int(sys.argv[2]) if len(sys.argv)>2 else 4):
    reps=[tobytes(bits[pk*3072+r*1024:pk*3072+r*1024+1024]) for r in range(3)]
    p=reps[0]; calc=crc32_mpeg2(p[:124]); stored=int.from_bytes(p[124:],'big')
    print(f"packet {pk}: reps_identical={reps[0]==reps[1]==reps[2]} hdr={p[:12].hex()} stored={stored:08x} calc={calc:08x} {'OK' if calc==stored else 'BAD'}")
