#!/usr/bin/env python3
# gt_oklab.py -- golden test for the Oklab engine against BOTH references:
#   A) Ottosson published values (the spec; default space=3 matrices)
#   B) colour-science XYZ_to_Oklab with THEIR matrices (independent impl)
# Needs: colour-science (pip install via a CN mirror if needed), numpy, g++.
import subprocess, ctypes, sys
import numpy as np

def build():
    subprocess.run(['g++','-O2','-std=c++17','-fPIC','-shared','bridge_oklab.cpp',
                    '-I.','-I../src','-o','liboklab_gt.so'], check=True)

def main():
    build()
    import colour
    import colour.models.oklab as ok
    lib = ctypes.CDLL('./liboklab_gt.so')
    f = lib.cr_linrgb_to_oklab; f.argtypes = [ctypes.c_float]*3 + [ctypes.POINTER(ctypes.c_float)]*3
    fm = lib.cr_linrgb_to_oklab_mtx
    fm.argtypes = [ctypes.POINTER(ctypes.c_float)]*2 + [ctypes.c_float]*3 + [ctypes.POINTER(ctypes.c_float)]*3
    fi = lib.cr_oklab_to_linrgb; fi.argtypes = [ctypes.c_float]*3 + [ctypes.POINTER(ctypes.c_float)]*3

    rng = np.random.default_rng(42)
    rgb = rng.random((2000,3)).astype(np.float64)
    okk = True

    # ---- A) spec: Ottosson published values ----
    pub = [((1,0,0),(0.62796, 0.22486, 0.12585)),
           ((0,1,0),(0.86644,-0.23389, 0.17950)),
           ((0,0,1),(0.45201,-0.03246,-0.31153))]
    errA = 0.0
    for (r,g,b),(L0,a0,b0) in pub:
        L=ctypes.c_float(); a=ctypes.c_float(); bb=ctypes.c_float()
        f(r,g,b, ctypes.byref(L),ctypes.byref(a),ctypes.byref(bb))
        errA = max(errA, abs(L.value-L0), abs(a.value-a0), abs(bb.value-b0))
    print('A) Ottosson spec   : maxerr %.2e  %s' % (errA, 'PASS' if errA<1e-4 else 'FAIL'))
    okk &= errA < 1e-4

    # ---- B) colour-science: inject THEIR matrices into our generic engine ----
    M1c = np.array(ok.MATRIX_1_XYZ_TO_LMS, dtype=np.float32).ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    M2c = np.array(ok.MATRIX_2_LMS_TO_LAB, dtype=np.float32).ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    ref = colour.XYZ_to_Oklab(rgb)          # colour's own pipeline = ground truth
    errB = 0.0
    for i in range(len(rgb)):
        L=ctypes.c_float(); a=ctypes.c_float(); bb=ctypes.c_float()
        fm(M1c, M2c, float(rgb[i,0]),float(rgb[i,1]),float(rgb[i,2]),
           ctypes.byref(L),ctypes.byref(a),ctypes.byref(bb))
        errB = max(errB, abs(L.value-ref[i,0]), abs(a.value-ref[i,1]), abs(bb.value-ref[i,2]))
    print('B) colour-science  : maxerr %.2e  %s' % (errB, 'PASS' if errB<1e-4 else 'FAIL'))
    okk &= errB < 1e-4

    # ---- C) spec roundtrip ----
    errC = 0.0
    for i in range(1000):
        L=ctypes.c_float(); a=ctypes.c_float(); bb=ctypes.c_float()
        r,g,b = float(rgb[i,0]),float(rgb[i,1]),float(rgb[i,2])
        f(r,g,b, ctypes.byref(L),ctypes.byref(a),ctypes.byref(bb))
        r2=ctypes.c_float(); g2=ctypes.c_float(); b2=ctypes.c_float()
        fi(L.value,a.value,bb.value, ctypes.byref(r2),ctypes.byref(g2),ctypes.byref(b2))
        errC = max(errC, abs(r2.value-r), abs(g2.value-g), abs(b2.value-b))
    print('C) spec roundtrip  : maxerr %.2e  %s' % (errC, 'PASS' if errC<1e-3 else 'FAIL'))
    okk &= errC < 1e-3

    print('OKLAB GOLDEN TEST:', 'ALL PASS' if okk else 'FAILED')
    sys.exit(0 if okk else 1)

if __name__ == '__main__':
    main()
