# ColorRestore — AviSynth+ Plugin

AviSynth+ plugin for color correction. Currently ships one filter: **Vibrance**.

## Vibrance

A **bit-exact** port of FFmpeg's `vf_vibrance` (validated against the
`ffmpeg` CLI, see `golden_test.py`), plus an original tweak layer:
skin-tone protection, saturation limiter, shadow/highlight protection,
adaptive gain, and three working spaces (gamma / linear light / Oklab).

### Syntax

```
Vibrance(clip c,
  # ---- FFmpeg vf_vibrance parameters (frozen, golden-tested) ----
  float "intensity" = 0,          # -2..2
  float "rbal" = 1, "gbal" = 1, "bbal" = 1,   # -10..10
  float "rlum" = 0.072186, "glum" = 0.715158, "blum" = 0.212656,  # FFmpeg defaults!
  bool  "alternate" = false,
  # ---- tweak layer (original; all no-ops at default) ----
  float "skin" = 0,               # 0..1 skin-tone protection strength
  float "skin_hue" = 25,          # skin hue center (degrees)
  float "skin_range" = 35,        # hue window half-width
  float "skin_sat_min" = 0.08,    # lower gate (grays are not skin)
  float "skin_sat_max" = 0.75,    # upper gate (pure red is not skin)
  float "sat_limit" = 2,          # <2: soft cap on high-saturation boost
  float "shadow_protect" = 0,     # 0..1 attenuate boost near black
  float "highlight_protect" = 0,  # 0..1 attenuate boost near white
  bool  "show" = false,           # true: show skin mask (grayscale)
  # ---- adaptive gain (original) ----
  float "auto" = 0,               # 0..1 gain adapted from frame mean saturation
  float "auto_target_sat" = 0.25, # target mean saturation
  int   "auto_smoothing" = 1,     # gain averaged over N frames (anti-pumping)
  # ---- working space ----
  int   "space" = 0,              # 0=gamma (default, FFmpeg-identical)
                                  # 1=linear light (sRGB / BT.709 LUT)
                                  # 3=Oklab (perceptual, hue-invariant)
  int   "transfer" = 0)           # for space>=1: 0=auto (_Transfer prop),
                                  #                1=sRGB, 2=BT.709 OETF
```

### Examples

```avisynth
# identical to: ffmpeg -vf vibrance=intensity=0.6
Vibrance(intensity=0.6)

# face-safe, no highlight blow-out
Vibrance(intensity=0.8, skin=0.7, sat_limit=0.9, highlight_protect=0.5)

# tune the skin mask visually
Vibrance(intensity=0.8, skin=0.7, skin_hue=30, show=true)

# footage with many scene cuts: smooth adaptive gain
Vibrance(intensity=0.4, auto=0.8, auto_smoothing=12)

# grading in linear light
Vibrance(intensity=0.5, space=1, transfer=1)

# perceptual: boost chroma without hue shift
Vibrance(intensity=0.5, space=3)

# HDR source (PQ) graded in linear light
Vibrance(intensity=0.4, space=1, transfer=3)

# film-style log grading
Vibrance(intensity=0.6, space=2)
```

### Technical notes

- FFmpeg's 5.x/6.x default `rlum/blum` values have **R/B swapped** relative
  to BT.709. FFmpeg 7+ fixed this upstream (same pixel math, new defaults).
  `legacy=true` reproduces the old behavior for existing scripts; use
  `legacy=false` to match modern FFmpeg. Explicit `rlum/glum/blum` always
  win over both. The golden test validates BOTH modes against any ffmpeg
  version (luma coefficients are passed explicitly).
- `space=0` with all tweaks at default is bit-exact against `vf_vibrance`.
- Oklab uses Bjorn Ottosson's published spec matrices (NOT the
  colour-science variant — their `XYZ_to_Oklab` uses a different
  XYZ->LMS matrix; see `gt_oklab.py`, which validates both references).
- `space=2` uses ACEScct (log grading); valid code domain is
  [0.0729, 0.5548] — SDR-range content. Codes below the black floor clamp.
- PQ/HLG (auto-detected from the `_Transfer` prop: 16=PQ, 18=HLG) are
  decoded to *relative* linear (1.0 = 10000 nits for PQ). Vibrance then
  works like any linear-light source. PQ near-black codes (<0.2, i.e.
  under ~10 nits) quantize in the uniform LUT and carry no real signal.
