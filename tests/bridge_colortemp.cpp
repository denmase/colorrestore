// bridge_colortemp.cpp - RGB24 adapter for golden test vs FFmpeg vf_colortemperature
#include "vibrance_core.h"
#include <vector>
using namespace colorrestore;
extern "C" {
void cr_colortemp_rgb24(const uint8_t* in, uint8_t* out, int w, int h,
                        float temperature, float mix, float pl)
{
    const int npix = w * h;
    std::vector<uint8_t> r(npix), g(npix), b(npix), ro(npix), go(npix), bo(npix);
    for (int i = 0; i < npix; ++i) { r[i]=in[3*i]; g[i]=in[3*i+1]; b[i]=in[3*i+2]; }
    ColorTempParams p; p.temperature = temperature; p.mix = mix; p.pl = pl;
    float color[3]; kelvin2rgb(p.temperature, color);
    colortemp_frame<uint8_t>(p, color, r.data(),g.data(),b.data(), ro.data(),go.data(),bo.data(), w,h,w,w);
    for (int i = 0; i < npix; ++i) { out[3*i]=ro[i]; out[3*i+1]=go[i]; out[3*i+2]=bo[i]; }
}
}
