// bridge.cpp - packed RGB24 <-> planar adapter around vibrance_core.h.
// Used by golden_test.py to validate the core against FFmpeg's vf_vibrance.
#include "vibrance_core.h"
#include <vector>
using namespace colorrestore;
extern "C" {
void cr_vibrance_rgb24(const uint8_t* in, uint8_t* out, int w, int h,
                       float intensity, float rb, float gb, float bb, int alternate)
{
    const int npix = w * h;
    std::vector<uint8_t> r(npix), g(npix), b(npix), ro(npix), go(npix), bo(npix);
    for (int i = 0; i < npix; ++i) { r[i]=in[3*i]; g[i]=in[3*i+1]; b[i]=in[3*i+2]; }
    VibranceParams p;
    p.intensity = intensity; p.skin = 0.f;
    p.balance[0]=rb; p.balance[1]=gb; p.balance[2]=bb;
    p.alternate = alternate != 0;
    vibrance_frame<uint8_t>(p, r.data(), g.data(), b.data(), ro.data(), go.data(), bo.data(), w, h, w, w);
    for (int i = 0; i < npix; ++i) { out[3*i]=ro[i]; out[3*i+1]=go[i]; out[3*i+2]=bo[i]; }
}
}
