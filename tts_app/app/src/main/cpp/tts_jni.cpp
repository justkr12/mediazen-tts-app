#include <jni.h>
#include <string>
#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "TTS_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef enum {
    SPEAKER_DEFAULT = -1,
    SPEAKER_CUSTOM12 = 82,
    SPEAKER_CUSTOM15 = 85,
} speaker_id_t;

typedef int (*tts_engine_open_t)(int, const char*, speaker_id_t, int*, int);
typedef int (*tts_engine_tts_t)(int, const char*);
typedef int (*tts_engine_wavedrop_t)(bool, const char*);
typedef void (*tts_engine_close_t)();
typedef int (*tts_engine_version_t)(char*);
typedef void (*tts_engine_interrupt_t)();

static void* g_handle = nullptr;
static tts_engine_open_t g_open = nullptr;
static tts_engine_tts_t g_tts = nullptr;
static tts_engine_wavedrop_t g_wavedrop = nullptr;
static tts_engine_close_t g_close = nullptr;
static tts_engine_version_t g_version = nullptr;
static tts_engine_interrupt_t g_interrupt = nullptr;

static std::string g_lib_dir;

// Java에서 직접 경로를 전달받음 (가장 확실한 방법)
extern "C" JNIEXPORT void JNICALL
Java_com_mediazen_ttsapp_TtsEngine_setLibDir(JNIEnv* env, jclass, jstring libDir) {
    const char* dir = env->GetStringUTFChars(libDir, nullptr);
    g_lib_dir = std::string(dir);
    env->ReleaseStringUTFChars(libDir, dir);
    LOGI("setLibDir: %s", g_lib_dir.c_str());
}

static void* load_dep(const char* name) {
    if (g_lib_dir.empty()) {
        LOGE("load_dep: g_lib_dir is empty, cannot load %s", name);
        return nullptr;
    }
    std::string path = g_lib_dir + "/" + name;
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        LOGE("load_dep FAIL %s : %s", name, dlerror());
    } else {
        LOGI("load_dep OK %s", name);
    }
    return h;
}

static bool ensure_loaded() {
    if (g_handle) return true;

    LOGI("ensure_loaded: lib_dir=%s", g_lib_dir.c_str());

    // 로드 옵션 플래그 RTLD_NOW | RTLD_GLOBAL 보장
    load_dep("libvt_Dic_module.so");
    load_dep("libvt_custom12_H22.so");
    load_dep("libvt_custom12_M22.so");
    load_dep("libvt_custom15_H22.so");
    load_dep("libvt_custom15_sP22.so");
    load_dep("libvt_multiVoice_t.so");

    std::string tts_path = g_lib_dir.empty()
        ? std::string("libtts_module.so")
        : g_lib_dir + "/libtts_module.so";

    g_handle = dlopen(tts_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!g_handle) {
        LOGE("dlopen libtts_module.so failed: %s", dlerror());
        return false;
    }

    g_open     = (tts_engine_open_t)    dlsym(g_handle, "_Z15tts_engine_openiPKc13_speaker_id_tPii");
    if (!g_open) LOGE("dlsym failed for tts_engine_open: %s", dlerror());

    g_tts      = (tts_engine_tts_t)     dlsym(g_handle, "_Z14tts_engine_ttsiPKc");
    if (!g_tts) LOGE("dlsym failed for tts_engine_tts: %s", dlerror());

    g_wavedrop = (tts_engine_wavedrop_t) dlsym(g_handle, "_Z19tts_engine_wavedropbPKc");
    if (!g_wavedrop) LOGE("dlsym failed for tts_engine_wavedrop: %s", dlerror());

    g_close    = (tts_engine_close_t)   dlsym(g_handle, "_Z16tts_engine_closev");
    if (!g_close) LOGE("dlsym failed for tts_engine_close: %s", dlerror());

    g_version  = (tts_engine_version_t)  dlsym(g_handle, "_Z18tts_engine_versionPc");
    if (!g_version) LOGE("dlsym failed for tts_engine_version: %s", dlerror());

    g_interrupt= (tts_engine_interrupt_t)dlsym(g_handle, "_Z20tts_engine_interruptv");
    if (!g_interrupt) LOGE("dlsym failed for tts_engine_interrupt: %s", dlerror());

    if (!g_open || !g_tts || !g_wavedrop || !g_close) {
        LOGE("dlsym critical check failed");
        dlclose(g_handle);
        g_handle = nullptr;
        return false;
    }
    LOGI("TTS engine loaded OK");
    return true;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineOpen(
    JNIEnv* env, jclass, jint nStream, jstring dbPath, jint speakerId, jintArray frameSize, jint bootOption) {
    if (!ensure_loaded()) return -1;
    const char* path = env->GetStringUTFChars(dbPath, nullptr);
    int fs = 0;
    int ret = g_open(nStream, path, (speaker_id_t)speakerId, &fs, bootOption);
    env->ReleaseStringUTFChars(dbPath, path);
    if (frameSize) {
        env->SetIntArrayRegion(frameSize, 0, 1, &fs);
    }
    return ret;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineTts(
    JNIEnv* env, jclass, jint nStream, jstring text) {
    if (!ensure_loaded()) return -1;
    const char* t = env->GetStringUTFChars(text, nullptr);
    int ret = g_tts(nStream, t);
    env->ReleaseStringUTFChars(text, t);
    return ret;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineWaveDrop(
    JNIEnv* env, jclass, jboolean flag, jstring pathName) {
    if (!ensure_loaded()) return -1;
    const char* path = env->GetStringUTFChars(pathName, nullptr);
    int ret = g_wavedrop(flag, path);
    env->ReleaseStringUTFChars(pathName, path);
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineClose(JNIEnv*, jclass) {
    if (g_close) g_close();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineVersion(JNIEnv* env, jclass, jobject versionObj) {
    if (!ensure_loaded() || !g_version) return -1;
    char buf[256] = {0};
    int ret = g_version(buf);
    if (ret == 0) {
        jclass cls = env->GetObjectClass(versionObj);
        jmethodID append = env->GetMethodID(cls, "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;");
        env->CallObjectMethod(versionObj, append, env->NewStringUTF(buf));
    }
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_mediazen_ttsapp_TtsEngine_ttsEngineInterrupt(JNIEnv*, jclass) {
    if (g_interrupt) g_interrupt();
}
