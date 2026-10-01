// ============================================================================
// ColorRestore.cpp -- AviSynth+ plugin: Vibrance
// Base: bit-exact port of FFmpeg vf_vibrance (validated vs ffmpeg CLI,
// see golden_test.py). Extensions: skin mask, sat_limit, shadow/highlight
// protect, show_mask, adaptive gain (auto) with temporal smoothing,
// linear-light processing (space=1) via transfer LUTs.
// Target: AviSynth+ interface version 12+ (uses props API from v10+).
// ============================================================================
#include "avisynth.h"          // real build: official header; offline test: avisynth_stub.h
#include "vibrance_core.h"
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstdint>

using namespace colorrestore;

static inline void requireRange(IScriptEnvironment* env, bool ok, const char* what) {
    if (!ok) env->ThrowError("Vibrance: %s out of range", what);
}

class VibranceFilter : public GenericVideoFilter {
    VibranceParams p;
    TransferLuts luts[2];       // [0]=sRGB, [1]=BT.709 OETF
    std::map<int, float> statsCache;
    std::mutex statsMutex;

    float meanSatOfFrame(const PVideoFrame& f) {
        const int w = vi.width, h = vi.height;
        if (vi.ComponentSize() == 1)
            return frame_mean_saturation<uint8_t>(f->GetReadPtr(PLANAR_R), f->GetReadPtr(PLANAR_G),
                                                  f->GetReadPtr(PLANAR_B), w, h, f->GetPitch(PLANAR_R));
        if (vi.ComponentSize() == 2)
            return frame_mean_saturation<uint16_t>(
                reinterpret_cast<const uint16_t*>(f->GetReadPtr(PLANAR_R)),
                reinterpret_cast<const uint16_t*>(f->GetReadPtr(PLANAR_G)),
                reinterpret_cast<const uint16_t*>(f->GetReadPtr(PLANAR_B)),
                w, h, f->GetPitch(PLANAR_R) / 2);
        return frame_mean_saturation<float>(
            reinterpret_cast<const float*>(f->GetReadPtr(PLANAR_R)),
            reinterpret_cast<const float*>(f->GetReadPtr(PLANAR_G)),
            reinterpret_cast<const float*>(f->GetReadPtr(PLANAR_B)),
            w, h, f->GetPitch(PLANAR_R) / 4);
    }

    float meanSatFor(int n, IScriptEnvironment* env) {
        {
            std::lock_guard<std::mutex> lk(statsMutex);
            auto it = statsCache.find(n);
            if (it != statsCache.end()) return it->second;
        }
        const float ms = meanSatOfFrame(child->GetFrame(n, env));
        std::lock_guard<std::mutex> lk(statsMutex);
        if (statsCache.size() > 64) statsCache.clear();  // crude bound; frames near n stay hot
        statsCache[n] = ms;
        return ms;
    }

    template <typename T>
    void run(const VibranceParams& pp, const TransferLuts* lut, int mode,
             const PVideoFrame& src, PVideoFrame& dst) {
        const int w = vi.width, h = vi.height;
        const T* sR = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_R));
        const T* sG = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_G));
        const T* sB = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_B));
        T* dR = reinterpret_cast<T*>(dst->GetWritePtr(PLANAR_R));
        T* dG = reinterpret_cast<T*>(dst->GetWritePtr(PLANAR_G));
        T* dB = reinterpret_cast<T*>(dst->GetWritePtr(PLANAR_B));
        const int sp = src->GetPitch(PLANAR_R) / int(sizeof(T));
        const int dp = dst->GetPitch(PLANAR_R) / int(sizeof(T));
        if (mode == 3)      vibrance_frame_oklab<T>(pp, *lut, sR, sG, sB, dR, dG, dB, w, h, sp, dp);
        else if (lut)       vibrance_frame_lut<T>(pp, *lut, sR, sG, sB, dR, dG, dB, w, h, sp, dp);
        else                vibrance_frame<T>(pp, sR, sG, sB, dR, dG, dB, w, h, sp, dp);
    }

