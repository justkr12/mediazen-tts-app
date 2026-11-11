package com.mediazen.ttsapp

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.widget.Button
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var editText: com.google.android.material.textfield.TextInputEditText
    private lateinit var rbCustom12: RadioButton
    private lateinit var rbCustom15: RadioButton
    private lateinit var etOutputPath: android.widget.EditText
    private lateinit var btnSynthesize: Button
    private lateinit var btnStop: Button
    private lateinit var tvStatus: TextView
    private lateinit var progressBar: ProgressBar

    private var isProcessing = false
    private var audioTrack: AudioTrack? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // ★ nativeLibraryDir을 C++로 전달 (System.loadLibrary 이후에 바로 호출)
        val libDir = applicationInfo.nativeLibraryDir
        TtsEngine.setLibDir(libDir)
        android.util.Log.i("TTS_MAIN", "nativeLibraryDir: $libDir")

        editText = findViewById(R.id.editText)
        rbCustom12 = findViewById(R.id.rbCustom12)
        rbCustom15 = findViewById(R.id.rbCustom15)
        etOutputPath = findViewById(R.id.etOutputPath)
        btnSynthesize = findViewById(R.id.btnSynthesize)
        btnStop = findViewById(R.id.btnStop)
        tvStatus = findViewById(R.id.tvStatus)
        progressBar = findViewById(R.id.progressBar)

        checkStoragePermission()

        btnSynthesize.setOnClickListener { synthesize() }
        btnStop.setOnClickListener { stopSynthesis() }

        // 버전 확인 (try-catch로 crash 방지)
        try {
            val version = StringBuilder()
            val ret = TtsEngine.ttsEngineVersion(version)
            tvStatus.text = if (ret == 0) "엔진 버전: $version" else "버전 조회 실패: $ret"
        } catch (e: Exception) {
            tvStatus.text = "엔진 초기화 오류: ${e.message}"
            android.util.Log.e("TTS_MAIN", "version error", e)
        }
    }

    private fun checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = android.content.Intent(
                        android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        android.net.Uri.parse("package:$packageName")
                    )
                    startActivity(intent)
                } catch (e: Exception) {
                    val intent = android.content.Intent(
                        android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION
                    )
                    startActivity(intent)
                }
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                    arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE), 100)
            }
        }
    }

    private fun synthesize() {
        val text = editText.text.toString().trim()
        if (text.isEmpty()) {
            Toast.makeText(this, "텍스트를 입력하세요", Toast.LENGTH_SHORT).show()
            return
        }

        val speakerId = if (rbCustom12.isChecked) TtsEngine.Speaker.CUSTOM12.id else TtsEngine.Speaker.CUSTOM15.id
        // hdttsRoot: custom12/ 또는 custom15/ 의 상위 폴더. 엔진은 여기에
        // data-common/, fixed_db/, userdict/ 를 붙여서 DB를 찾는다.
        val hdttsRoot = "/storage/emulated/0/hdtts"

        val outputPath = etOutputPath.text.toString().trim()
        if (outputPath.isEmpty()) {
            Toast.makeText(this, "출력 경로를 입력하세요", Toast.LENGTH_SHORT).show()
            return
        }

        val tag = if (speakerId == TtsEngine.Speaker.CUSTOM12.id) "12" else "15"
        val packDir = File("${hdttsRoot.trimEnd('/')}/custom$tag")
        if (!packDir.isDirectory) {
            Toast.makeText(this, "보이스팩 폴더를 찾을 수 없습니다: ${packDir.absolutePath}", Toast.LENGTH_LONG).show()
            tvStatus.text = "폴더 없음: ${packDir.absolutePath}"
            return
        }

        setProcessing(true)
        tvStatus.text = "초기화 중..."

        Thread {
            try {
                runOnUiThread { tvStatus.text = "엔진 초기화: ${packDir.absolutePath} (speakerId=$speakerId)" }
                val retOpen = TtsDirect.open(applicationInfo.nativeLibraryDir, hdttsRoot, speakerId)
                if (retOpen < 0) {
                    val reason = describeOpenError(retOpen)
                    android.util.Log.e("TTS_MAIN", "open failed: $retOpen ($reason)")
                    runOnUiThread {
                        tvStatus.text = "엔진 초기화 실패: $retOpen\n$reason"
                        setProcessing(false)
                    }
                    return@Thread
                }

                runOnUiThread { tvStatus.text = "음성 합성 중..." }
                // Pass -1,-1,-1 as the last three engine arguments (confirmed from HKMC_Navi5W_phone.apk).
                val pcm = TtsDirect.nSynth(TtsDirect.encodeText(text), -1, -1, -1)
                if (pcm == null) {
                    val status = TtsDirect.nLastStatus()
                    runOnUiThread {
                        tvStatus.text = "합성 실패: $status"
                        setProcessing(false)
                    }
                    return@Thread
                }

                // Play raw PCM via AudioTrack (22050 Hz, mono, PCM_16BIT, MODE_STATIC).
                // Also save WAV to outputPath for inspection.
                try {
                    val outFile = File(outputPath)
                    outFile.parentFile?.mkdirs()
                    outFile.writeBytes(buildWavHeader(pcm.size) + pcm)
                } catch (e: Exception) {
                    android.util.Log.w("TTS_MAIN", "wav save failed: ${e.message}")
                }

                runOnUiThread {
                    tvStatus.text = "완료: ${pcm.size} bytes PCM"
                    setProcessing(false)
                    playPcm(pcm)
                }
            } catch (e: Exception) {
                runOnUiThread {
                    tvStatus.text = "오류: ${e.message}"
                    setProcessing(false)
                }
                android.util.Log.e("TTS_MAIN", "synthesize error", e)
            }
        }.start()
    }

    /**
     * Turns an error code from TtsDirect.open into something actionable on screen.
     * Codes below -1000 come from our own staging code; the rest are returned by the engine.
     */
    private fun describeOpenError(code: Int): String = when (code) {
        TtsDirect.ERR_NO_SOURCE_DIR ->
            "보이스팩 폴더가 없습니다 (경로/권한 확인)"
        TtsDirect.ERR_STAGE_FAILED ->
            "DB 구조 준비 실패 (fixed_db/*.vtdb 누락 또는 링크/복사 실패)"
        TtsDirect.ERR_NO_LIB ->
            "libvt_custom*.so 와 일치하는 .vtdb 조합을 찾지 못했습니다"
        -1000 -> "엔진 라이브러리 로드 실패 (dlopen)"
        -1001 -> "엔진 심볼 조회 실패 (dlsym)"
        -1002 -> "다른 엔진이 이미 로드됨 (앱 재시작 필요)"
        else -> "엔진 오류 코드 (logcat의 TTS_DIRECT 태그 확인)"
    }

    // Plays raw 22050 Hz mono 16-bit PCM bytes directly via AudioTrack (MODE_STATIC).
    // Matches the play() method in com.hkport.Vt from HKMC_Navi5W_phone.apk.
    private fun playPcm(pcm: ByteArray) {
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        try {
            val attrs = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build()
            val format = AudioFormat.Builder()
                .setSampleRate(22050)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                .build()
            val track = AudioTrack.Builder()
                .setAudioAttributes(attrs)
                .setAudioFormat(format)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .setBufferSizeInBytes(pcm.size)
                .build()
            track.write(pcm, 0, pcm.size)
            audioTrack = track
            track.play()
        } catch (e: Exception) {
            android.util.Log.e("TTS_MAIN", "playPcm error", e)
            tvStatus.text = "재생 실패: ${e.message}"
        }
    }

    // Builds a minimal 44-byte WAV header for 22050 Hz mono 16-bit PCM.
    private fun buildWavHeader(pcmSize: Int): ByteArray {
        val h = ByteArray(44)
        fun le32(v: Int, o: Int) { for (i in 0..3) h[o+i] = (v shr (i*8)).toByte() }
        fun le16(v: Int, o: Int) { h[o] = (v and 0xff).toByte(); h[o+1] = (v shr 8).toByte() }
        "RIFF".toByteArray().copyInto(h, 0)
        le32(36 + pcmSize, 4)
        "WAVEfmt ".toByteArray().copyInto(h, 8)
        le32(16, 16); le16(1, 20); le16(1, 22); le32(22050, 24)
        le32(44100, 28); le16(2, 32); le16(16, 34)
        "data".toByteArray().copyInto(h, 36)
        le32(pcmSize, 40)
        return h
    }

    override fun onDestroy() {
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        super.onDestroy()
    }

    private fun stopSynthesis() {
        audioTrack?.pause()
        audioTrack?.flush()
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        TtsEngine.ttsEngineInterrupt()
        tvStatus.text = "중지 요청됨"
        setProcessing(false)
    }

    private fun setProcessing(processing: Boolean) {
        isProcessing = processing
        runOnUiThread {
            btnSynthesize.isEnabled = !processing
            btnStop.isEnabled = processing
            btnStop.alpha = if (processing) 1f else 0.5f
            progressBar.visibility = if (processing) android.view.View.VISIBLE else android.view.View.GONE
        }
    }
}
