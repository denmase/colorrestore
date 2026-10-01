#!/usr/bin/env python3
# gt_colortemp.py -- golden test for ColorTemp against FFmpeg vf_colortemperature.
# Needs: ffmpeg with colortemperature filter, numpy, g++.
import subprocess, ctypes, sys
import numpy as np

W, H, NFRAMES = 320, 240, 3
CASES = [
    ("t=3200",        dict(t=3200, mix=1.0, pl=0.0)),
    ("t=9300",        dict(t=9300, mix=1.0, pl=0.0)),
    ("t=4500:mix=0.5",dict(t=4500, mix=0.5, pl=0.0)),
    ("t=6500:pl=1",   dict(t=6500, mix=1.0, pl=1.0)),
    ("t=20000:mix=0.7:pl=0.4", dict(t=20000, mix=0.7, pl=0.4)),
]

def main():
    subprocess.run(['g++','-O2','-std=c++17','-fPIC','-shared','bridge_colortemp.cpp',
                    '-I.','-I../src','-o','libct_gt.so'], check=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi',
        '-i','testsrc2=size=%dx%d:rate=1' % (W,H),'-frames:v',str(NFRAMES),
        '-pix_fmt','rgb24','-f','rawvideo','-y','ct_in.raw'], check=True)
    lib = ctypes.CDLL('./libct_gt.so')
    lib.cr_colortemp_rgb24.argtypes = [ctypes.c_void_p]*2 + [ctypes.c_int]*2 + [ctypes.c_float]*3
    inp = np.fromfile('ct_in.raw', dtype=np.uint8).reshape(NFRAMES,H,W,3)
    ok_all = True
    for name, p in CASES:
        out_ff = 'ct_ff.raw'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24',
            '-s','%dx%d' % (W,H),'-i','ct_in.raw','-vf',
            'colortemperature=temperature=%s:mix=%s:pl=%s' % (p['t'],p['mix'],p['pl']),
            '-f','rawvideo','-pix_fmt','rgb24','-y',out_ff], check=True)
        ff = np.fromfile(out_ff, dtype=np.uint8).reshape(NFRAMES,H,W,3)
        ours = np.empty_like(inp)
        for i in range(NFRAMES):
            src = np.ascontiguousarray(inp[i])
            lib.cr_colortemp_rgb24(src.ctypes.data, ours[i].ctypes.data, W,H,p['t'],p['mix'],p['pl'])
        d = np.abs(ff.astype(int) - ours.astype(int))
        ok = d.max() <= 1   # 1 LSB tolerance for float op-order / FMA differences
        ok_all &= ok
        print('%-26s maxdiff=%3d  %s' % (name, d.max(), 'PASS' if ok else 'FAIL'))
    print('COLORTEMP GOLDEN TEST:', 'ALL PASS' if ok_all else 'FAILED')
    sys.exit(0 if ok_all else 1)

if __name__ == '__main__':
    main()
