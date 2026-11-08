// Direct driver for libvt_custom12_*.so / libvt_custom15_*.so.
// The sequence (dlopen, dlsym CUSTOMxx_*, LOADTTS, TextToBuffer loop) follows libhkport.so
// in HKMC.apk. libvt_multiVoice_t.so and libtts_module.so are not used on this path.
// Note that libhkport.so is a reference for the sequence only, not for the LOADTTS arguments:
// it is ARM mode and leaves r1/r2 holding dlsym leftovers, so its own call fails with -300.
// The argument convention below was taken from the engine's own disassembly.
#include <jni.h>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>
#include <android/log.h>
#include "tts_core.h"

#define LOG_TAG "TTS_DIRECT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr jint kErrBadArgument = -1;
constexpr jint kErrDlopen = -1000;
constexpr jint kErrDlsym = -1001;
constexpr jint kErrOtherEngineLoaded = -1002;
constexpr jsize kMaxTextBytes = 2000;

std::mutex g_mutex;
void* g_handle = nullptr;
std::string g_loaded_lib;
ttscore::loadtts_t g_load = nullptr;
ttscore::text_to_buffer_t g_text = nullptr;
int g_last_status = 0;

void* find_symbol(void* handle, const char* name12, const char* name15) {
    void* p = dlsym(handle, name12);
    return p ? p : dlsym(handle, name15);
}

void reset_state() {
    if (g_handle) dlclose(g_handle);
    g_handle = nullptr;
    g_loaded_lib.clear();
    g_load = nullptr;
    g_text = nullptr;
}

}  // namespace

// Loads the engine library and runs LOADTTS. Idempotent for the same library.
// Returns the LOADTTS result (0 or greater on success) or a negative error code.
extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsDirect_nInit(JNIEnv* env, jclass, jstring libPath, jstring dbPath, jstring engineId) {
    if (!libPath || !dbPath || !engineId) return kErrBadArgument;
    const char* lib = env->GetStringUTFChars(libPath, nullptr);
    const char* db = env->GetStringUTFChars(dbPath, nullptr);
    const char* id = env->GetStringUTFChars(engineId, nullptr);
    jint ret = kErrBadArgument;

    if (lib && db && id && *lib && *db && *id) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_handle) {
            ret = (g_loaded_lib == lib) ? 0 : kErrOtherEngineLoaded;
            if (ret != 0) LOGE("nInit: %s is already loaded, restart the app to switch", g_loaded_lib.c_str());
        } else {
            void* h = dlopen(lib, RTLD_LAZY);
            if (!h) {
                LOGE("dlopen %s: %s", lib, dlerror());
                ret = kErrDlopen;
            } else {
                g_load = reinterpret_cast<ttscore::loadtts_t>(
                    find_symbol(h, "CUSTOM12_LOADTTS", "CUSTOM15_LOADTTS"));
                g_text = reinterpret_cast<ttscore::text_to_buffer_t>(
                    find_symbol(h, "CUSTOM12_TextToBuffer", "CUSTOM15_TextToBuffer"));
                if (!g_load || !g_text) {
                    LOGE("dlsym failed in %s (load=%p text=%p)", lib, reinterpret_cast<void*>(g_load),
                         reinterpret_cast<void*>(g_text));
                    g_handle = h;
                    reset_state();
                    ret = kErrDlsym;
                } else {
                    // engineId is not an argument of LOADTTS: the speaker id is hardcoded to 82
                    // (0x177ea) and the version string is compared against a rodata literal.
                    // arg3 is spliced between dbPath and "userdict_kor.csv", so the engine opens
                    // exactly <dbPath>/userdict/userdict_kor.csv.
                    ret = g_load(db, ttscore::kEngineVersion, ttscore::kUserDictPrefix);
                    LOGI("LOADTTS(%s) = %d", db, ret);
                    if (ret == 0) {
                        g_handle = h;
                        g_loaded_lib = lib;
                    } else {
                        // -300 means arg2 did not match the version literal embedded in the
                        // library; any other value is a load failure inside VT_LOADTTS_EXT_KOR.
                        LOGE("LOADTTS failed (%d) for %s", ret, db);
                        g_handle = h;
                        reset_state();
                    }
                }
            }
        }
    }

    if (lib) env->ReleaseStringUTFChars(libPath, lib);
    if (db) env->ReleaseStringUTFChars(dbPath, db);
    if (id) env->ReleaseStringUTFChars(engineId, id);
    return ret;
}

// Synthesizes CP949 text and returns a complete WAV file (22050 Hz, mono, 16-bit), or null on failure.
// The failure reason is available from nLastStatus().
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mediazen_ttsapp_TtsDirect_nSynth(JNIEnv* env, jclass, jbyteArray text) {
    if (!text) { g_last_status = kErrBadArgument; return nullptr; }
    jsize n = env->GetArrayLength(text);
    if (n <= 0 || n > kMaxTextBytes) { g_last_status = kErrBadArgument; return nullptr; }

    std::vector<char> buf(static_cast<size_t>(n) + 1, 0);
    env->GetByteArrayRegion(text, 0, n, reinterpret_cast<jbyte*>(buf.data()));
    if (memchr(buf.data(), 0, static_cast<size_t>(n)) != nullptr) { g_last_status = kErrBadArgument; return nullptr; }

    std::vector<uint8_t> pcm;
    ttscore::SynthResult r = {ttscore::kStatusNoPcm, 0, 0};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_text) { g_last_status = kErrDlsym; return nullptr; }
        r = ttscore::synth_loop(g_text, buf.data(), pcm);
        g_last_status = r.status;
    }
    LOGI("synth: %d text bytes -> %zu pcm bytes, frames=%d, last_ret=%d, status=%d",
         static_cast<int>(n), pcm.size(), r.frames, r.last_ret, r.status);
    if (r.status != ttscore::kStatusOk) return nullptr;

    std::vector<uint8_t> wav(ttscore::kWavHeaderSize + pcm.size());
    ttscore::build_wav_header(static_cast<uint32_t>(pcm.size()), wav.data());
    memcpy(wav.data() + ttscore::kWavHeaderSize, pcm.data(), pcm.size());

    jbyteArray out = env->NewByteArray(static_cast<jsize>(wav.size()));
    if (!out) { g_last_status = kErrBadArgument; return nullptr; }
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(wav.size()), reinterpret_cast<const jbyte*>(wav.data()));
    return out;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsDirect_nLastStatus(JNIEnv*, jclass) {
    return g_last_status;
}
