
#include "vibrance_core.h"
extern "C" {
void cr_linrgb_to_oklab(float r, float g, float b, float* L, float* a, float* bb) {
    colorrestore::linrgb_to_oklab(r, g, b, *L, *a, *bb);
}
void cr_oklab_to_linrgb(float L, float a, float b, float* r, float* g, float* bb) {
    colorrestore::oklab_to_linrgb(L, a, b, *r, *g, *bb);
}
void cr_linrgb_to_oklab_mtx(const float* M1, const float* M2, float r, float g, float b,
                            float* L, float* a, float* bb) {
    colorrestore::linrgb_to_oklab_mtx(M1, M2, r, g, b, *L, *a, *bb);
}
void cr_oklab_to_linrgb_mtx(const float* M2i, const float* M1i, float L, float a, float b,
                            float* r, float* g, float* bb) {
    colorrestore::oklab_to_linrgb_mtx(M2i, M1i, L, a, b, *r, *g, *bb);
}
}