public:
    VibranceFilter(PClip clip, const VibranceParams& params, IScriptEnvironment* env)
        : GenericVideoFilter(clip), p(params)
    {
        if (!vi.IsRGB() && !vi.IsYUV())
            env->ThrowError("Vibrance: unsupported color format");
        if (vi.IsYUV()) {
            AVSValue in[1] = { child };
            AVSValue conv = env->Invoke(vi.BitsPerComponent() == 8 ? "ConvertToRGB32"
                                       : "ConvertToRGB64", AVSValue(in, 1));
            child = PClip(conv.AsClip());
            vi = child->GetVideoInfo();
        }
        if (!vi.IsPlanarRGB() && !vi.IsRGB())
            env->ThrowError("Vibrance: needs RGB planar or packed RGB input");

        requireRange(env, p.intensity >= -2.f && p.intensity <= 2.f, "intensity (-2..2)");
        for (int i = 0; i < 3; ++i)
            requireRange(env, p.balance[i] >= -10.f && p.balance[i] <= 10.f, "balance (-10..10)");
        requireRange(env, p.skin >= 0.f && p.skin <= 1.f, "skin (0..1)");
        requireRange(env, p.auto_strength >= 0.f && p.auto_strength <= 1.f, "auto (0..1)");
        requireRange(env, p.space >= 0 && p.space <= 3, "space (0..3: 0=gamma 1=linear 3=oklab)");
        requireRange(env, p.transfer >= 0 && p.transfer <= 2, "transfer (0..2)");
        if (p.auto_smoothing < 1) p.auto_smoothing = 1;

        if (p.space >= 1) {
            build_transfer_luts(luts[TF_SRGB], TF_SRGB);
            build_transfer_luts(luts[TF_BT709], TF_BT709);
        }
    }

    PVideoFrame GetFrame(int n, IScriptEnvironment* env) override {
        PVideoFrame src = child->GetFrame(n, env);
        PVideoFrame dst = env->NewVideoFrameP(vi, &src);
        VibranceParams pp = p;

        // resolve linear-light LUT
        const TransferLuts* lut = nullptr;
        if (p.space >= 1) {
            int kind = TF_SRGB;
            if (p.transfer == 2) kind = TF_BT709;
            else if (p.transfer == 0) {
                int err = 1;
                const AVSMap* m = env->getFramePropsRO(src);
                const int64_t tf = m ? env->propGetInt(m, "_Transfer", 0, &err) : 0;
                if (!err && tf == 1) kind = TF_BT709;   // BT.709 OETF; sRGB (13) & unknown -> sRGB
            }
            lut = &luts[kind];
        }

        // adaptive gain (optionally temporally smoothed)
        if (pp.auto_strength > 0.0f) {
            if (pp.auto_smoothing <= 1) {
                pp.intensity *= auto_gain(meanSatOfFrame(src), pp);
            } else {
                std::vector<float> ms(pp.auto_smoothing);
                for (int k = 0; k < pp.auto_smoothing; ++k) {
                    const int fn = std::min(std::max(n - k, 0), vi.num_frames - 1);
                    ms[k] = meanSatFor(fn, env);
                }
                pp.intensity *= smoothed_gain(ms.data(), pp.auto_smoothing, pp);
            }
        }

        const int mode = p.space;
        if (vi.ComponentSize() == 1)      run<uint8_t>(pp, lut, mode, src, dst);
        else if (vi.ComponentSize() == 2) run<uint16_t>(pp, lut, mode, src, dst);
        else                              run<float>(pp, lut, mode, src, dst);
        return dst;
    }
};

AVSValue __cdecl Create_Vibrance(AVSValue args, void*, IScriptEnvironment* env) {
    VibranceParams p;
    p.intensity = args[1].AsFloatf(0.0f);
    p.balance[0] = args[2].AsFloatf(1.0f);
    p.balance[1] = args[3].AsFloatf(1.0f);
    p.balance[2] = args[4].AsFloatf(1.0f);
    p.luma[0]    = args[5].AsFloatf(0.072186f);
    p.luma[1]    = args[6].AsFloatf(0.715158f);
    p.luma[2]    = args[7].AsFloatf(0.212656f);
    p.alternate  = args[8].AsBool(false);
    p.skin       = args[9].AsFloatf(0.0f);
    p.skin_hue   = args[10].AsFloatf(25.0f);
    p.skin_range = args[11].AsFloatf(35.0f);
    p.skin_sat_min = args[12].AsFloatf(0.08f);
    p.skin_sat_max = args[13].AsFloatf(0.75f);
    p.sat_limit    = args[14].AsFloatf(2.0f);
    p.shadow_protect    = args[15].AsFloatf(0.0f);
    p.highlight_protect = args[16].AsFloatf(0.0f);
    p.show_mask    = args[17].AsBool(false);
    p.auto_strength   = args[18].AsFloatf(0.0f);
    p.auto_target_sat = args[19].AsFloatf(0.25f);
    p.space           = args[20].AsInt(0);
    p.transfer        = args[21].AsInt(0);
    p.auto_smoothing  = args[22].AsInt(1);
    return new VibranceFilter(args[0].AsClip(), p, env);
}


