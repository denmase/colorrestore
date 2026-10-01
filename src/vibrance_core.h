// vibrance_core.h - Pure algorithm core for the AviSynth+ ColorRestore plugin.
// Base = port of FFmpeg vf_vibrance (validated bit-exact vs ffmpeg CLI,
// see golden_test.py). Tweak layer (sat_limit, shadow/highlight protect,
// skin mask, show_mask) is original work - all no-ops at default values,
// so FFmpeg parity is preserved when tweaks are untouched.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <type_traits>
#include <cfloat>

namespace colorrestore {

struct VibranceParams {
    // ---- FFmpeg vf_vibrance parameters (frozen, golden-tested) ----
    float intensity = 0.0f;                    // -2..2
    float balance[3]  = {1.0f, 1.0f, 1.0f};    // rbal/gbal/bbal
    float luma[3]     = {0.072186f, 0.715158f, 0.212656f}; // rlum/glum/blum (FFmpeg defaults)
    bool  alternate   = false;
    // ---- original tweak layer ----
    float skin        = 0.0f;                  // 0..1 skin-tone protection strength
    float skin_hue    = 25.0f;                 // skin hue center (deg)
    float skin_range  = 35.0f;                 // half-width hue window (deg)
    float skin_sat_min = 0.08f;                // below: not skin (grays)
    float skin_sat_max = 0.75f;                // above: not skin (pure red etc.)
    float sat_limit   = 2.0f;                  // >=2 disables; soft cap: no boost at/above
    float shadow_protect    = 0.0f;            // 0..1 attenuate boost in near-black
    float highlight_protect = 0.0f;            // 0..1 attenuate boost in near-white
    bool  show_mask   = false;                 // debug: output skin mask as grayscale
    // ---- original: adaptive gain (two-pass) ----
    float auto_strength = 0.0f;                // 0..1; 0 disables
    float auto_target_sat = 0.25f;             // target mean saturation (normalized 0..1)
    int   space = 0;                           // 0=gamma (as-is), 1=linear light (LUT)
    int   transfer = 0;                        // for space=1: 0=auto (_Transfer prop), 1=sRGB, 2=BT.709 OETF
    int   auto_smoothing = 1;                  // frames averaged for adaptive gain (1=off)
};

// ---- adaptive gain (shared by auto path and wrapper-side smoothing) ----
static inline float auto_gain(float mean_sat, const VibranceParams& p) {
    float g = 1.0f + p.auto_strength * (p.auto_target_sat - mean_sat)
                   / std::max(p.auto_target_sat, 0.05f);
    return std::min(std::max(g, 0.0f), 2.0f);
}

// average of per-frame gains over a window of frame stats (anti-pumping)
static inline float smoothed_gain(const float* mean_sats, int n, const VibranceParams& p) {
    if (n <= 1) return auto_gain(mean_sats[0], p);
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += auto_gain(mean_sats[i], p);
    return float(acc / n);
}
// mean of per-pixel (max-min) saturation over a frame, normalized 0..1
template <typename T>
float frame_mean_saturation(const T* R, const T* G, const T* B, int w, int h, int pitch) {
    const float scale = std::is_same<T, float>::value ? 1.0f
                      : float((std::uint64_t(1) << (sizeof(T) * 8)) - 1);
    double acc = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = R[x] / scale, g = G[x] / scale, b = B[x] / scale;
            acc += double(std::max(r, std::max(g, b)) - std::min(r, std::min(g, b)));
        }
        R += pitch; G += pitch; B += pitch;
    }
    return float(acc / double(int64_t(w) * h));
}

// Two-pass adaptive vibrance: pass 1 measures frame mean saturation,
// pass 2 applies vibrance with intensity scaled by adaptive gain.
// Deterministic per frame (function of frame content only) -> MT-safe.
template <typename T>
void vibrance_frame_auto(VibranceParams p,   // by value: we scale intensity
                         const T* srcR, const T* srcG, const T* srcB,
                         T* dstR, T* dstG, T* dstB,
                         int w, int h, int srcPitch, int dstPitch)
{
    const float mean_sat = frame_mean_saturation(srcR, srcG, srcB, w, h, srcPitch);
    p.intensity *= auto_gain(mean_sat, p);
    vibrance_frame(p, srcR, srcG, srcB, dstR, dstG, dstB, w, h, srcPitch, dstPitch);
}

