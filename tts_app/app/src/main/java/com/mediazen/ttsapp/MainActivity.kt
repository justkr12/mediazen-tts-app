package com.mediazen.ttsapp

import android.Manifest
import android.content.pm.PackageManager
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
        // ★ DB 경로: /hdtts/custom12/ (엔진이 내부적으로 /custom12/를 붙이므로 상위 폴더만)
        val dbPath = if (speakerId == TtsEngine.Speaker.CUSTOM12.id)
            "/storage/emulated/0/hdtts"
        else
            "/storage/emulated/0/hdtts"

        val outputPath = etOutputPath.text.toString().trim()
        if (outputPath.isEmpty()) {
            Toast.makeText(this, "출력 경로를 입력하세요", Toast.LENGTH_SHORT).show()
            return
        }

        val dbDir = File(dbPath)
        if (!dbDir.exists()) {
            Toast.makeText(this, "보이스팩 폴더를 찾을 수 없습니다: $dbPath", Toast.LENGTH_LONG).show()
            tvStatus.text = "폴더 없음: $dbPath"
            return
        }

        setProcessing(true)
        tvStatus.text = "초기화 중..."

        Thread {
            val frameSize = intArrayOf(0)
            var engineOpened = false

            try {
                runOnUiThread { tvStatus.text = "엔진 열기: $dbPath (speakerId=$speakerId)" }
                val retOpen = TtsEngine.ttsEngineOpen(0, dbPath, speakerId, frameSize, 0)
                if (retOpen != 0) {
                    runOnUiThread {
                        tvStatus.text = "엔진 열기 실패: $retOpen"
                        setProcessing(false)
                    }
                    return@Thread
                }
                engineOpened = true

                runOnUiThread { tvStatus.text = "WAV 출력 활성화: $outputPath" }
                // 엔진의 tts_engine_wavedrop 반환값은 상태 코드가 아니므로(디스어셈블로 확인) 무시한다.
                TtsEngine.ttsEngineWaveDrop(true, outputPath)

                runOnUiThread { tvStatus.text = "음성 합성 중..." }
                val retTts = TtsEngine.ttsEngineTts(0, text)

                TtsEngine.ttsEngineWaveDrop(false, outputPath)

                runOnUiThread {
                    tvStatus.text = if (retTts == 0)
                        "완료: $outputPath (frameSize: ${frameSize[0]})"
                    else
                        "합성 실패: $retTts"
                    setProcessing(false)
                    if (retTts == 0) {
                        Toast.makeText(this@MainActivity, "파일 저장됨: $outputPath", Toast.LENGTH_LONG).show()
                    }
                }
            } catch (e: Exception) {
                runOnUiThread {
                    tvStatus.text = "오류: ${e.message}"
                    setProcessing(false)
                }
                android.util.Log.e("TTS_MAIN", "synthesize error", e)
            } finally {
                if (engineOpened) {
                    try { TtsEngine.ttsEngineClose() } catch (e: Exception) {
                        android.util.Log.e("TTS_MAIN", "close error", e)
                    }
                }
            }
        }.start()
    }

    private fun stopSynthesis() {
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
