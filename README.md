# MediaZen TTS 앱

미디어젠 5w세대 TTS 엔진(`libvt_custom12_H22.so` / `libvt_custom15_H22.so`)을 루팅 없이
일반 Android 앱에서 직접 구동하는 프로젝트다.

## 구성

| 경로 | 내용 |
|---|---|
| `tts_app/app/src/main/cpp/tts_direct.cpp` | 엔진 dlopen/dlsym, LOADTTS, 합성 JNI 진입점 |
| `tts_app/app/src/main/cpp/tts_core.h` | 호스트에서 테스트 가능한 합성 루프와 WAV 헤더 |
| `tts_app/app/src/main/java/com/mediazen/ttsapp/TtsDirect.kt` | dbPath 스테이징, 엔진 로드 |
| `tts_app/tests/` | 호스트 테스트 (dbPath 레이아웃, 합성 코어) |
| `dbpath_findings.md` | dbPath 진입점 리버싱 기록 |
| `loadtts_verify.md` | `LOADTTS` 3인자 규약 검증 기록 |

## 음성 데이터 배치

기기에서 `/storage/emulated/0/hdtts/` 아래에 `custom12/` 와 `custom15/` 를 둔다.
앱은 이 디렉터리를 읽어 `staged12/` / `staged15/` 스테이징 트리를 만들고, 그 경로를
엔진의 dbPath로 넘긴다. 엔진 `.so` 는 APK의 `jniLibs` 에서만 로드한다.

## 빌드

CI(`.github/workflows/build-apk.yml`)가 유일한 빌드 경로다.
`main` 에 푸시하면 디버그 APK를 `debug-<실행번호>` 태그의 프리릴리스로 게시한다.

## 알려진 제약

- 엔진 라이브러리는 전부 ELF32 ARM이다. APK는 `armeabi-v7a` 만 포함하므로 프로세스는 32비트로 뜬다.
  x64 PC에서의 네이티브 재생 경로는 없다.
- `sP22` / `M22` 용 vtdb가 배포본에 없어 해당 변형 라이브러리는 사용할 수 없다. H22만 동작한다.
- speaker 전환은 앱 재시작이 필요하다.