static inline float ff_sign(float x) { return x >= 0.0f ? 1.0f : -1.0f; }

static inline float smoothstep(float e0, float e1, float x) {
    float t = std::min(std::max((x - e0) / (e1 - e0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}




// tweak weights from gamma RGB values; scale_all==1.0 and mask_out==0.0 when all disabled
static inline void compute_tweak_weights(const VibranceParams& p, float r, float g, float b,
                                         float luma, float sat, float& scale_all, float& mask_out)
{
    float skin_w = 1.0f, limit_w = 1.0f, protect_w = 1.0f;
    mask_out = 0.0f;
    if (p.skin > 0.0f || p.show_mask) {
        float hue = std::atan2(1.7320508f * (g - b), 2.0f * r - g - b) * 57.29578f;
        if (hue < 0.0f) hue += 360.0f;
        float d = std::fabs(hue - p.skin_hue);
        if (d > 180.0f) d = 360.0f - d;
        const float w_hue = 1.0f - smoothstep(p.skin_range * 0.5f, p.skin_range, d);
        const float w_sat = smoothstep(p.skin_sat_min, p.skin_sat_min * 2.0f, sat)
                          * (1.0f - smoothstep(p.skin_sat_max * 0.7f, p.skin_sat_max, sat));
        const float w = w_hue * w_sat;
        mask_out = w;
        if (p.skin > 0.0f) skin_w = 1.0f - p.skin * w;
    }
    if (p.sat_limit < 2.0f)
        limit_w = 1.0f - smoothstep(p.sat_limit * 0.5f, p.sat_limit, sat);
    if (p.shadow_protect > 0.0f)
        protect_w *= 1.0f - p.shadow_protect * (1.0f - smoothstep(0.0f, 0.15f, luma));
    if (p.highlight_protect > 0.0f)
        protect_w *= 1.0f - p.highlight_protect * smoothstep(0.85f, 1.0f, luma);
    scale_all = skin_w * limit_w * protect_w;
}

// Per-pixel vibrance. Input/output RGB normalized [0,1]. Not clipped (wrapper clips).
static inline void vibrance_pixel(const VibranceParams& p,
                                  float r, float g, float b,
                                  float& out_r, float& out_g, float& out_b)
{
    const float maxc = std::max(r, std::max(g, b));
    const float minc = std::min(r, std::min(g, b));
    const float sat  = maxc - minc;
    const float luma = r * p.luma[0] + g * p.luma[1] + b * p.luma[2];

    const float alt  = p.alternate ? 1.0f : -1.0f;
    const float rgb[3] = {r, g, b};
    float out[3];

    // ---- tweak weights (all 1.0 when tweaks disabled -> exact FFmpeg behavior) ----
    float scale_all, mask_out;
    compute_tweak_weights(p, r, g, b, luma, sat, scale_all, mask_out);

    for (int i = 0; i < 3; ++i) {
        const float gintensity = p.intensity * p.balance[i];
        const float sgintensity = alt * ff_sign(gintensity);
        const float coeff = 1.0f + gintensity * (1.0f - sgintensity * sat);
        const float eff = 1.0f + (coeff - 1.0f) * scale_all;
        out[i] = luma + (rgb[i] - luma) * eff;
    }
    if (p.show_mask) out[0] = out[1] = out[2] = mask_out;
    out_r = out[0]; out_g = out[1]; out_b = out[2];
}

template <typename T>
void vibrance_frame(const VibranceParams& p,
                    const T* srcR, const T* srcG, const T* srcB,
                    T* dstR, T* dstG, T* dstB,
                    int w, int h, int srcPitch, int dstPitch)
{
    if (p.intensity == 0.0f && !p.show_mask) {   // exact identity passthrough
        for (int y = 0; y < h; ++y) {
            std::copy(srcR, srcR + w, dstR);
            std::copy(srcG, srcG + w, dstG);
            std::copy(srcB, srcB + w, dstB);
            srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
            dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
        }
        return;
    }
    const float scale = std::is_same<T, float>::value ? 1.0f
                      : float((std::uint64_t(1) << (sizeof(T) * 8)) - 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = srcR[x] / scale, g = srcG[x] / scale, b = srcB[x] / scale;
            float or_, og, ob;
            vibrance_pixel(p, r, g, b, or_, og, ob);
            dstR[x] = T(std::min(std::max(or_ * scale, 0.0f), scale));  // truncation, matches FFmpeg
            dstG[x] = T(std::min(std::max(og * scale, 0.0f), scale));
            dstB[x] = T(std::min(std::max(ob * scale, 0.0f), scale));
        }
        srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
        dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
    }
}



// ---- transfer functions + LUTs for space=1 (linear light) ----
enum TransferKind { TF_SRGB = 0, TF_BT709 = 1, TF_ACEScct = 2, TF_PQ = 3, TF_HLG = 4 };
static constexpr int TF_COUNT = 5;
static constexpr int LUT_SIZE = 4097;   // uniform LUT; PQ/HLG/ACEScct have steep toes

struct TransferLuts {
    float to_linear[LUT_SIZE];
    float from_linear[LUT_SIZE];
};

static inline float srgb2lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static inline float lin2srgb(float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.f / 2.4f) - 0.055f; }
static inline float bt7092lin(float c) { return c <= 0.081f ? c / 4.5f : std::pow((c + 0.099f) / 1.099f, 1.f / 0.45f); }
static inline float lin2bt709(float l) { return l <= 0.018f ? l * 4.5f : 1.099f * std::pow(l, 0.45f) - 0.099f; }

// ACEScct (log grading space; toe handles blacks without floor issues)
static inline float lin_to_acesct(float x) { return x <= 0.0078125f ? 10.5402377416545f*x + 0.0729055341958355f : (std::log2(x) + 9.72f) / 17.52f; }
static inline float acesct_to_lin(float x) { return x <= 0.155251141552511f ? (x - 0.0729055341958355f) / 10.5402377416545f : std::pow(2.0f, x*17.52f - 9.72f); }

// SMPTE ST 2084 PQ (1.0 code = 10000 nits; we treat it as relative linear)
static inline float pq_to_linear(float v) {
    const float p = std::pow(v, 1.0f/78.84375f);
    return std::pow(std::max(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f*p), 1.0f/0.1593017578125f);
}
static inline float linear_to_pq(float l) {
    const float p = std::pow(std::max(l, 0.0f), 0.1593017578125f);
    return std::pow((0.8359375f + 18.8515625f*p) / (1.0f + 18.6875f*p), 78.84375f);
}

// ARIB STD-B67 HLG (scene-referred OETF)
static inline float hlg_to_linear(float e) {
    const float a=0.17883277f, b=0.28466892f, c=0.55991073f;
    return e <= 0.5f ? e*e/3.0f : (std::exp((e - c)/a) + b) / 12.0f;
}
static inline float linear_to_hlg(float l) {
    const float a=0.17883277f, b=0.28466892f, c=0.55991073f;
    return l <= 1.0f/12.0f ? std::sqrt(3.0f*std::max(l, 0.0f)) : a*std::log(12.0f*l - b) + c;
}

inline void build_transfer_luts(TransferLuts& L, int kind) {
    for (int i = 0; i < LUT_SIZE; ++i) {
        const float c = float(i) / (LUT_SIZE - 1);
        switch (kind) {
            case TF_BT709:  L.to_linear[i] = bt7092lin(c);   L.from_linear[i] = lin2bt709(c);   break;
            case TF_ACEScct:L.to_linear[i] = std::max(acesct_to_lin(c), 0.0f);L.from_linear[i] = lin_to_acesct(c);break;
            case TF_PQ:     L.to_linear[i] = pq_to_linear(c); L.from_linear[i] = linear_to_pq(c); break;
            case TF_HLG:    L.to_linear[i] = hlg_to_linear(c);L.from_linear[i] = linear_to_hlg(c);break;
            default:        L.to_linear[i] = srgb2lin(c);    L.from_linear[i] = lin2srgb(c);     break;
        }
    }
}

static inline float lut_lookup(const float* lut, float c) {
    c = std::min(std::max(c, 0.0f), 1.0f);
    const float pos = c * (LUT_SIZE - 1);
    const int   i   = int(pos);
    const float f   = pos - float(i);
    const int   i2  = std::min(i + 1, LUT_SIZE - 1);
    return lut[i] * (1.0f - f) + lut[i2] * f;
}

// vibrance in linear light: gamma->linear (LUT), vibrance, linear->gamma (LUT)
template <typename T>
void vibrance_frame_lut(const VibranceParams& p, const TransferLuts& L,
                        const T* srcR, const T* srcG, const T* srcB,
                        T* dstR, T* dstG, T* dstB,
                        int w, int h, int srcPitch, int dstPitch)
{
    if (p.intensity == 0.0f && !p.show_mask) {   // exact identity passthrough (skip LUT roundtrip)
        for (int y = 0; y < h; ++y) {
            std::copy(srcR, srcR + w, dstR);
            std::copy(srcG, srcG + w, dstG);
            std::copy(srcB, srcB + w, dstB);
            srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
            dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
        }
        return;
    }
    const float scale = std::is_same<T, float>::value ? 1.0f
                      : float((std::uint64_t(1) << (sizeof(T) * 8)) - 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = lut_lookup(L.to_linear, srcR[x] / scale);
            float g = lut_lookup(L.to_linear, srcG[x] / scale);
            float b = lut_lookup(L.to_linear, srcB[x] / scale);
            float or_, og, ob;
            vibrance_pixel(p, r, g, b, or_, og, ob);
            dstR[x] = T(std::min(std::max(lut_lookup(L.from_linear, or_) * scale, 0.0f), scale));
            dstG[x] = T(std::min(std::max(lut_lookup(L.from_linear, og) * scale, 0.0f), scale));
            dstB[x] = T(std::min(std::max(lut_lookup(L.from_linear, ob) * scale, 0.0f), scale));
        }
        srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
        dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
    }
}


