package com.mediazen.ttsapp

class TtsEngine {
    companion object {
        init {
            System.loadLibrary("tts_jni")
        }

        // nativeLibraryDir 경로를 C++로 전달 (dlopen 절대경로용)
        @JvmStatic
        external fun setLibDir(libDir: String)

        @JvmStatic
        external fun ttsEngineOpen(nStream: Int, dbPath: String, speakerId: Int, frameSize: IntArray, bootOption: Int): Int
        @JvmStatic
        external fun ttsEngineTts(nStream: Int, text: String): Int
        @JvmStatic
        external fun ttsEngineWaveDrop(bWaveDropFlag: Boolean, pathName: String): Int
        @JvmStatic
        external fun ttsEngineClose()
        @JvmStatic
        external fun ttsEngineVersion(version: StringBuilder): Int
        @JvmStatic
        external fun ttsEngineInterrupt()
    }

    enum class Speaker(val id: Int) {
        DEFAULT(-1),
        CUSTOM12(82),
        CUSTOM15(85)
    }
}
