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
         * Prepares the directory structure the engine expects under dbPath:
         *   {dbPath}data-common/   <- all files except vtdb and userdict_kor.csv
         *   {dbPath}fixed_db/      <- tts_single_db_kor_custom12_H22.vtdb or *_kor_custom15_*.vtdb
         *   {dbPath}userdict_kor.csv
         *
         * If the source folder (srcDir) already has these subdirectories nothing is done.
         * Otherwise a staging directory is created under stageRoot with symlinks pointing
         * back into srcDir so no data is copied.
         *
         * Returns the dbPath to pass to the engine, or null on failure.
         */
        fun prepareDbPath(srcDir: File, stageRoot: File, tag: String): String? {
            val dataCommon = File(srcDir, "data-common")
            val fixedDb = File(srcDir, "fixed_db")
            if (dataCommon.isDirectory && fixedDb.isDirectory) {
                // Already structured correctly.
                return srcDir.absolutePath.trimEnd('/') + "/"
            }

            // Build a staging tree under stageRoot/custom{tag}/
            val stage = File(stageRoot, "custom$tag")
            val stageCommon = File(stage, "data-common")
            val stageFixed = File(stage, "fixed_db")
            stageCommon.mkdirs()
            stageFixed.mkdirs()

            val vtdbSuffix = "tts_single_db_kor_custom${tag}_H22.vtdb"
            var ok = true
            srcDir.listFiles()?.forEach { src ->
                val name = src.name
                val dest: File = when {
                    name == vtdbSuffix -> File(stageFixed, name)
                    name == "userdict_kor.csv" -> File(stage, name)
                    else -> File(stageCommon, name)
                }
                if (!dest.exists()) {
                    try {
                        // Symlink so no data is duplicated.
                        val pb = ProcessBuilder("ln", "-sf", src.absolutePath, dest.absolutePath)
                        val exitCode = pb.start().waitFor()
                        if (exitCode != 0) {
                            Log.w(TAG, "symlink failed for ${src.name}, copying")
                            src.copyTo(dest, overwrite = true)
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "link/copy ${src.name}: ${e.message}")
                        ok = false
                    }
                }
            }
            if (!ok) Log.w(TAG, "prepareDbPath: some files could not be linked under $stage")
            return stage.absolutePath.trimEnd('/') + "/"
        }

        /**
         * Loads the engine for the given speaker.
         * hdttsRoot is the parent of custom12/ or custom15/ (e.g. /storage/emulated/0/hdtts).
         * stageRoot is a writable directory for the staging symlink tree (e.g. filesDir).
         * Tries each library variant in order and returns the first non-negative LOADTTS result,
         * otherwise the last error code.
         */
        fun open(libDir: String, hdttsRoot: String, stageRoot: File, speakerId: Int): Int {
            val tag = if (speakerId == TtsEngine.Speaker.CUSTOM12.id) "12" else "15"
            val srcDir = File("${hdttsRoot.trimEnd('/')}/custom$tag")
            val dbPath = prepareDbPath(srcDir, stageRoot, tag)
            if (dbPath == null) {
                Log.e(TAG, "prepareDbPath returned null for $srcDir")
                return -1
            }
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
