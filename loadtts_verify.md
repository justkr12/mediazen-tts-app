# CUSTOM12/15_LOADTTS 3인자 규약 검증

검증 대상: `tts_app/app/src/main/cpp/tts_direct.cpp`의 `g_load(db, kEngineVersion, kUserDictPrefix)` 호출과
`tts_app/app/src/main/cpp/tts_core.h`의 상수 정의.

검증 방식: `libvt_custom12_H22.so` / `libvt_custom15_H22.so`를 `arm-linux-gnueabihf-objdump`로 직접
디스어셈블하고, 리터럴 풀 슬롯의 원시 바이트를 읽어 주소를 재계산했다. 기존 문서(`dbpath_findings.md`)의
결론을 그대로 인용하지 않고 전부 다시 유도했다.

## 0. 섹션 매핑 (먼저 확인한 사실)

`libvt_custom12_H22.so`는 `.text` vaddr `0x176b8` / off `0x176b8`, `.rodata` vaddr `0x5d318` / off `0x5d318`로
**동일(델타 0)** 이다. 따라서 `.text`/`.rodata` 안에서는 vaddr을 파일 오프셋으로 그대로 쓸 수 있다.
`.data.rel.ro.local`만 vaddr `0x68810` / off `0x67810`으로 `0x1000` 차이가 난다.

## 1. Thumb 리터럴 풀 계산 규칙

이 프로젝트에서 세 번의 오독을 만든 지점이므로 규칙을 먼저 고정한다.

- `ldr rX, [pc, #N]`: 슬롯 주소 = `((ldr주소 + 4) & ~3) + N`
- `add rX, pc`: 사용되는 `pc` = **그 add 명령어 주소 + 4**

두 값은 서로 다르다. `objdump`가 `@ (17868 <...>)`처럼 슬롯 주소를 직접 표기하므로 반드시 대조한다.

## 2. CUSTOM12_LOADTTS 진입부 (0x177a4)

```
177a4: stmdb sp!, {r4, r5, r6, r7, r8, lr}
177a8: mov   r8, r2        ; arg3 -> r8
177b0: mov   r6, r0        ; arg1 -> r6
177ba: mov   r7, r1        ; arg2 -> r7
177c0: str   r1, [sp, #0]  ; arg2를 로그용으로 스택에 보관
```

`r3`를 인자로 읽는 코드가 없고, `[sp, #280]` 이상(들어오는 스택 인자)을 읽는 코드도 없다.
**인자는 정확히 3개다. CONFIRMED.**

## 3. 인자 2 = 버전 문자열 (비교 대상 리터럴)

```
177ce: ldr r0, [pc, #152]   @ (17868)   ; 슬롯 0x17868, 값 0x00045bb4
177d0: mov r1, r7                        ; r1 = arg2
177d4: add r0, pc                        ; pc = 0x177d8 -> 0x177d8 + 0x45bb4 = 0x5d38c
177d6: bl  compare_str_IgnoreWhiteSpace
177dc: cmp r0, #0
177de: bne.n 1783e                       ; 불일치 -> 실패 반환
```

슬롯 `0x17868`의 원시 바이트는 `b45b0400` = `0x00045bb4`이다.
`0x5d38c`의 문자열은 `TTS.KOK.L-NRv24.101.240603-L2voice` (34바이트). **CONFIRMED.**

대조: `0x5d318`의 `VW_Custom12`는 슬롯 `0x17860`(`0x00045b5c`)에서 `add@0x177b8`(base `0x177bc`)로
계산되어 `r5`에 들어가며, `__android_log_print`의 **태그**로만 쓰인다. 비교 대상이 아니다.
두 리터럴은 `0x74`바이트 떨어져 있어 혼동하기 쉽다.

같은 함수의 로그 포맷(`0x17864` 슬롯, `0x5d324`)도 이 버전 문자열을 포함한다:

```
VwTtsEngine::CUSTOM12SMS_LOADTTS(TTS.KOK.L-NRv24.101.240603-L2voice): dbpath : %s, wrapperVersion : %s
```

즉 이 문자열은 로그와 비교 양쪽에서 동일하게 쓰인다.

## 4. 비교 실패 반환값 = -300

```
177de: bne.n 1783e
1783e: ldr r0, [pc, #24]   @ (17858)
17840: b.n   17844
```

슬롯 `0x17858`의 원시 바이트는 `d4feffff` = `0xfffffed4` = **-300**. **CONFIRMED.**
`3`이 아니다. 이전에 관측된 `3`은 이 경로의 반환값이 아니다.

## 5. 성공 경로의 반환값

```
177f8: bl    VT_LOADTTS_EXT_KOR
177fc: mov   r4, r0
177fe: cbnz  r0, 17842        ; 0이 아니면 그 값을 그대로 반환
17800: movs  r0, #1
...
17842: mov   r0, r4
17844: ... 17852: ldmia.w sp!, {r4, r5, r6, r7, r8, pc}
```

`VT_LOADTTS_EXT_KOR`가 `0`을 반환하면 그대로 `0`이 반환된다. 따라서 **성공 판정은 `ret == 0`이 맞다.**
기존의 `ret >= 0`은 틀렸다. **CONFIRMED.**

## 6. 인자 3 = user-dict 경로 접두어

