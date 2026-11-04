<#
.SYNOPSIS
    Build script for TTS Android App
#>

$ErrorActionPreference = "Stop"

$projectDir = "C:\tts_app"
$gradleCmd = "cmd /c gradlew.bat"

Write-Host "=== TTS Android App Build ===" -ForegroundColor Cyan
Write-Host "Project: $projectDir"

if (-not (Test-Path "$projectDir\gradlew.bat")) {
    Write-Host "Generating Gradle wrapper..." -ForegroundColor Yellow
    cmd /c "cd /d $projectDir && gradle wrapper --gradle-version 8.6"
}

Write-Host "Building APK..." -ForegroundColor Cyan
cmd /c "cd /d $projectDir && gradlew.bat assembleDebug"

$apkPath = "$projectDir\app\build\outputs\apk\debug\app-debug.apk"
if (Test-Path $apkPath) {
    Write-Host ""
    Write-Host "=== Build Successful ===" -ForegroundColor Green
    Write-Host "APK: $apkPath"
    Write-Host ""
    Write-Host "Install on emulator/device:"
    Write-Host "  adb install -r $apkPath"
    Write-Host ""
    Write-Host "Before running app, push vtdb2 folders:"
    Write-Host "  adb push C:\tts_build\TTS\VW\custom12 /data/local/tmp/custom12"
    Write-Host "  adb push C:\tts_build\TTS\VW\custom15 /data/local/tmp/custom15"
} else {
    Write-Error "APK not found"
    exit 1
}