// ============================================================================
// Oklab (space=3) -- Bjorn Ottosson's perceptual color space.
// Chroma is scaled around L, so hue is structurally invariant; the tweak
// weights are computed from the original gamma RGB (consistent semantics
// with space=0/1). 'balance' collapses to its mean (no RGB channels here).
// ============================================================================

static inline float cbrt_signed(float x) { return x >= 0.f ? std::cbrt(x) : -std::cbrt(-x); }
// generic matrix-parameterized transforms; spec wrappers below use Ottosson
// constants, tests may inject other references (e.g. colour-science LMS)
static inline void linrgb_to_oklab_mtx(const float M1[9], const float M2[9],
                                       float r, float g, float b,
                                       float& L, float& a, float& bb) {
    float l = M1[0]*r + M1[1]*g + M1[2]*b;
    float m = M1[3]*r + M1[4]*g + M1[5]*b;
    float s = M1[6]*r + M1[7]*g + M1[8]*b;
    l = cbrt_signed(l); m = cbrt_signed(m); s = cbrt_signed(s);
    L  = M2[0]*l + M2[1]*m + M2[2]*s;
    a  = M2[3]*l + M2[4]*m + M2[5]*s;
    bb = M2[6]*l + M2[7]*m + M2[8]*s;
}
static inline void oklab_to_linrgb_mtx(const float M2i[9], const float M1i[9],
                                       float L, float a, float b,
                                       float& r, float& g, float& bb) {
    float l = M2i[0]*L + M2i[1]*a + M2i[2]*b;
    float m = M2i[3]*L + M2i[4]*a + M2i[5]*b;
    float s = M2i[6]*L + M2i[7]*a + M2i[8]*b;
    l = l*l*l; m = m*m*m; s = s*s*s;
    r  = M1i[0]*l + M1i[1]*m + M1i[2]*s;
    g  = M1i[3]*l + M1i[4]*m + M1i[5]*s;
    bb = M1i[6]*l + M1i[7]*m + M1i[8]*s;
}