- YUV input is auto-converted to RGB; planar RGB 8/16/float supported.
- Target: AviSynth+ interface v12+ (r10 / 3.7.3+). MT-safe (gain state is
  mutex-protected; smoothing is deterministic per frame).

## ColorTemp

A **bit-exact** port of FFmpeg's `vf_colortemperature` (validated against
the `ffmpeg` CLI, see `gt_colortemp.py`). Shifts the white point of the
video along the Kelvin axis using the Tanner Helland approximation,
with optional lightness preservation.

### Syntax

```
ColorTemp(clip c,
  float "temperature" = 6500,   # Kelvin, 1000..40000
  float "mix" = 1,              # 0..1 blend toward the shifted white point
  float "pl" = 0)               # 0..1 preserve lightness (max+min correction)
```

### Examples

```avisynth
# warm up daylight footage to tungsten
ColorTemp(temperature=3200)

# subtle cool shift at half strength
ColorTemp(temperature=9300, mix=0.5)

# shift without changing overall brightness
ColorTemp(temperature=4500, pl=1)
```

### Technical notes

- `temperature` values below 6500 warm the image, above 6500 cool it.
- `pl=1` applies the l0/l1 lightness correction fully; `pl=0` leaves
  brightness to shift naturally (FFmpeg default).
- Values below 1000K / above 40000K are rejected, matching FFmpeg.

## Build

### Windows (MSVC or MinGW)

```
# needs the official avisynth.h (AviSynthPlus r10+, interface 12) on the include path
g++ -O2 -std=c++17 -shared -I<path-to-avisynth-include> ColorRestore.cpp -o ColorRestore.dll
# MSVC: cl /O2 /std:c++17 /LD /I<include> ColorRestore.cpp
```

### Linux (cross-check / CI)

`avisynth_stub.h` can stand in as `avisynth.h` for an offline compile
check (NOT for loading into real AviSynth+):

```
cp avisynth_stub.h avisynth.h
g++ -O2 -std=c++17 -shared -fPIC -I. ColorRestore.cpp -o ColorRestore.so
```

## Testing

```
# unit tests (30 checks):
g++ -O2 -std=c++17 test_core.cpp -o test_core && ./test_core

# golden test vs FFmpeg CLI (needs ffmpeg + numpy):
python3 golden_test.py

# golden test for ColorTemp vs FFmpeg (needs ffmpeg + numpy):
python3 gt_colortemp.py

# dual-reference Oklab golden test (needs colour-science + numpy):
pip install colour-science
python3 gt_oklab.py
```

CI (`.github/workflows/CI.yml`) runs all of the above on every push.

## Architecture

```
src/vibrance_core.h     pure algorithms, no AviSynth deps (unit-testable)
src/ColorRestore.cpp    AviSynth+ wrapper (interface v12); dispatch
                        8/16/float x space(0/1/3) x auto(0/1/smoothed)
tests/test_core.cpp     unit tests (35 checks)
tests/golden_test.py    golden test: Vibrance vs ffmpeg -vf vibrance
tests/gt_colortemp.py   golden test: ColorTemp vs ffmpeg -vf colortemperature
tests/gt_oklab.py       golden test: Oklab vs Ottosson spec + colour-science
tests/bridge*.cpp       RGB24<->planar adapters used by the golden tests
tools/avisynth_stub.h   minimal interface-v12 stub for offline compile checks
.github/workflows/CI.yml  GitHub Actions: build + all tests on Linux & Windows
```

The core works in planar RGB. Every tweak is computed as a multiplier
(`scale_all`) equal to 1.0 at default — the FFmpeg formula is never
touched, so the golden test stays valid forever.

## Roadmap

- [x] Vibrance: FFmpeg port + tweak layer + auto/smoothing + linear + Oklab
- [x] ColorTemp (port of vf_colortemperature — golden-tested)
- [x] space=2 (ACEScct log), PQ/HLG transfers
- [ ] doom9 release

## License

Code ported from FFmpeg: LGPL v2.1+ (per FFmpeg's license). Original code
(tweak layer, adaptive gain, Oklab glue): MIT. Pick either to match your
distribution needs; FFmpeg-derived files are marked in their headers.
