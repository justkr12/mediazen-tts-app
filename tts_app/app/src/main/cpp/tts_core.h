// Host-testable core of the direct CUSTOM12/CUSTOM15 synthesis path.
// The call convention was taken from libhkport.so (Java_com_hkport_Vt_nSynth) in HKMC.apk.
#ifndef TTS_CORE_H
#define TTS_CORE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ttscore {

constexpr size_t kFrameBufSize = 0x40000;
constexpr int kMaxFrames = 100000;

constexpr uint32_t kSampleRate = 22050;
constexpr uint16_t kChannels = 1;
constexpr uint16_t kBitsPerSample = 16;
constexpr size_t kWavHeaderSize = 44;

constexpr int kStatusOk = 0;
constexpr int kStatusNoPcm = -2;

// Output codec selector: the 4th argument indexes a jump table in VT_TextToBuffer_KOR
// (libvt_custom1x_H22.so, tbb at 0x17d64). Only 3 selects raw PCM.
//   0 = TextToDialogicAdpcmBuffer_COMMON
//   1 = TextToMulawPcmBuffer_COMMON
//   2 = TextToAlawPcmBuffer_COMMON
//   3 = TextToPcmBuffer_COMMON
constexpr int kCodecPcm = 3;

// CUSTOMxx_LOADTTS(db_path, version, user_dict_prefix) - three arguments.
// Confirmed from libvt_custom12_H22.so at 0x177a4:
//   177b0 mov r6,r0 ; 177f6 mov r2,r6 ; 177f8 bl VT_LOADTTS_EXT_KOR -> arg1 is the dbPath
//   177d0 mov r1,r7 ; 177d6 bl compare_str_IgnoreWhiteSpace          -> arg2 must equal the
//                                                                      version literal below
//   1780e mov r0,r6 ; 17810 add r1,pc ("%s%s") ; 1780c mov r2,r8 (arg3)
//   17812 add r3,pc ("userdict_kor.csv") ; 17814 blx sprintf
//   17826 mov r1,r6 ; 1782a bl VT_LOAD_UserDict_KOR
// arg2 is compared against a rodata literal and a mismatch returns -300 from 0x1783e.
// sprintf writes into arg1, so the user-dict path is exactly db_path + arg3 + "userdict_kor.csv".
// The version literal below is the one stored at rodata 0x5d38c in both the custom12 and
// custom15 H22 libraries.
constexpr char kEngineVersion[] = "TTS.KOK.L-NRv24.101.240603-L2voice";

// Appended between the staged dbPath and "userdict_kor.csv". The voice packs ship the file as
// <db>/userdict/userdict_kor.csv, so the staged root must expose that same subdirectory.
constexpr char kUserDictPrefix[] = "userdict/";

constexpr char kUserDictFileName[] = "userdict_kor.csv";

// Returned by CUSTOMxx_LOADTTS when arg2 does not match the embedded version literal.
constexpr int kLoadTtsVersionMismatch = -300;

typedef int (*loadtts_t)(const char* db_path, const char* version, const char* user_dict_prefix);

// CUSTOMxx_TextToBuffer: (text, out, &out_len, codec, 0, 0, -1 x 6).
// Return value: 1 on the last frame, 0 when more frames follow, negative on error.
typedef int (*text_to_buffer_t)(const char* text, void* out, int* out_len, int codec,
                                int a4, int a5, int a6, int a7, int a8,
                                int a9, int a10, int a11);

struct SynthResult {
    int status;    // kStatusOk, kStatusNoPcm, or the engine's negative return value
    int frames;    // number of engine calls made
    int last_ret;  // return value of the final engine call
};

// Optional per-call trace hook, called as trace(frame_index, ret, len). Null disables it.
typedef void (*frame_trace_t)(int frame, int ret, int len);

/**
 * Calls fn repeatedly and appends each returned frame to pcm.
 * Stops when fn returns 1, a negative value, or an empty frame, or after kMaxFrames calls.
 * Success means at least one byte of PCM was produced, as in the original library.
 */
inline SynthResult synth_loop(text_to_buffer_t fn, const char* text, std::vector<uint8_t>& pcm,
                              frame_trace_t trace = nullptr) {
    SynthResult r = {kStatusNoPcm, 0, 0};
    std::vector<uint8_t> buf(kFrameBufSize);
    for (int i = 0; i < kMaxFrames; ++i) {
        int len = 0;
        int ret = fn(text, buf.data(), &len, kCodecPcm, 0, 0, -1, -1, -1, -1, -1, -1);
        r.frames = i + 1;
        r.last_ret = ret;
        if (trace) trace(i, ret, len);
        if (len >= 1 && static_cast<size_t>(len) <= kFrameBufSize) {
            pcm.insert(pcm.end(), buf.begin(), buf.begin() + len);
        }
        if (ret == 1 || ret < 0 || len < 1) break;
    }
    if (!pcm.empty()) {
        r.status = kStatusOk;
    } else if (r.last_ret < 0) {
        r.status = r.last_ret;
    }
    return r;
}

inline void put_le32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void put_le16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

/** Builds a 44-byte PCM WAV header for pcm_bytes of 22050 Hz mono 16-bit audio. */
inline void build_wav_header(uint32_t pcm_bytes, uint8_t out[kWavHeaderSize]) {
    const uint16_t block_align = static_cast<uint16_t>(kChannels * kBitsPerSample / 8);
    std::memcpy(out, "RIFF", 4);
    put_le32(out + 4, 36u + pcm_bytes);
    std::memcpy(out + 8, "WAVEfmt ", 8);
    put_le32(out + 16, 16u);
    put_le16(out + 20, 1);
    put_le16(out + 22, kChannels);
    put_le32(out + 24, kSampleRate);
    put_le32(out + 28, kSampleRate * block_align);
    put_le16(out + 32, block_align);
    put_le16(out + 34, kBitsPerSample);
    std::memcpy(out + 36, "data", 4);
    put_le32(out + 40, pcm_bytes);
}

}  // namespace ttscore

#endif  // TTS_CORE_H
