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

// CUSTOMxx_TextToBuffer: (text, out, &out_len, continue_flag, 0, 0, -1 x 6).
// Return value: 1 on the last frame, 0 when more frames follow, negative on error.
typedef int (*text_to_buffer_t)(const char* text, void* out, int* out_len, int cont,
                                int a4, int a5, int a6, int a7, int a8,
                                int a9, int a10, int a11);

struct SynthResult {
    int status;    // kStatusOk, kStatusNoPcm, or the engine's negative return value
    int frames;    // number of engine calls made
    int last_ret;  // return value of the final engine call
};

/**
 * Calls fn repeatedly and appends each returned frame to pcm.
 * Stops when fn returns 1, a negative value, or an empty frame, or after kMaxFrames calls.
 * Success means at least one byte of PCM was produced, as in the original library.
 */
inline SynthResult synth_loop(text_to_buffer_t fn, const char* text, std::vector<uint8_t>& pcm) {
    SynthResult r = {kStatusNoPcm, 0, 0};
    std::vector<uint8_t> buf(kFrameBufSize);
    int cont = 0;
    for (int i = 0; i < kMaxFrames; ++i) {
        int len = 0;
        int ret = fn(text, buf.data(), &len, cont, 0, 0, -1, -1, -1, -1, -1, -1);
        r.frames = i + 1;
        r.last_ret = ret;
        if (len >= 1 && static_cast<size_t>(len) <= kFrameBufSize) {
            pcm.insert(pcm.end(), buf.begin(), buf.begin() + len);
        }
        cont = 1;
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