class ColorTempFilter : public GenericVideoFilter {
    ColorTempParams p;
    float color[3];
public:
    ColorTempFilter(PClip clip, const ColorTempParams& params, IScriptEnvironment* env)
        : GenericVideoFilter(clip), p(params)
    {
        if (!vi.IsRGB() && !vi.IsYUV())
            env->ThrowError("ColorTemp: unsupported color format");
        if (vi.IsYUV()) {
            AVSValue in[1] = { child };
            AVSValue conv = env->Invoke(vi.BitsPerComponent() == 8 ? "ConvertToRGB32"
                                       : "ConvertToRGB64", AVSValue(in, 1));
            child = PClip(conv.AsClip());
            vi = child->GetVideoInfo();
        }
        if (!vi.IsPlanarRGB() && !vi.IsRGB())
            env->ThrowError("ColorTemp: needs RGB planar or packed RGB input");
        requireRange(env, p.temperature >= 1000.f && p.temperature <= 40000.f, "temperature (1000..40000)");
        requireRange(env, p.mix >= 0.f && p.mix <= 1.f, "mix (0..1)");
        requireRange(env, p.pl >= 0.f && p.pl <= 1.f, "pl (0..1)");
        kelvin2rgb(p.temperature, color);
    }

    PVideoFrame GetFrame(int n, IScriptEnvironment* env) override {
        PVideoFrame src = child->GetFrame(n, env);
        PVideoFrame dst = env->NewVideoFrameP(vi, &src);
        const int w = vi.width, h = vi.height;
        if (vi.ComponentSize() == 1) {
            colortemp_frame<uint8_t>(p, color,
                src->GetReadPtr(PLANAR_R), src->GetReadPtr(PLANAR_G), src->GetReadPtr(PLANAR_B),
                dst->GetWritePtr(PLANAR_R), dst->GetWritePtr(PLANAR_G), dst->GetWritePtr(PLANAR_B),
                w, h, src->GetPitch(PLANAR_R), dst->GetPitch(PLANAR_R));
        } else if (vi.ComponentSize() == 2) {
            colortemp_frame<uint16_t>(p, color,
                reinterpret_cast<const uint16_t*>(src->GetReadPtr(PLANAR_R)),
                reinterpret_cast<const uint16_t*>(src->GetReadPtr(PLANAR_G)),
                reinterpret_cast<const uint16_t*>(src->GetReadPtr(PLANAR_B)),
                reinterpret_cast<uint16_t*>(dst->GetWritePtr(PLANAR_R)),
                reinterpret_cast<uint16_t*>(dst->GetWritePtr(PLANAR_G)),
                reinterpret_cast<uint16_t*>(dst->GetWritePtr(PLANAR_B)),
                w, h, src->GetPitch(PLANAR_R) / 2, dst->GetPitch(PLANAR_R) / 2);
        } else {
            colortemp_frame<float>(p, color,
                reinterpret_cast<const float*>(src->GetReadPtr(PLANAR_R)),
                reinterpret_cast<const float*>(src->GetReadPtr(PLANAR_G)),
                reinterpret_cast<const float*>(src->GetReadPtr(PLANAR_B)),
                reinterpret_cast<float*>(dst->GetWritePtr(PLANAR_R)),
                reinterpret_cast<float*>(dst->GetWritePtr(PLANAR_G)),
                reinterpret_cast<float*>(dst->GetWritePtr(PLANAR_B)),
                w, h, src->GetPitch(PLANAR_R) / 4, dst->GetPitch(PLANAR_R) / 4);
        }
        return dst;
    }
};

AVSValue __cdecl Create_ColorTemp(AVSValue args, void*, IScriptEnvironment* env) {
    ColorTempParams p;
    p.temperature = args[1].AsFloatf(6500.0f);
    p.mix = args[2].AsFloatf(1.0f);
    p.pl  = args[3].AsFloatf(0.0f);
    return new ColorTempFilter(args[0].AsClip(), p, env);
}

extern "C" __attribute__((visibility("default")))
const char* AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* linkage) {
    AVS_linkage = linkage;   // required: host passes linkage, plugins must adopt it
    env->AddFunction(
        "Vibrance",
        "c"          // clip
        "[intensity]f[rbal]f[gbal]f[bbal]f"
        "[rlum]f[glum]f[blum]f[alternate]b"
        "[skin]f[skin_hue]f[skin_range]f[skin_sat_min]f[skin_sat_max]f"
        "[sat_limit]f[shadow_protect]f[highlight_protect]f[show]b"
        "[auto]f[auto_target_sat]f[space]i[transfer]i[auto_smoothing]i",
        Create_Vibrance, nullptr);
    env->AddFunction(
        "ColorTemp",
        "c[temperature]f[mix]f[pl]f",
        Create_ColorTemp, nullptr);
    return "ColorRestore: Vibrance (FFmpeg vf_vibrance port + tweak layer + linear space + adaptive gain)";
}
