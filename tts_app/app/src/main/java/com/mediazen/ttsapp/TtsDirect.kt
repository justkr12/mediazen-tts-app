package com.mediazen.ttsapp

import android.system.Os
import android.util.Log
import java.io.File
import java.nio.charset.Charset

class TtsDirect {
    companion object {
        const val ENGINE_ID = "TTS.KOK.L-NRv24.101.240603-L2voice"

        private const val TAG = "TTS_MAIN"
        private val LIB_VARIANTS = arrayOf("H22", "sP22")

        init {
            System.loadLibrary("tts_jni")
        }

        @JvmStatic external fun nInit(libPath: String, dbPath: String, engineId: String): Int
        // a0/a1/a2 are passed as the last three int arguments to TextToBuffer.
        // Use -1 to let the engine apply its defaults (confirmed from HKMC_Navi5W_phone.apk).
        @JvmStatic external fun nSynth(textCp949: ByteArray, a0: Int, a1: Int, a2: Int): ByteArray?
        @JvmStatic external fun nLastStatus(): Int

        // Path fragments confirmed by disassembly of CUSTOM12/15_LOADTTS and load_CommonDB:
        //   {dbPath}data-common/   <- tts_single_db.vtdb2, Diclist_*.txt, HMC_*.*, etc.
        //   {dbPath}fixed_db/      <- tts_single_db_kor_custom{tag}_{variant}.vtdb
        //   {dbPath}userdict/userdict_kor.csv  <- in a userdict/ subdirectory
        private const val DIR_DATA_COMMON = "data-common"
        private const val DIR_FIXED_DB = "fixed_db"
        private const val DIR_USERDICT = "userdict"
        private const val USERDICT_FILE = "userdict_kor.csv"

        fun vtdbName(tag: String, variant: String) =
            "tts_single_db_kor_custom${tag}_$variant.vtdb"

        private fun isFixedDb(name: String) =
            name.startsWith("tts_single_db_") && name.endsWith(".vtdb")

        /**
         * Builds the directory tree the engine expects, using a staging root beside the voice pack.
         *
         * Strategy (avoids copying the 102/302 MB vtdb2):
         *   1. stage/data-common  -> symlink pointing to srcDir (the flat custom{tag}/ folder).
         *      The engine opens files inside data-common/ by name; they all live in srcDir.
         *   2. stage/fixed_db/    -> directory; vtdb copied here (9 MB, manageable).
         *   3. stage/userdict/    -> directory holding userdict_kor.csv (5 KB).
         *
         * stageRoot must sit on the same filesystem as srcDir: a symlink may not cross from
         * internal storage (filesDir) into external storage, and when it is refused the caller
         * would fall back to copying the whole pack. Staging beside the pack also keeps the
         * 405 MB of voice data out of the app's internal storage.
         *
         * Returns the dbPath string to pass to nInit, or null on failure.
         */
        fun prepareDbPath(srcDir: File, stageRoot: File, tag: String): String? {
            if (!srcDir.isDirectory) {
                Log.e(TAG, "prepareDbPath: not a directory: $srcDir")
                return null
            }

            val stage = File(stageRoot, "staged$tag")
            val stageCommonLink = File(stage, DIR_DATA_COMMON)
            val stageFixed = File(stage, DIR_FIXED_DB)
            val stageUserdict = File(stage, DIR_USERDICT)

            if (!stage.isDirectory && !stage.mkdirs()) {
                Log.e(TAG, "prepareDbPath: cannot create $stage")
                return null
            }
            if (!stageFixed.isDirectory && !stageFixed.mkdirs()) {
                Log.e(TAG, "prepareDbPath: cannot create $stageFixed")
                return null
            }
            if (!stageUserdict.isDirectory && !stageUserdict.mkdirs()) {
                Log.e(TAG, "prepareDbPath: cannot create $stageUserdict")
                return null
            }

            // data-common: symlink -> srcDir (read-only, no data duplicated)
            if (!stageCommonLink.exists()) {
                try {
                    Os.symlink(srcDir.absolutePath, stageCommonLink.absolutePath)
                    Log.i(TAG, "prepareDbPath: symlink data-common -> ${srcDir.absolutePath}")
                } catch (e: Exception) {
                    Log.e(TAG, "prepareDbPath: symlink data-common failed: ${e.message}")
                    return null
                }
            }

            // fixed_db/*.vtdb: small (9 MB), copied into the staged tree
            var vtdbFound = false
            for (variant in LIB_VARIANTS) {
                val name = vtdbName(tag, variant)
                val src = File(srcDir, name)
                val dest = File(stageFixed, name)
                if (!src.isFile) continue
                vtdbFound = true
                if (!dest.isFile || dest.lastModified() < src.lastModified()) {
                    try {
                        src.copyTo(dest, overwrite = true)
                        Log.i(TAG, "prepareDbPath: copied $name (${src.length()} bytes)")
                    } catch (e: Exception) {
                        Log.e(TAG, "prepareDbPath: copy $name failed: ${e.message}")
                        return null
                    }
                }
            }
            if (!vtdbFound) {
                Log.e(TAG, "prepareDbPath: no vtdb file found in $srcDir")
                return null
            }

            // userdict/userdict_kor.csv: tiny (5 KB), copied into the staged tree
            val userdictSrc = File(srcDir, USERDICT_FILE)
            val userdictDest = File(stageUserdict, USERDICT_FILE)
            if (userdictSrc.isFile &&
                (!userdictDest.isFile || userdictDest.lastModified() < userdictSrc.lastModified())) {
                try {
                    userdictSrc.copyTo(userdictDest, overwrite = true)
                    Log.i(TAG, "prepareDbPath: copied $USERDICT_FILE")
                } catch (e: Exception) {
                    Log.w(TAG, "prepareDbPath: copy $USERDICT_FILE failed: ${e.message}")
                    // non-fatal: engine continues without user dict
                }
            }

            Log.i(TAG, "prepareDbPath: stage ready at $stage")
            return stage.absolutePath.trimEnd('/') + "/"
        }

        const val ERR_NO_SOURCE_DIR = -2000
        const val ERR_NO_LIB = -2001
        const val ERR_STAGE_FAILED = -2002

        /**
         * Loads the engine for the given speaker.
         * hdttsRoot: parent of custom12/ or custom15/ (e.g. /storage/emulated/0/hdtts).
         * The staging tree is created inside hdttsRoot so it shares a filesystem with the pack.
         */
        fun open(libDir: String, hdttsRoot: String, speakerId: Int): Int {
            val tag = if (speakerId == TtsEngine.Speaker.CUSTOM12.id) "12" else "15"
            val root = File(hdttsRoot)
            val srcDir = File(root, "custom$tag")
            if (!srcDir.isDirectory) {
                Log.e(TAG, "voice pack folder not found: ${srcDir.absolutePath}")
                return ERR_NO_SOURCE_DIR
            }
            val dbPath = prepareDbPath(srcDir, root, tag) ?: return ERR_STAGE_FAILED

            var ret = ERR_NO_LIB
            for (variant in LIB_VARIANTS) {
                val lib = File(libDir, "libvt_custom${tag}_$variant.so")
                if (!lib.exists()) continue
                if (!File(File(dbPath, DIR_FIXED_DB), vtdbName(tag, variant)).isFile) {
                    Log.w(TAG, "skipping ${lib.name}: ${vtdbName(tag, variant)} missing")
                    continue
                }
                ret = nInit(lib.absolutePath, dbPath, ENGINE_ID)
                Log.i(TAG, "open ${lib.name} db=$dbPath -> $ret")
                if (ret >= 0) return ret
            }
            return ret
        }

        fun encodeText(text: String): ByteArray {
            val cs = try { Charset.forName("x-windows-949") }
                     catch (e: Exception) { Charset.forName("EUC-KR") }
            return text.replace('\u00A0', ' ').toByteArray(cs)
        }
    }
}
