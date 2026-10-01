#!/usr/bin/env python3
# golden_test.py - validate ColorRestore Vibrance against FFmpeg vf_vibrance.
# Needs: ffmpeg (vibrance filter), numpy, g++. Run from this directory.
import subprocess, ctypes, sys
import numpy as np

W, H, NFRAMES = 320, 240, 3

CASES = [
    ("intensity=0.5",                          dict(intensity=0.5,  rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=1.0",                          dict(intensity=1.0,  rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=-1.0",                         dict(intensity=-1.0, rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=0.8,rbal/gbal/bbal=0.5/1/1.5", dict(intensity=0.8,  rb=0.5, gb=1, bb=1.5, alt=0)),
    ("intensity=1.0:alternate=1",              dict(intensity=1.0,  rb=1,   gb=1, bb=1,   alt=1)),
]

def main():
    subprocess.run(['g++','-O2','-std=c++17','-fPIC','-shared','bridge.cpp',
                    '-I.','-I../src','-o','libbridge_gt.so'], check=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi',
        '-i','testsrc2=size=%dx%d:rate=1' % (W,H),'-frames:v',str(NFRAMES),
        '-pix_fmt','rgb24','-f','rawvideo','-y','in.raw'], check=True)
    lib = ctypes.CDLL('./libbridge_gt.so')
    lib.cr_vibrance_rgb24.argtypes = [ctypes.c_void_p]*2 + [ctypes.c_int]*2 + [ctypes.c_float]*4 + [ctypes.c_int]
    inp = np.fromfile('in.raw', dtype=np.uint8).reshape(NFRAMES,H,W,3)
    ok_all = True
    for name, p in CASES:
        out_ff = 'ff_%d_%.1f.raw' % (p['alt'], p['intensity'])
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24',
            '-s','%dx%d' % (W,H),'-i','in.raw','-vf',
            'vibrance=intensity=%s:rbal=%s:gbal=%s:bbal=%s%s' % (
                p['intensity'],p['rb'],p['gb'],p['bb'],
                ':alternate=1' if p['alt'] else ''),
            '-f','rawvideo','-pix_fmt','rgb24','-y',out_ff], check=True)
        ff = np.fromfile(out_ff, dtype=np.uint8).reshape(NFRAMES,H,W,3)
        ours = np.empty_like(inp)
        for i in range(NFRAMES):
            src = np.ascontiguousarray(inp[i])
            lib.cr_vibrance_rgb24(src.ctypes.data, ours[i].ctypes.data, W, H,
                p['intensity'], p['rb'], p['gb'], p['bb'], p['alt'])
        d = np.abs(ff.astype(int) - ours.astype(int))
        ok = d.max() <= 1   # 1 LSB tolerance for float op-order / FMA differences
        ok_all &= ok
        print('%-42s maxdiff=%3d  %s' % (name, d.max(), 'PASS' if ok else 'FAIL'))
    print('GOLDEN TEST:', 'ALL PASS' if ok_all else 'FAILED')
    sys.exit(0 if ok_all else 1)

if __name__ == '__main__':
    main()