static const float OKLAB_M1[9] = {
    0.4122214708f, 0.5363325363f, 0.0514459929f,
    0.2119034982f, 0.6806995451f, 0.1073969566f,
    0.0883024619f, 0.2817188376f, 0.6299787005f };
static const float OKLAB_M2[9] = {
    0.2104542553f,  0.7936177850f, -0.0040720468f,
    1.9779984951f, -2.4285922050f,  0.4505937099f,
    0.0259040371f,  0.7827717662f, -0.8086757660f };
static const float OKLAB_M2I[9] = {
    1.0f,  0.3963377774f,  0.2158037573f,
    1.0f, -0.1055613458f, -0.0638541728f,
    1.0f, -0.0894841775f, -1.2914855480f };
static const float OKLAB_M1I[9] = {
     4.0767416613f, -3.3077115904f,  0.2309699287f,
    -1.2684380041f,  2.6097574007f, -0.3413193963f,
    -0.0041960865f, -0.7034186145f,  1.7076147009f };

static inline void linrgb_to_oklab(float r, float g, float b, float& L, float& a, float& bb) {
    linrgb_to_oklab_mtx(OKLAB_M1, OKLAB_M2, r, g, b, L, a, bb);
}

static inline void oklab_to_linrgb(float L, float a, float b, float& r, float& g, float& bb) {
    oklab_to_linrgb_mtx(OKLAB_M2I, OKLAB_M1I, L, a, b, r, g, bb);
}

