package com.mediazen.ttsapp

import android.system.Os
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
         * Directory layout the engine builds internally from the dbPath string it is given.
         * These are literal path fragments taken from libvt_custom12_*.so / libvt_custom15_*.so:
         *   "fixed_db/"          + <db file>            -> speaker model
         *   "%sdata-common/"     + <common db files>    -> common dictionaries
         *   "userdict/userdict_kor.csv"                 -> user dictionary
         */
        const val DIR_DATA_COMMON = "data-common"
        const val DIR_FIXED_DB = "fixed_db"
        // userdict_kor.csv lives directly under dbPath, not in a subdirectory.
        // Confirmed by CUSTOM12_LOADTTS: sprintf(path, "%s%s", dbPath, "userdict_kor.csv")
        const val USERDICT_FILE = "userdict_kor.csv"

        /** The speaker model file the engine looks for, e.g. tts_single_db_kor_custom12_H22.vtdb. */
        fun vtdbName(tag: String, variant: String) = "tts_single_db_kor_custom${tag}_$variant.vtdb"

        /** True when the file is the speaker model (fixed DB) rather than a common DB file. */
        private fun isFixedDb(name: String): Boolean =
            name.startsWith("tts_single_db_") && name.endsWith(".vtdb")

        /**
         * Prepares the directory structure the engine expects under dbPath.
         * Paths confirmed by disassembling CUSTOM12_LOADTTS and load_CommonDB:
         *   {dbPath}fixed_db/tts_single_db_kor_custom{tag}_{variant}.vtdb
         *   {dbPath}data-common/            <- vtdb2, Diclist_*.txt, HMC_*.cps, etc.
         *   {dbPath}userdict_kor.csv        <- directly under dbPath, no subdirectory
         *
         * The engine concatenates these names onto dbPath itself, so the tree must exist on
         * disk even though the voice pack ships as a flat folder. Because the pack holds a
         * 102 MB / 302 MB vtdb2, a staging directory of symlinks is built under stageRoot
         * instead of copying. stageRoot should sit on the same filesystem as srcDir so the
         * links resolve; when symlinks are refused the file is copied as a fallback.
         *
         * When srcDir already contains all required entries it is used as-is.
         *
         * Returns the dbPath to pass to the engine, or null on failure.
         */
        fun prepareDbPath(srcDir: File, stageRoot: File, tag: String): String? {
            if (!srcDir.isDirectory) {
                Log.e(TAG, "prepareDbPath: not a directory: $srcDir")
                return null
            }

            // Already structured correctly: point the engine straight at the source folder.
            if (File(srcDir, DIR_DATA_COMMON).isDirectory &&
                File(srcDir, DIR_FIXED_DB).isDirectory &&
                File(srcDir, USERDICT_FILE).isFile
            ) {
                Log.i(TAG, "prepareDbPath: $srcDir already has the expected layout")
                return srcDir.absolutePath.trimEnd('/') + "/"
            }

            // Build a staging tree beside the pack, never over it.
            val stage = File(stageRoot, "staged$tag")
            val stageCommon = File(stage, DIR_DATA_COMMON)
            val stageFixed = File(stage, DIR_FIXED_DB)
            for (d in arrayOf(stageCommon, stageFixed)) {
                if (!d.isDirectory && !d.mkdirs()) {
                    Log.e(TAG, "prepareDbPath: cannot create ${d.absolutePath}")
                    return null
                }
            }

            var linked = 0
            var copied = 0
            var copiedBytes = 0L
            var failed = 0
            srcDir.listFiles()?.forEach { src ->
                // Skip anything that is not a plain file so the staging tree stays flat.
                if (!src.isFile) return@forEach

                val name = src.name
                val dest: File = when {
                    isFixedDb(name) -> File(stageFixed, name)
                    // userdict_kor.csv goes directly under stage/, not in a subdirectory.
                    name == USERDICT_FILE -> File(stage, name)
                    else -> File(stageCommon, name)
                }

                // Re-link when the source is newer, so a refreshed voice pack is picked up.
                if (dest.exists() && dest.lastModified() >= src.lastModified()) return@forEach

                when (linkOrCopy(src, dest)) {
                    LinkResult.LINKED -> linked++
                    LinkResult.COPIED -> { copied++; copiedBytes += src.length() }
                    LinkResult.FAILED -> failed++
                }
            }

            // A copy fallback duplicates the pack, which for the vtdb2 is 102 MB / 302 MB.
            // Warn rather than fail: a partial pack may still be small enough to fit.
            if (copied > 0) {
                val free = stage.usableSpace
                if (free in 1 until copiedBytes) {
                    Log.w(TAG, "prepareDbPath: copied $copiedBytes bytes with only $free free on $stage")
                }
            }

            // The engine cannot start without a speaker model, so fail loudly here instead of
            // letting it return an opaque negative code later. Any known variant is acceptable:
            // open() picks the library whose variant matches a DB that is actually present.
            val present = stageFixed.listFiles()?.map { it.name }?.sorted() ?: emptyList()
            if (present.none { it == vtdbName(tag, "H22") || it == vtdbName(tag, "sP22") }) {
                Log.e(TAG, "prepareDbPath: no tts_single_db_kor_custom${tag}_*.vtdb in " +
                    "${stageFixed.absolutePath} (found: ${present.joinToString()})")
                return null
            }

            Log.i(TAG, "prepareDbPath: staged $stage (linked=$linked copied=$copied failed=$failed)")
            return stage.absolutePath.trimEnd('/') + "/"
        }

        private enum class LinkResult { LINKED, COPIED, FAILED }

        /**
         * Creates dest as a symlink to src, falling back to a copy when the filesystem does not
         * allow symlinks (some external storage mounts and Android 15 app dirs reject them).
         * Os.symlink is used rather than java.nio.file.Files so minSdk 21 stays supported.
         */
        private fun linkOrCopy(src: File, dest: File): LinkResult {
            try {
                if (dest.exists()) dest.delete()
                Os.symlink(src.absolutePath, dest.absolutePath)
                return LinkResult.LINKED
            } catch (e: Exception) {
                Log.w(TAG, "symlink failed for ${src.name} (${e.javaClass.simpleName}: ${e.message}), copying")
            }
            return try {
                src.copyTo(dest, overwrite = true)
                LinkResult.COPIED
            } catch (e: Exception) {
                Log.e(TAG, "copy ${src.name} failed: ${e.message}")
                LinkResult.FAILED
            }
        }

        /** Error codes returned by open() when it fails before reaching the engine. */
        const val ERR_NO_SOURCE_DIR = -2000
        const val ERR_NO_LIB = -2001
        const val ERR_STAGE_FAILED = -2002

        /**
         * Loads the engine for the given speaker.
         * hdttsRoot is the parent of custom12/ or custom15/ (e.g. /storage/emulated/0/hdtts).
         * The staging tree is created inside hdttsRoot so it shares a filesystem with the voice
         * pack: symlinks resolve, and the 102 MB / 302 MB vtdb2 never lands in internal storage.
         * Tries each library variant in order and returns the first non-negative LOADTTS result,
         * otherwise the last error code.
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
            var tried = 0
            for (variant in LIB_VARIANTS) {
                val lib = File(libDir, "libvt_custom${tag}_$variant.so")
                if (!lib.exists()) continue
                // The engine derives the fixed DB name from its own variant, so the matching
                // .vtdb has to be present or LOADTTS fails with an opaque code.
                if (!File(File(dbPath, DIR_FIXED_DB), vtdbName(tag, variant)).isFile) {
                    Log.w(TAG, "skipping ${lib.name}: ${vtdbName(tag, variant)} is missing")
                    continue
                }
                tried++
                ret = nInit(lib.absolutePath, dbPath, ENGINE_ID)
                Log.i(TAG, "open ${lib.name} db=$dbPath -> $ret")
                if (ret >= 0) return ret
            }
            if (tried == 0) Log.e(TAG, "no usable libvt_custom${tag}_*.so + matching .vtdb pair found")
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
