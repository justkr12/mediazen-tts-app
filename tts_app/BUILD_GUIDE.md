# TTS App Build & Run Guide

This project is a full Android Studio project designed to run the MediaZen TTS engine.

## 1. Open Project
1. Open **Android Studio**.
2. Select **Open** and navigate to `C:\tts_app`.
3. Wait for Gradle to sync (this may take several minutes as it downloads the plugins).

## 2. SDK & NDK Setup
Ensure you have the following installed in SDK Manager:
- **Android SDK Platform 34**
- **NDK (Side by side)** - Version 30.0.16248370 (or similar)
- **CMake** - Version 3.22.1

## 3. Deploy Voice Data (Crucial)
The app looks for voice databases in `/data/local/tmp/`. You MUST push these before running the app:

```bash
adb push C:\tts_build\TTS\VW\custom12 /data/local/tmp/custom12
adb push C:\tts_build\TTS\VW\custom15 /data/local/tmp/custom15
```

## 4. Run
1. Connect your **Google Play x86_64 Emulator** (or ARM device).
2. Click **Run** (Green arrow) in Android Studio.
3. Enter text and click **"음성 합성 실행"**.
4. Result will be saved to `/sdcard/tts_output.wav`.

## 5. Retrieve Result
```bash
adb pull /sdcard/tts_output.wav .
```

## Troubleshooting
- **App Crashes on Start**: Ensure all `.so` files are in `app/src/main/jniLibs/armeabi-v7a/`.
- **Synthesis Fails**: Ensure vtdb2 files are pushed to `/data/local/tmp/`.
- **Build Error**: If Gradle fails, check the "Build" tab at the bottom of Android Studio for specific errors and click the suggested "Fix" links.
