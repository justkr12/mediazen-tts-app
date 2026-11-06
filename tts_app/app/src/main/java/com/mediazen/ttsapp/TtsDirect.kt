package com.mediazen.ttsapp

import android.util.Log
import java.io.File
import java.nio.charset.Charset

class TtsDirect {
    companion object {
        // Engine identifier passed to CUSTOMxx_LOADTTS (taken from libhkport.so in HKMC.apk).
        const val ENGINE_ID = "TTS.KOK.L-NRv24.101.240603-L2voice"

        private const val TAG = "TTS_MAIN"
        private val LIB_VARIANTS = arrayOf("H22", "sP22")

        init {
            System.loadLibrary("tts_jni")
        }

        @JvmStatic
        external fun nInit(libPath: String, dbPath: String, engineId: String): Int
        @JvmStatic
        external fun nSynth(textCp949: ByteArray): ByteArray?
        @JvmStatic
        external fun nLastStatus(): Int

        /**
         * Loads the engine for the given speaker. The voice pack folder is dbRoot/custom12/ or dbRoot/custom15/.
         * Tries each library variant in order and returns the first non-negative LOADTTS result,
         * otherwise the last error code.
         */
        fun open(libDir: String, dbRoot: String, speakerId: Int): Int {
            val tag = if (speakerId == TtsEngine.Speaker.CUSTOM12.id) "12" else "15"
            val dbPath = "${dbRoot.trimEnd('/')}/custom$tag/"
            var ret = -1
            for (variant in LIB_VARIANTS) {
                val lib = File(libDir, "libvt_custom${tag}_$variant.so")
                if (!lib.exists()) continue
                ret = nInit(lib.absolutePath, dbPath, ENGINE_ID)
                Log.i(TAG, "open ${lib.name} db=$dbPath -> $ret")
                if (ret >= 0) return ret
            }
            return ret
        }

        /** Converts text to the engine's input encoding (CP949), replacing NBSP with a space. */
        fun encodeText(text: String): ByteArray {
            val cs = try {
                Charset.forName("x-windows-949")
            } catch (e: Exception) {
                Charset.forName("EUC-KR")
            }
            return text.replace('\u00A0', ' ').toByteArray(cs)
        }
    }
}
