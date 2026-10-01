// ============================================================================
// avisynth.h  --  MINIMAL STUB for offline build-testing (AviSynth+ API,
// interface version 12 shape). NOT the official header. Replicates only the
// subset of the AviSynth+ C++ API that ColorRestore.cpp uses, so the wrapper
// compiles here. For real builds, use the official avisynth.h from
// AviSynthPlus (r10+/3.7.3+, interface version 12) -- no wrapper changes
// should be needed.
// ============================================================================
#pragma once
#include <cstdint>

#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif

#define AVISYNTH_INTERFACE_VERSION 12
#define MT_NICE_FILTER 1

enum PlanePlane {
    PLANAR_Y = 0, PLANAR_U = 1, PLANAR_V = 2,
    PLANAR_G = PLANAR_Y, PLANAR_B = PLANAR_U, PLANAR_R = PLANAR_V,
    PLANAR_A = 3
};

struct VideoInfo {
    int width = 0, height = 0;
    unsigned fps_numerator = 0, fps_denominator = 0;
    int num_frames = 0;
    int pixel_type = 0;
    int audio_samples_per_second = 0;
    int sample_type = 0;
    int64_t num_audio_samples = 0;
    int nchannels = 0;
    int image_type = 0;

    bool IsRGB()       const { return (pixel_type & 0x7F000000) == 0x40000000
                                    || (pixel_type & 0x7F000000) == 0x44000000; }
    bool IsPlanarRGB() const { return (pixel_type & 0x7F000000) == 0x34000000; }
    bool IsPlanar()    const { return (pixel_type & 0x20000000) != 0; }
    bool IsFloat()     const { return (pixel_type & 0x10) != 0; }
    bool IsYUV()       const { return (pixel_type & 0x7F000000) == 0x20000000; }
    int BitsPerComponent() const {
        switch (pixel_type & 0xC0000000) {
            case 0x80000000: return 16;
            case 0xC0000000: return 32;
            default:         return 8;
        }
    }
    int ComponentSize() const { return IsFloat() ? 4 : (BitsPerComponent() + 7) / 8; }
};

class VideoFrame;
class VideoFrameBuffer;
class IScriptEnvironment;
class IClip;
class AVSValue;

class VideoFrame {
    mutable volatile int refcount;
public:
    int GetPitch(int plane = 0) const { (void)plane; return 0; }
    const uint8_t* GetReadPtr(int plane = 0) const { (void)plane; return nullptr; }
    uint8_t* GetWritePtr(int plane = 0) const { (void)plane; return nullptr; }
    void AddRef() { ++refcount; }
    void Release() { if (--refcount == 0) delete this; }
    virtual ~VideoFrame() {}
};

class PVideoFrame {
    VideoFrame* p;
public:
    PVideoFrame() : p(nullptr) {}
    PVideoFrame(const PVideoFrame& o) : p(o.p) { if (p) p->AddRef(); }
    ~PVideoFrame() { if (p) p->Release(); }
    PVideoFrame& operator=(const PVideoFrame& o) {
        if (o.p) o.p->AddRef(); if (p) p->Release(); p = o.p; return *this;
    }
    VideoFrame* operator->() const { return p; }
    operator VideoFrame* () const { return p; }
};

class IClip {
public:
    virtual ~IClip() {}
    virtual PVideoFrame GetFrame(int n, IScriptEnvironment* env) = 0;
    virtual bool        GetParity(int n) = 0;
    virtual void        GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) = 0;
    virtual int         SetCacheHints(int cachehints, int frame_range) = 0;
    virtual const VideoInfo& GetVideoInfo() = 0;
    void AddRef()  { ++refcount; }
    void Release() { if (--refcount == 0) delete this; }
private:
    mutable volatile int refcount = 0;
};

class PClip {
    IClip* p;
public:
    PClip() : p(nullptr) {}
    PClip(IClip* x) : p(x) { if (p) p->AddRef(); }
    PClip(const PClip& o) : p(o.p) { if (p) p->AddRef(); }
    ~PClip() { if (p) p->Release(); }
    PClip& operator=(const PClip& o) {
        if (o.p) o.p->AddRef(); if (p) p->Release(); p = o.p; return *this;
    }
    IClip* operator->() const { return p; }
    operator IClip* () const { return p; }
};

class AVSValue {
public:
    AVSValue() {}
    AVSValue(const AVSValue* a, int size) { (void)a; (void)size; }
    AVSValue(IClip* c)   : clip(c)  {}
    AVSValue(const PClip& c) : clip(c) {}
    AVSValue(int i)      : integer(i) {}
    AVSValue(float f)    : floating(f) {}
    AVSValue(double d)   : floating(d) {}
    AVSValue(bool b)     : boolean(b) {}
    AVSValue(const char* s) : string(s) {}
    bool IsClip()   const { return true; }
    bool IsInt()    const { return false; }
    bool IsFloat()  const { return true; }
    bool IsBool()   const { return false; }
    bool IsString() const { return false; }
    bool IsArray()  const { return false; }
    int         AsInt(int def = 0)          const { (void)def; return 0; }
    float       AsFloatf(float def = 0.f)   const { (void)def; return 0.f; }
    double      AsFloat(double def = 0.0)   const { (void)def; return 0.0; }
    bool        AsBool(bool def = false)    const { (void)def; return false; }
    const char* AsString(const char* def = "") const { (void)def; return ""; }
    IClip*      AsClip() const { return clip; }
    const AVSValue& operator[](int) const { return *this; }
    int ArraySize() const { return 0; }
private:
    IClip* clip = nullptr;
    int integer = 0; double floating = 0; bool boolean = false; const char* string = nullptr;
};

struct AVSMap;

class IScriptEnvironment {
public:
    virtual ~IScriptEnvironment() {}
    virtual PVideoFrame NewVideoFrameP(const VideoInfo& vi, PVideoFrame* propSrc, bool align = false) {
        (void)vi; (void)propSrc; (void)align; return PVideoFrame();
    }
    virtual void ThrowError(const char* fmt, ...) = 0;
    virtual AVSValue Invoke(const char*, const AVSValue&, const char* const* = nullptr) = 0;
    virtual const AVSMap* getFramePropsRO(const PVideoFrame& frame) { (void)frame; return nullptr; }
    virtual int64_t propGetInt(const AVSMap* map, const char* key, int index, int* error) {
        (void)map; (void)key; (void)index; if (error) *error = 1; return 0;
    }
    virtual void AddFunction(const char* name, const char* params,
                             AVSValue(__cdecl* create)(AVSValue, void*, IScriptEnvironment*),
                             void* user_data = nullptr) = 0;
    virtual void SetFilterMTMode(const char* filter, int mode, bool force = false) = 0;
};

class GenericVideoFilter : public IClip {
protected:
    PClip child;
    VideoInfo vi;
public:
    GenericVideoFilter(PClip _child) : child(_child) { vi = child->GetVideoInfo(); }
    PVideoFrame GetFrame(int n, IScriptEnvironment* env) override { return child->GetFrame(n, env); }
    bool GetParity(int n) override { return child->GetParity(n); }
    void GetAudio(void* buf, int64_t s, int64_t c, IScriptEnvironment* env) override { child->GetAudio(buf, s, c, env); }
    int SetCacheHints(int h, int r) override { (void)h; (void)r; return 0; }
    const VideoInfo& GetVideoInfo() override { return vi; }
};

struct AVS_Linkage { const void* fp[64]; };
extern AVS_Linkage const* AVS_linkage;

// frame properties API (interface v10+), matching official avisynth.h signatures.
struct AVSMap;
