// Host test for tts_core.h. Build and run from the tests directory:
//   g++ -std=c++14 -Wall -Wextra -fsanitize=address,undefined -I../app/src/main/cpp test_tts_core.cpp -o test_tts_core
//   ./test_tts_core
#include <cassert>
#include <cstdio>
#include "tts_core.h"

using namespace ttscore;

static int g_calls;
static int g_first_cont;
static int g_other_cont_ok;
static int g_args_ok;

static int check_args(int cont, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11) {
    if (g_calls == 0) g_first_cont = cont; else if (cont != 1) g_other_cont_ok = 0;
    if (a4 != 0 || a5 != 0 || a6 != -1 || a7 != -1 || a8 != -1 || a9 != -1 || a10 != -1 || a11 != -1) g_args_ok = 0;
    return 0;
}

static void reset() { g_calls = 0; g_first_cont = -1; g_other_cont_ok = 1; g_args_ok = 1; }

// Three frames of 4, 6, 2 bytes; the last call returns 1.
static int fake_three(const char*, void* out, int* len, int cont, int a4, int a5, int a6, int a7,
                      int a8, int a9, int a10, int a11) {
    check_args(cont, a4, a5, a6, a7, a8, a9, a10, a11);
    static const int sizes[3] = {4, 6, 2};
    int n = sizes[g_calls];
    for (int i = 0; i < n; ++i) static_cast<uint8_t*>(out)[i] = static_cast<uint8_t>(g_calls * 16 + i);
    *len = n;
    return (++g_calls == 3) ? 1 : 0;
}

// Error with no data on the first call.
static int fake_error(const char*, void*, int* len, int, int, int, int, int, int, int, int, int) {
    ++g_calls;
    *len = 0;
    return -7;
}

// Frame, then an error on the second call: data was produced, so status stays ok.
static int fake_partial(const char*, void* out, int* len, int, int, int, int, int, int, int, int, int) {
    ++g_calls;
    if (g_calls == 1) { static_cast<uint8_t*>(out)[0] = 9; *len = 1; return 0; }
    *len = 0;
    return -3;
}

// Oversized length is not appended and does not stop the loop (same as the original library).
static int fake_oversize(const char*, void*, int* len, int, int, int, int, int, int, int, int, int) {
    ++g_calls;
    *len = static_cast<int>(kFrameBufSize) + 1;
    return 0;
}

// Never terminates: exercises the frame cap.
static int fake_endless(const char*, void* out, int* len, int, int, int, int, int, int, int, int, int) {
    ++g_calls;
    static_cast<uint8_t*>(out)[0] = 1;
    *len = 1;
    return 0;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

int main() {
    std::vector<uint8_t> pcm;

    reset();
    SynthResult r = synth_loop(fake_three, "x", pcm);
    assert(r.status == kStatusOk && r.frames == 3 && r.last_ret == 1);
    assert(pcm.size() == 12);
    assert(pcm[0] == 0 && pcm[3] == 3 && pcm[4] == 16 && pcm[9] == 21 && pcm[10] == 32 && pcm[11] == 33);
    assert(g_first_cont == 0 && g_other_cont_ok == 1 && g_args_ok == 1);

    reset(); pcm.clear();
    r = synth_loop(fake_error, "x", pcm);
    assert(r.status == -7 && pcm.empty() && r.frames == 1);

    reset(); pcm.clear();
    r = synth_loop(fake_partial, "x", pcm);
    assert(r.status == kStatusOk && pcm.size() == 1 && r.frames == 2 && r.last_ret == -3);

    reset(); pcm.clear();
    r = synth_loop(fake_oversize, "x", pcm);
    assert(r.status == kStatusNoPcm && pcm.empty() && r.frames == kMaxFrames);

    reset(); pcm.clear();
    r = synth_loop(fake_endless, "x", pcm);
    assert(r.status == kStatusOk && r.frames == kMaxFrames && pcm.size() == size_t(kMaxFrames));

    uint8_t h[kWavHeaderSize];
    build_wav_header(1000, h);
    assert(std::memcmp(h, "RIFF", 4) == 0 && rd32(h + 4) == 1036);
    assert(std::memcmp(h + 8, "WAVEfmt ", 8) == 0 && rd32(h + 16) == 16);
    assert(rd16(h + 20) == 1 && rd16(h + 22) == 1);
    assert(rd32(h + 24) == 22050 && rd32(h + 28) == 44100);
    assert(rd16(h + 32) == 2 && rd16(h + 34) == 16);
    assert(std::memcmp(h + 36, "data", 4) == 0 && rd32(h + 40) == 1000);

    std::puts("all tests passed");
    return 0;
}