// vibrance in Oklab: scale (a,b) around L with perceptual falloff.
// r,g,b = original gamma RGB (for tweak weights). L,a,bb = Oklab of linear input.
static inline void vibrance_pixel_oklab(const VibranceParams& p,
                                        float r, float g, float b,
                                        float L, float a, float bb,
                                        float& oL, float& oa, float& ob)
{
    const float luma = r * p.luma[0] + g * p.luma[1] + b * p.luma[2];
    const float sat_rgb = std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
    float scale_all, mask_out;
    compute_tweak_weights(p, r, g, b, luma, sat_rgb, scale_all, mask_out);
    const float chroma = std::sqrt(a*a + bb*bb);
    const float satn   = std::min(chroma / 0.35f, 1.0f);      // normalize to practical max chroma
    const float bal_mean = (p.balance[0] + p.balance[1] + p.balance[2]) / 3.0f;
    const float falloff  = p.alternate ? satn : (1.0f - satn); // classic vs inverse
    const float cscale   = 1.0f + p.intensity * bal_mean * falloff * scale_all;
    oL = L; oa = a * cscale; ob = bb * cscale;
    if (p.show_mask) oL = oa = ob = mask_out;
}

// frame path: gamma RGB -> linear (LUT) -> Oklab -> vibrance -> inverse
template <typename T>
void vibrance_frame_oklab(const VibranceParams& p, const TransferLuts& L_in,
                          const T* srcR, const T* srcG, const T* srcB,
                          T* dstR, T* dstG, T* dstB,
                          int w, int h, int srcPitch, int dstPitch)
{
    if (p.intensity == 0.0f && !p.show_mask) {
        for (int y = 0; y < h; ++y) {
            std::copy(srcR, srcR + w, dstR);
            std::copy(srcG, srcG + w, dstG);
            std::copy(srcB, srcB + w, dstB);
            srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
            dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
        }
        return;
    }
    const float scale = std::is_same<T, float>::value ? 1.0f
                      : float((std::uint64_t(1) << (sizeof(T) * 8)) - 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = lut_lookup(L_in.to_linear, srcR[x] / scale);
            float g = lut_lookup(L_in.to_linear, srcG[x] / scale);
            float b = lut_lookup(L_in.to_linear, srcB[x] / scale);
            float L, a, bb;
            linrgb_to_oklab(r, g, b, L, a, bb);
            float oL, oa, ob;
            vibrance_pixel_oklab(p, srcR[x]/scale, srcG[x]/scale, srcB[x]/scale, L, a, bb, oL, oa, ob);
            oklab_to_linrgb(oL, oa, ob, r, g, b);
            dstR[x] = T(std::min(std::max(lut_lookup(L_in.from_linear, r) * scale, 0.0f), scale));
            dstG[x] = T(std::min(std::max(lut_lookup(L_in.from_linear, g) * scale, 0.0f), scale));
            dstB[x] = T(std::min(std::max(lut_lookup(L_in.from_linear, b) * scale, 0.0f), scale));
        }
        srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
        dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
    }
}


