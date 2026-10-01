#!/usr/bin/env python3
# golden_test.py - validate Vibrance against FFmpeg vf_vibrance.
# Planar gbrp end-to-end: the packed<->planar conversion path of swscale
# changed rounding in ffmpeg 6.0 (+/-2 LSB), so this harness feeds and
# reads planar gbrp directly, bypassing it. The filter itself is bit-stable
# across ffmpeg 5.1 .. 8.x (verified).
#
# Two suites, both version-independent (luma coefficients passed explicitly):
#   A) legacy mode (our default) vs ffmpeg with 5.x/6.x defaults
#   B) modern mode (legacy=false) vs ffmpeg with 7+ defaults (proper BT.709)
# Needs: ffmpeg, numpy, g++.
import subprocess, ctypes, sys, os
import numpy as np

W, H, NFRAMES = 320, 240, 3
LEGACY_LUMA = "rlum=0.072186:glum=0.715158:blum=0.212656"   # FFmpeg 5.x/6.x (swapped)
MODERN_LUMA = "rlum=0.212656:glum=0.715158:blum=0.072186"   # FFmpeg 7+ (BT.709)

CASES = [
    ("intensity=0.5",                  dict(intensity=0.5,  rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=1.0",                  dict(intensity=1.0,  rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=-1.0",                 dict(intensity=-1.0, rb=1,   gb=1, bb=1,   alt=0)),
    ("intensity=0.8,rbal/gbal/bbal",   dict(intensity=0.8,  rb=0.5, gb=1, bb=1.5, alt=0)),
    ("intensity=1.0:alternate=1",      dict(intensity=1.0,  rb=1,   gb=1, bb=1,   alt=1)),
]

def build_bridge():
    subprocess.run(['g++','-O2','-std=c++17','-fPIC','-shared','bridge.cpp',
                    '-I.','-I../src','-o','libbridge_gt.so'], check=True)

def rgb_to_gbrp(rgb):     # (N,H,W,3) -> gbrp raw bytes (G planes, B planes, R planes)
    return np.concatenate([rgb[:,:,:,1].reshape(NFRAMES,-1),
                           rgb[:,:,:,2].reshape(NFRAMES,-1),
                           rgb[:,:,:,0].reshape(NFRAMES,-1)], axis=1).astype(np.uint8)

def gbrp_to_rgb(p):       # (N,3,H,W) -> (N,H,W,3)
    return np.stack([p[:,2],p[:,0],p[:,1]], axis=3)

def run_suite(name, luma_str, luma_ours, inp_rgb, inp_gbrp, lib):
    ok_all = True
    for cname, p in CASES:
        out_ff = 'ff_tmp.raw'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','rawvideo','-pix_fmt','gbrp',
            '-s','%dx%d' % (W,H),'-i','in.gbrp','-vf',
            'vibrance=intensity=%s:rbal=%s:gbal=%s:bbal=%s:%s%s' % (
                p['intensity'],p['rb'],p['gb'],p['bb'],luma_str,
                ':alternate=1' if p['alt'] else ''),
            '-f','rawvideo','-pix_fmt','gbrp','-y',out_ff], check=True)
        ff = np.fromfile(out_ff, dtype=np.uint8).reshape(NFRAMES,3,H,W)
        ours_rgb = np.empty_like(inp_rgb)
        for i in range(NFRAMES):
            lib.cr_vibrance_rgb24(np.ascontiguousarray(inp_rgb[i]).ctypes.data,
                ours_rgb[i].ctypes.data, W, H,
                p['intensity'], p['rb'], p['gb'], p['bb'], p['alt'], *luma_ours)
        ours_p = np.stack([ours_rgb[:,:,:,1],ours_rgb[:,:,:,2],ours_rgb[:,:,:,0]],axis=1)
        d = np.abs(ff.astype(int) - ours_p.astype(int))
        ok = d.max() <= 1   # 1 LSB tolerance (float op-order). Real port bugs show diffs of 37+
        ok_all &= ok
        print('  [%s] %-30s maxdiff=%3d  %s' % (name, cname, d.max(), 'PASS' if ok else 'FAIL'))
        os.remove(out_ff)
    return ok_all

def main():
    build_bridge()
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi',
        '-i','testsrc2=size=%dx%d:rate=1' % (W,H),'-frames:v',str(NFRAMES),
        '-pix_fmt','rgb24','-f','rawvideo','-y','in.rgb'], check=True)
    inp_rgb = np.fromfile('in.rgb', dtype=np.uint8).reshape(NFRAMES,H,W,3)
    rgb_to_gbrp(inp_rgb).tofile('in.gbrp')
    lib = ctypes.CDLL('./libbridge_gt.so')
    lib.cr_vibrance_rgb24.argtypes = [ctypes.c_void_p]*2 + [ctypes.c_int]*2 + [ctypes.c_float]*4 + [ctypes.c_int] + [ctypes.c_float]*3
    a = run_suite('legacy', LEGACY_LUMA, (0.072186, 0.715158, 0.212656), inp_rgb, None, lib)
    b = run_suite('modern', MODERN_LUMA, (0.212656, 0.715158, 0.072186), inp_rgb, None, lib)
    print('GOLDEN TEST:', 'ALL PASS' if (a and b) else 'FAILED')
    sys.exit(0 if (a and b) else 1)

if __name__ == '__main__':
    main()
