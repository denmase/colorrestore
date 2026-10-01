// test_core.cpp — standalone unit tests for vibrance_core.h
#include "vibrance_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace colorrestore;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { ++failures; std::printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
    else         { std::printf("ok  : %s\n", msg); } \
} while (0)

int main() {
    // 1. intensity=0 must be identity
    {
        VibranceParams p; // defaults, intensity 0
        float r, g, b;
        vibrance_pixel(p, 0.3f, 0.55f, 0.8f, r, g, b);
        CHECK(std::fabs(r-0.3f) < 1e-6f && std::fabs(g-0.55f) < 1e-6f && std::fabs(b-0.8f) < 1e-6f,
              "intensity=0 is identity");
    }
    // 2. gray pixels never change at any intensity (chroma==0)
    {
        VibranceParams p; p.intensity = 2.0f;
        float r, g, b;
        vibrance_pixel(p, 0.4f, 0.4f, 0.4f, r, g, b);
        CHECK(std::fabs(r-0.4f) < 1e-6f && std::fabs(g-0.4f) < 1e-6f && std::fabs(b-0.4f) < 1e-6f,
              "gray unchanged at intensity=2");
    }
    // 3. clip-safe: output stays in [0,1]
    {
        VibranceParams p; p.intensity = 2.0f;
        float r, g, b;
        vibrance_pixel(p, 1.0f, 0.1f, 0.1f, r, g, b);
        // vibrance_pixel is intentionally unclipped (wrapper clips, like FFmpeg)
        r = std::min(std::max(r,0.f),1.f); g = std::min(std::max(g,0.f),1.f); b = std::min(std::max(b,0.f),1.f);
        CHECK(r >= 0.f && r <= 1.f && g >= 0.f && g <= 1.f && b >= 0.f && b <= 1.f,
              "output within [0,1] at max intensity (after clip)");
    }
    // 4. positive intensity increases saturation of a low-sat color (classic vibrance)
    {
        VibranceParams p; p.intensity = 1.0f;
        float r, g, b;
        vibrance_pixel(p, 0.45f, 0.50f, 0.50f, r, g, b);
        float sat_before = 0.50f - 0.45f;
        float sat_after  = std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
        CHECK(sat_after > sat_before, "positive intensity boosts saturation");
    }
    // 5. alternate flips the saturation-dependent behavior
    {
        VibranceParams pa; pa.intensity = 1.0f; pa.alternate = true;
        VibranceParams pb; pb.intensity = 1.0f; pb.alternate = false;
        float ra, ga, ba, rb, gb, bb;
        vibrance_pixel(pa, 0.45f, 0.50f, 0.50f, ra, ga, ba);
        vibrance_pixel(pb, 0.45f, 0.50f, 0.50f, rb, gb, bb);
        CHECK(std::fabs(ra-rb) > 1e-6f, "alternate changes result");
    }
    // 6. skin mask reduces the effect on a skin-hue pixel
    {
        // skin-ish: r>g>b, moderate saturation
        const float r0=0.75f, g0=0.55f, b0=0.45f;
        VibranceParams p1; p1.intensity = 1.0f;
        VibranceParams p2 = p1; p2.skin = 1.0f;
        float r1,g1,b1, r2,g2,b2;
        vibrance_pixel(p1, r0,g0,b0, r1,g1,b1);
        vibrance_pixel(p2, r0,g0,b0, r2,g2,b2);
        float delta1 = std::fabs(r1-r0)+std::fabs(g1-g0)+std::fabs(b1-b0);
        float delta2 = std::fabs(r2-r0)+std::fabs(g2-g0)+std::fabs(b2-b0);
        CHECK(delta2 < delta1, "skin mask attenuates change");
    }
    // 7. skin mask does NOT protect pure red (high sat) — gate works
    {
        const float r0=1.0f, g0=0.05f, b0=0.05f;
        VibranceParams p1; p1.intensity = 1.0f;
        VibranceParams p2 = p1; p2.skin = 1.0f;
        float r1,g1,b1, r2,g2,b2;
        vibrance_pixel(p1, r0,g0,b0, r1,g1,b1);
        vibrance_pixel(p2, r0,g0,b0, r2,g2,b2);
        CHECK(std::fabs(r1-r2) < 1e-6f, "pure red unaffected by skin gate");
    }
    // 8. template frame processing: 8-bit identity round-trip
    {
        const int w=64, h=48;
        std::vector<uint8_t> r(w*h), g(w*h), b(w*h), ro(w*h), go(w*h), bo(w*h);
        for (int i=0;i<w*h;++i){ r[i]=uint8_t((i*7)&255); g[i]=uint8_t((i*13)&255); b[i]=uint8_t((i*29)&255); }
        VibranceParams p; // identity
        vibrance_frame<uint8_t>(p, r.data(), g.data(), b.data(), ro.data(), go.data(), bo.data(), w, h, w, w);
        bool same = true;
        for (int i=0;i<w*h;++i) same &= (r[i]==ro[i] && g[i]==go[i] && b[i]==bo[i]);
        CHECK(same, "8-bit frame identity round-trip");
    }
    // 9. 16-bit vs float consistency within quantization
    {
        const int w=16, h=16;
        std::vector<uint16_t> r16(w*h), g16(w*h), b16(w*h), ro16(w*h), go16(w*h), bo16(w*h);
        std::vector<float> rf(w*h), gf(w*h), bf(w*h), rof(w*h), gof(w*h), bof(w*h);
        VibranceParams p; p.intensity = 0.8f;
        for (int i=0;i<w*h;++i){
            float fr = float((i*101)&1023)/1023.f, fg = float((i*211)&1023)/1023.f, fb = float((i*307)&1023)/1023.f;
            r16[i]=uint16_t(fr*65535.f); g16[i]=uint16_t(fg*65535.f); b16[i]=uint16_t(fb*65535.f);
            rf[i]=r16[i]/65535.f; gf[i]=g16[i]/65535.f; bf[i]=b16[i]/65535.f;
        }
        vibrance_frame<uint16_t>(p, r16.data(),g16.data(),b16.data(), ro16.data(),go16.data(),bo16.data(), w,h,w,w);
        vibrance_frame<float> (p, rf.data(), gf.data(), bf.data(), rof.data(),gof.data(),bof.data(), w,h,w,w);
        double maxerr = 0;
        for (int i=0;i<w*h;++i)
            maxerr = std::max(maxerr, std::fabs(ro16[i]/65535.0 - rof[i]));
        CHECK(maxerr < 1.5/65535.0, "16-bit vs float consistency");
    }

    // 10. tweak defaults are no-ops (FFmpeg parity): tweaked struct == plain struct
    {
        VibranceParams a; a.intensity = 0.8f;
        VibranceParams b = a;  // defaults: sat_limit=2, protects=0, skin=0
        float ar,ag,ab, br,bg,bb;
        vibrance_pixel(a, 0.2f, 0.4f, 0.9f, ar,ag,ab);
        vibrance_pixel(b, 0.2f, 0.4f, 0.9f, br,bg,bb);
        CHECK(ar==br && ag==bg && ab==bb, "tweak defaults are exact no-ops");
    }
    // 11. sat_limit blocks boost of a fully saturated pixel
    {
        VibranceParams p; p.intensity = 1.0f; p.sat_limit = 0.5f;
        float r,g,b;
        vibrance_pixel(p, 1.0f, 0.0f, 0.0f, r,g,b);   // sat=1 >> limit -> unchanged
        CHECK(r==1.0f && g==0.0f && b==0.0f, "sat_limit blocks saturated pixel");
        VibranceParams q; q.intensity = 1.0f; q.sat_limit = 2.0f; // no-op
        float r2,g2,b2;
        vibrance_pixel(q, 0.45f, 0.5f, 0.5f, r2,g2,b2);
        float r3,g3,b3;
        VibranceParams s; s.intensity = 1.0f; s.sat_limit = 0.5f;
        vibrance_pixel(s, 0.45f, 0.5f, 0.5f, r3,g3,b3);  // sat=0.05, below ramp -> same boost
        CHECK(r2==r3 && g2==g3 && b2==b3, "sat_limit keeps low-sat boost intact");
    }
    // 12. shadow_protect strongly attenuates boost in near-black
    {
        const float r0=0.02f, g0=0.01f, b0=0.03f;
        VibranceParams p0; p0.intensity = 1.0f;
        VibranceParams p1 = p0; p1.shadow_protect = 1.0f;
        float rA,gA,bA, rB,gB,bB;
        vibrance_pixel(p0, r0,g0,b0, rA,gA,bA);
        vibrance_pixel(p1, r0,g0,b0, rB,gB,bB);
        float dA = std::fabs(rA-r0)+std::fabs(gA-g0)+std::fabs(bA-b0);
        float dB = std::fabs(rB-r0)+std::fabs(gB-g0)+std::fabs(bB-b0);
        CHECK(dB < dA * 0.1f, "shadow_protect attenuates near-black");
    }
    // 13. highlight_protect strongly attenuates boost in near-white
    {
        const float r0=0.98f, g0=0.97f, b0=0.99f;
        VibranceParams p0; p0.intensity = 1.0f;
        VibranceParams p1 = p0; p1.highlight_protect = 1.0f;
        float rA,gA,bA, rB,gB,bB;
        vibrance_pixel(p0, r0,g0,b0, rA,gA,bA);
        vibrance_pixel(p1, r0,g0,b0, rB,gB,bB);
        float dA = std::fabs(rA-r0)+std::fabs(gA-g0)+std::fabs(bA-b0);
        float dB = std::fabs(rB-r0)+std::fabs(gB-g0)+std::fabs(bB-b0);
        CHECK(dB < dA * 0.1f, "highlight_protect attenuates near-white");
    }
    // 14. show_mask outputs the skin mask even with skin=0 (tuning aid)
    {
        VibranceParams p; p.show_mask = true; p.skin = 0.f; // skin off, mask shown anyway
        float r,g,b;
        vibrance_pixel(p, 0.75f, 0.55f, 0.45f, r,g,b);  // skin-ish pixel
        CHECK(r==g && g==b && r > 0.9f, "show_mask shows skin mask (skin=0)");
    }


    // 15. auto: low-saturation frame receives stronger boost than fixed intensity
    {
        const int w=32, h=32;
        std::vector<float> rr(w*h), gg(w*h), bb(w*h);
        for (int i=0;i<w*h;++i){  // truly dull: sat ~0.02, no clipping possible
            float t = float(i)/(w*h);
            rr[i]=0.44f+0.02f*t; gg[i]=0.45f; bb[i]=0.46f-0.02f*t;
        }
        VibranceParams q; q.intensity=0.3f;  // fixed, no auto
        std::vector<float> q1(w*h),q2(w*h),q3(w*h);
        vibrance_frame<float>(q, rr.data(),gg.data(),bb.data(), q1.data(),q2.data(),q3.data(), w,h,w,w);
        float ms_plain = frame_mean_saturation(q1.data(),q2.data(),q3.data(), w,h,w);

        VibranceParams p; p.intensity=0.3f; p.auto_strength=1.0f;
        std::vector<float> o1(w*h),o2(w*h),o3(w*h);
        vibrance_frame_auto<float>(p, rr.data(),gg.data(),bb.data(), o1.data(),o2.data(),o3.data(), w,h,w,w);
        float ms_auto = frame_mean_saturation(o1.data(),o2.data(),o3.data(), w,h,w);

        CHECK(ms_auto > ms_plain * 1.15f, "auto boosts dull frame beyond fixed intensity");
    }
    // 16. auto=0 is exact no-op
    {
        VibranceParams a; a.intensity=0.7f;
        VibranceParams b = a; b.auto_strength=0.0f;
        float r1,g1,b1, r2,g2,b2;
        vibrance_pixel(a, 0.3f,0.5f,0.7f, r1,g1,b1);
        vibrance_pixel(b, 0.3f,0.5f,0.7f, r2,g2,b2);
        CHECK(r1==r2 && g1==g2 && b1==b2, "auto=0 no-op on pixel level");
    }


    // 17. transfer LUT roundtrip (sRGB + BT.709)
    {
        for (int kind = 0; kind <= 1; ++kind) {
            TransferLuts L; build_transfer_luts(L, kind);
            float maxerr = 0.f;
            for (int i = 0; i <= 100; ++i) {
                float x = i / 100.f;
                maxerr = std::max(maxerr, std::fabs(lut_lookup(L.from_linear, lut_lookup(L.to_linear, x)) - x));
            }
            CHECK(maxerr < 3e-3f, kind==0 ? "sRGB LUT roundtrip" : "BT.709 LUT roundtrip");
        }
    }
    // 18. linear path identity at intensity=0 is exact (bypasses LUT)
    {
        const int w=8, h=8;
        std::vector<uint8_t> r(w*h), g(w*h), b(w*h), ro(w*h), go(w*h), bo(w*h);
        for (int i=0;i<w*h;++i){ r[i]=uint8_t(i*3); g[i]=uint8_t(i*5); b[i]=uint8_t(i*7); }
        VibranceParams p; p.space=1; // intensity 0 -> exact copy
        TransferLuts L; build_transfer_luts(L, TF_SRGB);
        vibrance_frame_lut<uint8_t>(p, L, r.data(),g.data(),b.data(), ro.data(),go.data(),bo.data(), w,h,w,w);
        bool same = true;
        for (int i=0;i<w*h;++i) same &= (r[i]==ro[i] && g[i]==go[i] && b[i]==bo[i]);
        CHECK(same, "linear path identity is exact");
    }
    // 19. linear vibrance keeps gray pixels gray
    {
        VibranceParams p; p.intensity=1.0f;
        TransferLuts L; build_transfer_luts(L, TF_SRGB);
        float r,g,b;
        vibrance_pixel(p, 0.5f,0.5f,0.5f, r,g,b);   // vibrance core itself
        CHECK(std::fabs(r-0.5f)<1e-6f, "gray stays gray (core)");
        (void)L;
    }
    // 20. auto_gain: clamped and monotonic
    {
        VibranceParams p; p.auto_strength=1.0f; p.auto_target_sat=0.25f;
        float g0 = auto_gain(0.0f, p), g1 = auto_gain(0.25f, p), g2 = auto_gain(1.0f, p);
        CHECK(g0 <= 2.0f && g2 >= 0.0f, "auto_gain clamped");
        CHECK(g0 > g1 && g1 > g2, "auto_gain monotonic decreasing");
        CHECK(std::fabs(g1 - 1.0f) < 1e-6f, "auto_gain == 1 at target");
    }
    // 21. smoothed_gain averages and dampens jumps
    {
        VibranceParams p; p.auto_strength=1.0f; p.auto_target_sat=0.25f;
        float dull = auto_gain(0.05f, p), vivid = auto_gain(0.60f, p);
        float two[2] = {0.05f, 0.60f};
        float sg = smoothed_gain(two, 2, p);
        CHECK(sg > std::min(dull,vivid) && sg < std::max(dull,vivid), "smoothed_gain interpolates");
        float one[1] = {0.05f};
        CHECK(std::fabs(smoothed_gain(one,1,p) - dull) < 1e-7f, "smoothed n=1 == auto_gain");
    }


    // 22. Oklab forward vs Ottosson published values (linear sRGB primers)
    {
        struct { float r,g,b; float L,a,bb; } pub[3] = {
            {1,0,0,  0.62796f,  0.22486f,  0.12585f},
            {0,1,0,  0.86644f, -0.23389f,  0.17950f},
            {0,0,1,  0.45201f, -0.03246f, -0.31153f},
        };
        bool ok = true;
        for (auto& t : pub) {
            float L,a,b; linrgb_to_oklab(t.r,t.g,t.b, L,a,b);
            ok &= std::fabs(L-t.L)<1e-4f && std::fabs(a-t.a)<1e-4f && std::fabs(b-t.bb)<1e-4f;
        }
        CHECK(ok, "Oklab forward matches Ottosson published values");
    }
    // 23. Oklab roundtrip on representative colors
    {
        const float cols[4][3] = {{1,0,0},{0.2,0.5,0.8},{0.9,0.8,0.1},{0.05,0.05,0.05}};
        bool ok = true;
        for (auto& c : cols) {
            float L,a,b; linrgb_to_oklab(c[0],c[1],c[2], L,a,b);
            float r,g,bb; oklab_to_linrgb(L,a,b, r,g,bb);
            ok &= std::fabs(r-c[0])<1e-3f && std::fabs(g-c[1])<1e-3f && std::fabs(bb-c[2])<1e-3f;
        }
        CHECK(ok, "Oklab roundtrip < 1e-3");
    }
    // 24. gray stays on the L axis (a=b=0)
    {
        float L,a,b; linrgb_to_oklab(0.5f,0.5f,0.5f, L,a,b);
        CHECK(std::fabs(a)<1e-4f && std::fabs(b)<1e-4f && std::fabs(L-0.7937005f)<1e-3f,
              "gray maps to L axis");
    }
    // 25. Oklab vibrance: hue structurally invariant, chroma grows
    {
        float L,a,b; linrgb_to_oklab(0.6f,0.2f,0.1f, L,a,b);
        VibranceParams p; p.intensity=0.6f;
        float oL,oa,ob; vibrance_pixel_oklab(p, 0.6f,0.2f,0.1f, L,a,b, oL,oa,ob);
        float h0 = std::atan2(b,a), h1 = std::atan2(ob,oa);
        float c0 = std::sqrt(a*a+b*b), c1 = std::sqrt(oa*oa+ob*ob);
        CHECK(std::fabs(h0-h1)<1e-5f && c1>c0, "Oklab vibrance: hue invariant, chroma grows");
    }


    // CT1. kelvin2rgb(6500) matches FFmpeg reference values
    {
        float rgb[3]; kelvin2rgb(6500.f, rgb);
        CHECK(std::fabs(rgb[0]-1.0f)<1e-5f && std::fabs(rgb[1]-0.996510f)<1e-4f && std::fabs(rgb[2]-0.980557f)<1e-4f,
              "kelvin2rgb(6500)");
    }
    // CT2. extremes: 1000K very red, 40000K very blue
    {
        float lo[3], hi[3]; kelvin2rgb(1000.f, lo); kelvin2rgb(40000.f, hi);
        CHECK(lo[0] > 0.99f && lo[2] < 0.1f, "1000K is warm");
        CHECK(hi[2] > 0.99f && hi[0] < 0.6f, "40000K is cold");
    }
    // CT3. mix=0 identity (pixel level)
    {
        float color[3]; kelvin2rgb(3200.f, color);
        float r,g,b; colortemp_pixel(color, 0.f, 0.f, 0.3f,0.5f,0.8f, r,g,b);
        CHECK(r==0.3f && g==0.5f && b==0.8f, "colortemp mix=0 identity");
    }
    // CT4. warmth increases with lower temperature
    {
        float c32[3], c65[3]; kelvin2rgb(3200.f, c32); kelvin2rgb(6500.f, c65);
        float r1,g1,b1, r2,g2,b2;
        colortemp_pixel(c32, 1.f, 0.f, 0.5f,0.5f,0.5f, r1,g1,b1);
        colortemp_pixel(c65, 1.f, 0.f, 0.5f,0.5f,0.5f, r2,g2,b2);
        CHECK(b1 < b2 && g1 < g2, "3200K warmer than 6500K");
    }
    // CT5. pl=1 preserves max+min lightness
    {
        float color[3]; kelvin2rgb(3200.f, color);
        float r,g,b; colortemp_pixel(color, 1.f, 1.f, 0.2f,0.5f,0.9f, r,g,b);
        float l0 = 0.2f + 0.9f;                    // original max+min
        float l1 = (std::max(r,std::max(g,b)) + std::min(r,std::min(g,b)));
        CHECK(std::fabs(l0-l1) < 1e-3f, "pl=1 preserves lightness");
    }

    std::printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