// ============================================================================
// ColorTemp -- port of FFmpeg vf_colortemperature (golden-tested vs ffmpeg
// CLI, see gt_colortemp.py). All multiplicative math is scale-invariant, so
// the core works in normalized [0,1] while FFmpeg works in raw sample values;
// results are identical up to truncation.
// ============================================================================
struct ColorTempParams {
    float temperature = 6500.0f;   // Kelvin, FFmpeg range 1000..40000
    float mix = 1.0f;              // 0..1
    float pl = 0.0f;               // preserve lightness, 0..1
};

static inline float saturate01(float x) { return std::min(std::max(x, 0.0f), 1.0f); }

// FFmpeg kelvin2rgb (Tanner Helland approximation)
static inline void kelvin2rgb(float k, float rgb[3]) {
    const float kelvin = k / 100.0f;
    if (kelvin <= 66.0f) {
        rgb[0] = 1.0f;
        rgb[1] = saturate01(0.39008157876901960784f * std::log(kelvin) - 0.63184144378862745098f);
    } else {
        const float t = std::max(kelvin - 60.0f, 0.0f);
        rgb[0] = saturate01(1.29293618606274509804f * std::pow(t, -0.1332047592f));
        rgb[1] = saturate01(1.12989086089529411765f * std::pow(t, -0.0755148492f));
    }
    if (kelvin >= 66.0f)      rgb[2] = 1.0f;
    else if (kelvin <= 19.0f) rgb[2] = 0.0f;
    else                      rgb[2] = saturate01(0.54320678911019607843f * std::log(kelvin - 10.0f) - 1.19625408914f);
}

// per-pixel color temperature, normalized [0,1] RGB in/out
static inline void colortemp_pixel(const float color[3], float mix, float pl,
                                   float r, float g, float b,
                                   float& out_r, float& out_g, float& out_b)
{
    float nr = r * color[0];
    float ng = g * color[1];
    float nb = b * color[2];
    nr = r + (nr - r) * mix;
    ng = g + (ng - g) * mix;
    nb = b + (nb - b) * mix;
    const float l0 = (std::max(r, std::max(g, b)) + std::min(r, std::min(g, b))) + FLT_EPSILON;
    const float l1 = (std::max(nr, std::max(ng, nb)) + std::min(nr, std::min(ng, nb))) + FLT_EPSILON;
    const float l = l0 / l1;
    out_r = nr + (nr * l - nr) * pl;
    out_g = ng + (ng * l - ng) * pl;
    out_b = nb + (nb * l - nb) * pl;
}

template <typename T>
void colortemp_frame(const ColorTempParams& p, const float color[3],
                     const T* srcR, const T* srcG, const T* srcB,
                     T* dstR, T* dstG, T* dstB,
                     int w, int h, int srcPitch, int dstPitch)
{
    if (p.mix == 0.0f) {   // identity passthrough
        for (int y = 0; y < h; ++y) {
            std::copy(srcR, srcR + w, dstR);
            std::copy(srcG, srcG + w, dstG);
            std::copy(srcB, srcB + w, dstB);
            srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
            dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
        }
        return;
    }
    const float scale = std::is_same<T, float>::value ? 1.0f
                      : float((std::uint64_t(1) << (sizeof(T) * 8)) - 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = srcR[x] / scale, g = srcG[x] / scale, b = srcB[x] / scale;
            float or_, og, ob;
            colortemp_pixel(color, p.mix, p.pl, r, g, b, or_, og, ob);
            dstR[x] = T(std::min(std::max(or_ * scale, 0.0f), scale));  // truncation, matches FFmpeg
            dstG[x] = T(std::min(std::max(og * scale, 0.0f), scale));
            dstB[x] = T(std::min(std::max(ob * scale, 0.0f), scale));
        }
        srcR += srcPitch; srcG += srcPitch; srcB += srcPitch;
        dstR += dstPitch; dstG += dstPitch; dstB += dstPitch;
    }
}

} // namespace colorrestore
