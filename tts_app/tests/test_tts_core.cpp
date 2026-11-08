// Host test for tts_core.h. Build and run from the tests directory:
//   g++ -std=c++14 -Wall -Wextra -fsanitize=address,undefined -I../app/src/main/cpp test_tts_core.cpp -o test_tts_core
//   ./test_tts_core
#include <cassert>
#include <cstdio>
#include "tts_core.h"

using namespace ttscore;

static int g_calls;
static int g_codec_ok;
static int g_args_ok;

// The 4th argument selects the output codec and must be kCodecPcm on every call.
static int check_args(int codec, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11) {
    if (codec != kCodecPcm) g_codec_ok = 0;
    if (a4 != 0 || a5 != 0 || a6 != -1 || a7 != -1 || a8 != -1 || a9 != -1 || a10 != -1 || a11 != -1) g_args_ok = 0;
    return 0;
}

static void reset() { g_calls = 0; g_codec_ok = 1; g_args_ok = 1; }

// Three frames of 4, 6, 2 bytes; the last call returns 1.
static int fake_three(const char*, void* out, int* len, int codec, int a4, int a5, int a6, int a7,
                      int a8, int a9, int a10, int a11) {
    check_args(codec, a4, a5, a6, a7, a8, a9, a10, a11);
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

// Trace hook: records what synth_loop observed on each call.
static int g_trace_calls;
static int g_trace_frame[8];
static int g_trace_ret[8];
static int g_trace_len[8];

static void trace_hook(int frame, int ret, int len) {
    if (g_trace_calls < 8) {
        g_trace_frame[g_trace_calls] = frame;
        g_trace_ret[g_trace_calls] = ret;
        g_trace_len[g_trace_calls] = len;
    }
    ++g_trace_calls;
}

static void trace_reset() {
    g_trace_calls = 0;
    for (int i = 0; i < 8; ++i) { g_trace_frame[i] = -1; g_trace_ret[i] = 0; g_trace_len[i] = -1; }
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// CUSTOMxx_LOADTTS takes exactly three arguments. Passing only two would leave the version slot
// holding whatever the previous dlsym call left in r1, which the engine compares against its
// embedded literal and rejects with -300 (libvt_custom12_H22.so 0x177d6 -> 0x1783e).
static char g_load_db[64];
static char g_load_ver[64];
static char g_load_prefix[64];
static int g_load_calls;

static int fake_load(const char* db, const char* ver, const char* prefix) {
    ++g_load_calls;
    std::snprintf(g_load_db, sizeof(g_load_db), "%s", db ? db : "(null)");
    std::snprintf(g_load_ver, sizeof(g_load_ver), "%s", ver ? ver : "(null)");
    std::snprintf(g_load_prefix, sizeof(g_load_prefix), "%s", prefix ? prefix : "(null)");
    // Mirrors the engine: a version that does not match the embedded literal fails with -300.
    return std::strcmp(ver ? ver : "", kEngineVersion) == 0 ? 0 : kLoadTtsVersionMismatch;
}

static void test_loadtts_contract() {
    // The declared typedef must accept three arguments; this fails to compile otherwise.
    loadtts_t fn = fake_load;
    g_load_calls = 0;
    int ret = fn("/data/staged12/", kEngineVersion, kUserDictPrefix);
    assert(g_load_calls == 1);
    assert(ret == 0);
    assert(std::strcmp(g_load_db, "/data/staged12/") == 0);
    assert(std::strcmp(g_load_ver, kEngineVersion) == 0);
    assert(std::strcmp(g_load_prefix, kUserDictPrefix) == 0);

    // The engine does sprintf(db_path, "%s%s", arg3, "userdict_kor.csv") at 0x1780e-0x17814,
    // so the file it opens is db_path + arg3 + kUserDictFileName.
    assert(std::strcmp(kUserDictPrefix, "userdict/") == 0);
    assert(std::strcmp(kUserDictFileName, "userdict_kor.csv") == 0);
    char composed[128];
    std::snprintf(composed, sizeof(composed), "%s%s%s", "/data/staged12/", kUserDictPrefix,
                  kUserDictFileName);
    assert(std::strcmp(composed, "/data/staged12/userdict/userdict_kor.csv") == 0);

    // The version constant must be the exact rodata literal, byte for byte.
    assert(std::strlen(kEngineVersion) == 34);
    assert(std::strcmp(kEngineVersion, "TTS.KOK.L-NRv24.101.240603-L2voice") == 0);

    // Any other value in the version slot is rejected by the engine with -300.
    assert(fn("/data/staged12/", "TTS.KOK.L-NRv24.101.240603-L2voice ", "") == kLoadTtsVersionMismatch);
    assert(fn("/data/staged12/", "", "") == kLoadTtsVersionMismatch);
    assert(fn("/data/staged12/", "/data/staged12/", "") == kLoadTtsVersionMismatch);
    assert(kLoadTtsVersionMismatch == -300);
}

int main() {
    std::vector<uint8_t> pcm;

    reset();
    SynthResult r = synth_loop(fake_three, "x", pcm);
    assert(r.status == kStatusOk && r.frames == 3 && r.last_ret == 1);
    assert(pcm.size() == 12);
    assert(pcm[0] == 0 && pcm[3] == 3 && pcm[4] == 16 && pcm[9] == 21 && pcm[10] == 32 && pcm[11] == 33);
    assert(g_codec_ok == 1 && g_args_ok == 1);

    // The trace hook sees every call with its frame index, return value and length.
    reset(); pcm.clear(); trace_reset();
    r = synth_loop(fake_three, "x", pcm, trace_hook);
    assert(r.status == kStatusOk && r.frames == 3);
    assert(g_trace_calls == 3);
    assert(g_trace_frame[0] == 0 && g_trace_frame[1] == 1 && g_trace_frame[2] == 2);
    assert(g_trace_len[0] == 4 && g_trace_len[1] == 6 && g_trace_len[2] == 2);
    assert(g_trace_ret[2] == 1);

    // A null hook is accepted and changes nothing.
    reset(); pcm.clear();
    r = synth_loop(fake_three, "x", pcm, nullptr);
    assert(r.status == kStatusOk && r.frames == 3 && pcm.size() == 12);

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

    test_loadtts_contract();

    std::puts("all tests passed");
    return 0;
}
