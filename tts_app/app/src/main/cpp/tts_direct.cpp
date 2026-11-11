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

// Synthesizes CP949 text and returns raw 22050 Hz mono 16-bit PCM bytes, or null on failure.
// a0, a1, a2 are passed as the last three int arguments to TextToBuffer (-1 = use engine default).
// The failure reason is available from nLastStatus().
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mediazen_ttsapp_TtsDirect_nSynth(JNIEnv* env, jclass, jbyteArray text,
                                           jint a0, jint a1, jint a2) {
    if (!text) { g_last_status = kErrBadArgument; return nullptr; }
    jsize n = env->GetArrayLength(text);
    if (n <= 0 || n > kMaxTextBytes) { g_last_status = kErrBadArgument; return nullptr; }

    std::vector<char> buf(static_cast<size_t>(n) + 1, 0);
    env->GetByteArrayRegion(text, 0, n, reinterpret_cast<jbyte*>(buf.data()));
    if (memchr(buf.data(), 0, static_cast<size_t>(n)) != nullptr) {
        g_last_status = kErrBadArgument; return nullptr;
    }

    // Wrap the engine function to inject the caller-supplied a0/a1/a2 as the last 3 args.
    // text_to_buffer_t signature: (text, out, &len, codec, 0, 0, -1, -1, -1, a0, a1, a2)
    struct Ctx { ttscore::text_to_buffer_t fn; int a0, a1, a2; };
    Ctx ctx;
    std::vector<uint8_t> pcm;
    ttscore::SynthResult r = {ttscore::kStatusNoPcm, 0, 0};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_text) { g_last_status = kErrDlsym; return nullptr; }
        ctx = {g_text, a0, a1, a2};
        // Use a lambda-equivalent trampoline via a local wrapper.
        auto wrapper = [](const char* txt, void* out, int* len, int codec,
                          int b4, int b5, int b6, int b7, int b8,
                          int b9, int b10, int b11) -> int {
            // Not reachable: we call the engine directly below.
            (void)txt; (void)out; (void)len; (void)codec;
            (void)b4; (void)b5; (void)b6; (void)b7; (void)b8;
            (void)b9; (void)b10; (void)b11;
            return -1;
        };
        (void)wrapper; // suppress unused warning

        // Call synth_loop with a closure that injects a0/a1/a2.
        // Since text_to_buffer_t is a plain function pointer we cannot use a capturing lambda.
        // Store ctx in thread-local storage and use a static trampoline.
        static thread_local Ctx* tl_ctx = nullptr;
        tl_ctx = &ctx;
        static auto trampoline = [](const char* txt, void* out, int* len, int codec,
                                    int b4, int b5, int b6, int b7, int b8,
                                    int /*b9*/, int /*b10*/, int /*b11*/) -> int {
            return tl_ctx->fn(txt, out, len, codec, b4, b5, b6, b7, b8,
                              tl_ctx->a0, tl_ctx->a1, tl_ctx->a2);
        };
        r = ttscore::synth_loop(static_cast<ttscore::text_to_buffer_t>(trampoline),
                                buf.data(), pcm);
        g_last_status = r.status;
    }
    LOGI("synth: %d text bytes -> %zu pcm bytes, frames=%d, last_ret=%d, status=%d, args=(%d,%d,%d)",
         static_cast<int>(n), pcm.size(), r.frames, r.last_ret, r.status, a0, a1, a2);
    if (r.status != ttscore::kStatusOk) return nullptr;

    // Return raw PCM. The caller (Kotlin) plays it via AudioTrack directly (no WAV header needed).
    jbyteArray out = env->NewByteArray(static_cast<jsize>(pcm.size()));
    if (!out) { g_last_status = kErrBadArgument; return nullptr; }
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(pcm.size()),
                            reinterpret_cast<const jbyte*>(pcm.data()));
    return out;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsDirect_nLastStatus(JNIEnv*, jclass) {
    return g_last_status;
}