```
1780e: mov  r0, r6          ; sprintf 목적지 = arg1 (dbPath)
17810: add  r1, pc          ; fmt
1780c: mov  r2, r8          ; arg3
17812: add  r3, pc          ; 접미사
17814: blx  sprintf
17826: mov  r1, r6          ; 완성된 경로
17828: mov  r0, r4          ; 엔진 인스턴스
1782a: bl   VT_LOAD_UserDict_KOR
```

`objdump`가 표기한 슬롯을 기준으로 정확한 문자열:

- fmt 슬롯 `0x17870`, `add@0x17812` → base `0x17816` → **`0x5d587` = `"%s%s"`**
- 접미사 슬롯 `0x17874`, `add@0x1781e` → base `0x17822` → **`0x5d58c` = `"userdict_kor.csv"`**

rodata 원시 바이트로도 확인된다:

```
5d580: 4949433d 303a00 | 25732573 00 | 75736572 64696374 5f6b6f72 2e637376 00
       "IIC=0:"          "%s%s"        "userdict_kor.csv"
```

따라서 엔진이 여는 경로는 정확히 **`dbPath + arg3 + "userdict_kor.csv"`** 이다. **CONFIRMED.**

참고: 이전 분석에서 fmt를 `0x5d589`, 접미사를 `0x5d598`로 적은 적이 있으나 이는 2바이트 어긋난 값이다.
올바른 값은 `0x5d587` / `0x5d58c`이며, `0x5d589`는 `"%s%s"`의 두 번째 바이트 위치다.

rodata에 존재하는 userdict 관련 문자열은 다음 셋뿐이다:

| 주소 | 문자열 |
|---|---|
| `0x5d58c` | `userdict_kor.csv` |
| `0x60680` | `userdict/userdict_kor.csv` |
| `0x6b16` | `userdict` |

`0x60680`의 `userdict/userdict_kor.csv`는 **`userdict/` 하위 디렉터리가 실제로 존재함**을 뒷받침한다.
우리 스테이징 트리가 `stage/userdict/userdict_kor.csv`를 만들고 `dbPath`를 `stage/`로 넘기므로,
`arg3 = "userdict/"`가 정확히 그 파일을 가리킨다. **CONFIRMED.**

`dbPath`는 Kotlin 쪽에서 `trimEnd('/') + "/"`로 끝나므로 최종 문자열은
`/storage/emulated/0/hdtts/staged12/userdict/userdict_kor.csv` 형태가 되며 이중 슬래시는 생기지 않는다.

## 7. 라이선스 문자열은 인자가 아니다

`0x177e8` 슬롯(`0x1786c`, `add@0x177ec`, base `0x177f0`)의 값 `0x00045bbf`는 `0x5d3af`를 가리키며,
그곳에는 `License:bb3e97f4...<vw_verify .../>...IIC=0:` 형태의 라이선스 블롭이 있다.
이 값은 `str r2, [sp, #8]`로 `VT_LOADTTS_EXT_KOR`의 스택 인자로 전달된다.

즉 라이선스는 **엔진 내부에 이미 내장되어 있어 호출자가 넘길 필요가 없다.** **CONFIRMED.**

## 8. CUSTOM15 동일성

`libvt_custom15_H22.so`:

- `0x177ce: ldr r0, [pc, #152] @ (17868)` — 동일한 슬롯
- `0x177d6: bl 17728 <compare_str_IgnoreWhiteSpace>` — 동일한 비교
- `0x1783e: ldr r0, [pc, #24] @ (17858)` — 동일한 실패 경로
- 슬롯 `0x17858` 바이트 `d4feffff` = **-300**, 동일
- `0x5d38c` = `TTS.KOK.L-NRv24.101.240603-L2voice`, **바이트 동일**

**CONFIRMED.** 두 라이브러리에 같은 인자 규약을 써도 된다.

## 9. libhkport.so의 호출은 참고가 되지 않는다

`libhkport.so`는 ARM 모드(4바이트 명령어)이며 `Java_com_hkport_Vt_nInit`에서:

```
1fa0: mov r0, r8     ; engineId만 설정
1fa8: blx r2         ; CUSTOM12_LOADTTS
```

`r1`/`r2`를 설정하는 코드가 없다. ARM 모드에서는 `r0`/`r1`/`r2`가 독립이므로 `r1`/`r2`는 `dlsym`
호출이 남긴 값이 그대로 들어간다. 따라서 이 호출은 `arg2` 비교에 실패해 **-300을 반환**한다.

`libhkport.so`는 dlopen/dlsym/TextToBuffer **순서**의 참고일 뿐, 인자 규약의 참고가 될 수 없다.
**CONFIRMED.**

## 10. 결론

현재 커밋된 코드는 검증된 사실과 일치한다.

| 항목 | 코드 | 판정 |
|---|---|---|
| 인자 개수 | `loadtts_t(db_path, version, user_dict_prefix)` | 일치 |
| 인자 2 값 | `kEngineVersion = "TTS.KOK.L-NRv24.101.240603-L2voice"` (34바이트) | 일치 |
| 인자 3 값 | `kUserDictPrefix = "userdict/"` | 일치 |
| 성공 판정 | `ret == 0` | 일치 |
| 실패 코드 | `kLoadTtsVersionMismatch = -300` | 일치 |

남은 미확정 사항은 없다. `LOADTTS` 이후 `TextToBuffer`가 `-6`을 반환하던 원인은
`VT_LOADTTS_EXT_KOR`가 호출되지 않아 speaker 82 인스턴스가 생성되지 않았기 때문이며,
본 수정으로 그 경로가 열린다. 실제 PCM 생성 여부는 기기에서 확인해야 한다.
