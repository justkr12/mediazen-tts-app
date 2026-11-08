# dbPath 가 엔진에 도달하는 경로 분석 (libvt_custom12_H22.so)

작성: engine-re (task-1). 대상: `tts_app/app/src/main/jniLibs/armeabi-v7a/libvt_custom12_H22.so`
(656516 바이트, ELF32 ARM, EABI5, little-endian). 참조: `.hktmp/lib/armeabi-v7a/libhkport.so`.

모든 주소는 `.so` 가상 주소(vaddr)이다. 문자열은 전부 파일에서 직접 디코드했고,
vaddr→파일오프셋 매핑을 먼저 검증한 뒤 읽었다(4절).

---

## 0. 결론 요약

**현재 `tts_direct.cpp` 의 2인자 호출은 틀렸다. 정답은 3인자이며, dbPath 는 첫 번째 인자다.**

```
CUSTOM12_LOADTTS(const char* dbPath, const char* version, const char* userDictPrefix)
```

단, **dbPath 를 r0 으로 옮기는 것만으로는 부족하다.** 현재 코드는 이미 r0=db 이지만,
**2번째 인자(r1)가 `"TTS.KOK.L-NRv24.101.240603-L2voice"`(34바이트)와 일치해야 한다.**
현재는 r1 에 engineId 가 들어가므로 0x177d6 의 비교가 실패하고,
엔진은 0x1783e 로 점프해 **-300** 을 반환한다. dbPath 는 전혀 사용되지 않는다.

| 인자 | 레지스터 | 역할 |
|---|---|---|
| 1 | r0 | **dbPath** (base 디렉터리) |
| 2 | r1 | **버전 문자열** — 정확히 `"TTS.KOK.L-NRv24.101.240603-L2voice"` 여야 함 |
| 3 | r2 | **유저 사전 접두어** — 뒤에 `"userdict_kor.csv"` 가 붙음 |

> 주의: arg1(dbPath)과 arg2(버전)를 혼동하면 안 된다. 비교에 쓰이는 것은 **arg2** 이고,
> 엔진에 dbPath 로 전달되는 것은 **arg1** 이다(0x177f6 `mov r2, r6`, r6 = arg1).

---

## 1. CUSTOM12_LOADTTS 의 실제 시그니처 (CONFIRMED)

`libvt_custom12_H22.so`, 함수 시작 **0x177a4**.

| 주소 | 명령 | 의미 |
|---|---|---|
| 0x177a8 | `mov r8, r2` | r8 = arg3 |
| 0x177b0 | `mov r6, r0` | r6 = arg1 |
| 0x177ba | `mov r7, r1` | r7 = arg2 |
| 0x177c8 | `mov r3, r6` | 로그에 arg1 을 넘김 |
| 0x177d0 | `mov r1, r7` | 비교 함수의 인자 = arg2 |
| 0x177d6 | `bl compare_str_IgnoreWhiteSpace` | r0 = 리터럴, r1 = arg2 |
| 0x177dc | `cmp r0, #0` / 0x177de `bne 1783e` | 0이 아니면(불일치) 실패 분기 |
| 0x177ea | `movs r1, #82` | speakerId = 82 |
| 0x177f6 | `mov r2, r6` | **r2 = arg1** |
| 0x177f8 | `bl VT_LOADTTS_EXT_KOR` | 위 r2 가 dbPath 로 전달됨 |
| 0x1780c | `mov r2, r8` | sprintf 의 %s = arg3 |

### 인자 개수 = 3 (CONFIRMED)
0x177a4~0x17858 구간 전체를 훑어 **r3 를 인자로 읽는 곳이 없고,
`[sp, #280]` 이상 스택 인자를 읽는 곳도 없다.** 따라서 정확히 r0/r1/r2 3개다.

### 인자 역할
- **arg1 (r0) = dbPath** — 0x177f6 에서 r2 로 옮겨져 `VT_LOADTTS_EXT_KOR` 의 3번째 인자가 된다.
- **arg2 (r1) = 버전/엔진 식별 문자열** — 0x177d0 에서 비교 함수의 두 번째 인자로 들어간다.
- **arg3 (r2) = 유저 사전 파일명 조각** — 0x1780c 에서 `sprintf` 의 `%s` 로만 쓰인다.

---

## 2. 질문 1: 0x177d6 에서 비교하는 리터럴 (CONFIRMED)

- `0x177ce`: `ldr r0, [pc, #152]` → 풀 슬롯 **0x17868** (내용 `0x00045bb4`)
- `0x177d4`: `add r0, pc` — 이때 **pc = 0x177d4 + 4 = 0x177d8** (Thumb 규칙)
- 결과 vaddr = `0x177d8 + 0x45bb4 = 0x5d38c`

파일에서 직접 디코드한 바이트:

```
0x5d38c: T T S . K O K . L - N R v 2 4 . 1 0 1 . 2 4 0 6 0 3 - L 2 v o i c e \0
```

**리터럴 = `"TTS.KOK.L-NRv24.101.240603-L2voice"` (34 바이트).**

### 정정: `"VW_Custom12"` 는 이 비교 대상이 아니다
`"VW_Custom12"` 는 vaddr **0x5d318** 에 있고, 그건 `r5`(로그 태그)가 가리키는 값이다:
- `0x177ae`: `ldr r5, [pc, #176]` → 슬롯 0x17860 (`0x00045b5c`)
- `0x177b8`: `add r5, pc` — **pc = 0x177b8 + 4 = 0x177bc**
- `0x177bc + 0x45b5c = 0x5d318` → `"VW_Custom12"` (11 바이트)

즉 0x5d318 은 **`__android_log_print` 의 태그**이고, 0x5d38c 가 비교 리터럴이다.
두 리터럴이 0x74 바이트 떨어져 있어 슬롯을 하나 잘못 짚으면 `"VW_Custom12"` 를 읽게 된다.

`compare_str_IgnoreWhiteSpace`(0x17728)는 양쪽에서 0x01~0x1f 범위 문자(공백/제어문자)를
걸러낸 뒤 `strcmp`(0x1778c)를 호출하고 그 결과를 그대로 반환한다. 즉 **공백 무시 완전 일치**를 요구한다.

---

## 3. 질문 2: 비교 실패 분기 0x1783e (CONFIRMED)

```
177dc: cmp  r0, #0
177de: bne.n 1783e        ; compare != 0  -> 실패
1783e: ldr  r0, [pc, #24] @ (17858)
17840: b.n  17844         ; 반환
```

풀 슬롯 **0x17858** 의 워드 = `0xfffffed4`.
- `0x1783e`: `ldr r0, [pc, #24]` — objdump 표기 주소 0x17858, 실제 pc = 0x1783e + 4 = **0x17842**,
  `0x17842 + 24 = 0x1785a` → 정렬 → **0x17858**.
- 슬롯 자체는 로드 대상이 아니라 **리터럴 값**이다. `0xfffffed4` = **-300**.

**비교 실패 시 반환값 = -300 (0xFFFFFED4).** (task 설명의 "observed as 3" 은 -300 을
부호 없는 바이트/`%d` 혼동으로 본 값으로 보인다. 여기서는 -300 으로 확정한다.)

### 질문 2-b: 3번째 인자(arg3)는 사전 파일명이다 (CONFIRMED)

성공 경로 0x17800~0x17814:

| 주소 | 명령 | 의미 |
|---|---|---|
| 0x17802 | `add r6, sp, #20` | r6 = 지역 버퍼 (280바이트 프레임 안) |
| 0x17808 | `ldr r1, [pc,#100]` @0x17870 (`0x00045d73`) | fmt |
| 0x1780a | `ldr r3, [pc,#104]` @0x17874 (`0x00045d76`) | 조각 |
| 0x1780c | `mov r2, r8` | r2 = **arg3** |
| 0x17810 | `add r1, pc` — pc=0x17814 → `0x17814+0x45d73 = 0x5d587` | **`"%s%s"`** |
| 0x17812 | `add r3, pc` — pc=0x17816 → `0x17816+0x45d76 = 0x5d58c` | **`"userdict_kor.csv"`** |
| 0x17814 | `blx sprintf` | `sprintf(r6, "%s%s", arg3, "userdict_kor.csv")` |
| 0x17826 | `mov r1, r6` / 0x17828 `mov r0, r4` | |
| 0x1782a | `bl VT_LOAD_UserDict_KOR` | `VT_LOAD_UserDict_KOR(ret, r6)` |

로그 문자열도 같은 결론을 뒷받침한다:
- 0x1781e `add r2, pc` (pc=0x17822) + `0x45d7b` = **0x5d59d** → `"VwTtsEngine UserDict load userDictPath = %s\n"`
- 0x17832 `add r2, pc` (pc=0x17836) + `0x45d94` = **0x5d5ca** → `"VwTtsEngine UserDict load UserRtn = %d\n"`

**arg3 = 유저 사전 경로 접두어**이며, 뒤에 `"userdict_kor.csv"` 가 그대로 이어 붙는다.
`VT_LOAD_UserDict_KOR`(0x1c99c)는 `VT_LOAD_UserDict_EXT_KOR`(0x1c8f0)로 꼬리호출하는 얇은 래퍼다.

---

## 4. 질문 3: libhkport.so 와의 화해 (CONFIRMED — Lead 의 독해가 맞다)

### 먼저 매핑 검증 (task 지시사항)
`libvt_custom12_H22.so` 는 **identity 매핑이 아니다**:

```
LOAD 0x000000 vaddr 0x000000 filesz 0x6744c R E    <- .text 포함
LOAD 0x067810 vaddr 0x00068810 filesz 0x3877c RW   <- .data.rel.ro.local 포함
```
- `.text`   vaddr 0x176b8 / off 0x176b8  (delta 0)
- `.rodata` vaddr 0x5d318 / off 0x5d318  (delta 0)
- `.data.rel.ro.local` vaddr 0x68810 / off **0x67810**  (delta **-0x1000**)

즉 **.text 와 .rodata 사이는 delta 0 이고**, 0x68810 이상에서만 0x1000 어긋난다.
본 문서의 모든 문자열은 이 규칙으로 검증 후 읽었다. (0x5d318/0x5d38c 는 delta 0 구간이다.)

`libhkport.so` 는 매핑이 다르다:
```
LOAD 0x000000 vaddr 0x000000 filesz 0x009b9 R
LOAD 0x0009bc vaddr 0x000019bc filesz 0x00e44 R E   <- .text, delta 0x1000
LOAD 0x001800 vaddr 0x00003800 filesz 0x0013c RW
```
`.text` vaddr 0x19bc / off 0x9bc → **delta 0x1000**. 이 파일의 문자열을 읽을 때는
vaddr 에서 0x1000 을 빼야 한다.

### libhkport 의 실제 호출 (ARM 모드, Thumb 아님)
`libhkport.so` 헤더 `Flags: 0x5000200, Version5 EABI, soft-float ABI` 이고
`Java_com_hkport_Vt_nInit`(0x1e24)의 명령이 **4바이트 ARM 인코딩**(`e92d4ff0` 등)이다.
**Thumb 이 아니므로 r0/r1/r2 를 따로 세팅해도 정상 동작한다.**

```
1f9c: ldr  r1, [pc, #184]   @ 0x205c        ; dlsym 이름 슬롯
1fa0: mov  r0, r8                            ; r0 = engineId char*
1fa4: add  r1, pc                            ; r1 = "CUSTOM12_LOADTTS" / "CUSTOM15_LOADTTS"
1fa8: blx  r2                                ; r2 = dlsym  -> r0 = CUSTOM12_LOADTTS 주소
1fac: ldr  r1, [pc, #172]   @ 0x2060
1fb0: mov  r4, r0                            ; r4 = 반환값 보관
1fb4: ldr  r2, [pc, #168]   @ 0x2064
1fb8: mov  r3, r8                            ; 로그 인자 = engineId
1fbc: add  r1, pc
1fc0: str  r0, [sp]
1fc4: add  r2, pc
1fc8: mov  r0, #4
1fcc: bl   __android_log_print               ; "CUSTOM12_LOADTTS(%s) = %d"
...
1fd0: ldr  r0, [pc, #144]   @ 0x2068
1fd4: mvn  r1, r4
1fd8: lsr  r1, r1, #31
1fdc: add  r0, pc
1fe0: str  r1, [r0]                          ; 성공 플래그 저장
```

`libhkport.so` 의 `.dynstr` 에 `dlsym`(0x4e), `dlopen`(0xb0), `CUSTOM12_LOADTTS`,
`CUSTOM15_LOADTTS` 가 있고, 문자열 목록에 `"CUSTOM12_LOADTTS(%s) = %d"` 가 있다.
**0x1fa8 의 `blx r2` 는 `CUSTOM12_LOADTTS` 호출이며 r0 = engineId 이다.**

### 화해
- `r0` = engineId → 엔진의 **arg1 = dbPath 슬롯**에 들어간다.
- `r1`, `r2` 는 **세팅되지 않는다**(직전 값 = dlsym 관련 찌꺼기).
- arg2 슬롯이 `"TTS.KOK.L-NRv24.101.240603-L2voice"` 와 일치할 가능성은 사실상 0
  → **0x177de 의 `bne 1783e` 를 타서 -300 을 반환한다.**

즉 **libhkport.so 의 이 호출은 실패하는 호출이다.** MediaZen 엔진이 정상 로드되는 것은
`libhkport.so` 경로가 아니라 다른 경로(예: `libtts_module.so` / `libvt_multiVoice_t.so`)를
통해서일 가능성이 높다. `libhkport.so` 는 HKMC 펌웨어에서 가져온 참고용 파일이며,
**이 프로젝트의 정답 구현이 아니다.**

> SPECULATIVE: libhkport 경로가 실제로 실패한다는 결론은 "arg2 슬롯이 우연히 일치하지 않는다"는
> 전제에 기댄다. 다만 arg1=engineId 를 dbPath 로 넘기는 구조 자체가 이미 모순이므로,
> 이 호출을 정답 레퍼런스로 삼아서는 안 된다는 점은 CONFIRMED 이다.

---

## 5. dbPath 는 엔진 내부에서 어떻게 쓰이는가 (CONFIRMED)

`VT_LOADTTS_EXT_KOR` **0x1e0b4**. 인자는 6개다
(r0/r1/r2 + 스택 `[sp,#596]`, `[sp,#600]`, `[sp,#604]` — 각 1,1,2회 읽힘).

```
1e0c0: mov  r4, r1            ; r4 = speakerId
1e0c4: mov  r6, r2            ; r6 = dbPath        <-- 3번째 인자
1e0ca: str  r0, [sp, #12]     ; hTTS
1e0fc: cmp  r4, #99
1e0fe: ite ls / 1e100 movls r1,r4 / 1e102 movhi r1,#82   ; speakerId 클램프
1e108: bl   get_speaker_info
1e11c: cmp  r6, #0   ; 1e11e beq 1e3e0   ; dbPath == NULL  -> get_MainDBPath 경로
1e122: ldrsb.w r3, [r6] ; 1e126 cmp r3,#0 ; 1e128 beq 1e262  ; 빈 문자열 -> get_MainDBPath
1e12c: add  r5, sp, #36       ; 지역 버퍼
1e12e: mov  r1, r6
1e130: mov  r0, r5
1e132: blx  strcpy            ; dbPath 복사
1e136: mov  r0, r5
1e138: bl   normalize_path    ; 0x1ca70
1e154: mov  r0, r8            ; r8 = 전역 버퍼
1e156: mov  r1, r5
1e158: blx  strcpy            ; 정규화된 경로를 전역에 복사
1e15c: mov  r0, r8
1e15e: bl   normalize_path
```

- **NULL 이거나 빈 문자열이면 0x1e3e0 / 0x1e262 로 가서 `get_MainDBPath`(0x1cf14)를 호출**한다.
  `get_MainDBPath` 는 `"../"` (vaddr **0x5dad0**, 3바이트 + NUL, `movs r2,#4`) 를 복사하는 얇은 래퍼다
  (`0x1cf14 ldr r1,[pc,#8]` → 슬롯 0x1cf20 = `0x00040bb4`, `add r1,pc` pc=0x1cf1c → `0x1cf1c+0x40bb4 = 0x5dad0`).
- 정규화된 dbPath 는 전역 버퍼에 저장되고, 0x1e1a6~0x1e1ae 에서
  `sprintf(버퍼, fmt, r8)` 로 하위 경로가 만들어진다.
  여기서 `r8` = 방금 저장한 dbPath, `sl` = 포맷 문자열이다.

### 하위 경로 포맷 (로컬 풀 0x1e454, `add sl, pc`)
`0x1e17e: ldr.w sl, [pc, #724]` → 슬롯 0x1e454 (`0x0003f6f0`),
`0x1e188: add sl, pc` — pc = 0x1e188 + 4 = **0x1e18c** → `0x1e18c + 0x3f6f0 = 0x5d87c`.

`.rodata` 0x5f0e4 부근에 다음이 연속으로 존재한다(task 설명의 문자열과 일치):

| vaddr | 문자열 |
|---|---|
| 0x5f0e4 | `"%sdata-common/"` |
| 0x5f0f4 | `"%sdata-%s/"` |
| 0x5f104 | `"%s%s/"` |
| 0x5f110 | `"SOFTWARE\VW\VT\"` |
| 0x5f120 | `"jihae"` |
| 0x5f12e | `"d:/ttsdb/ljh/pcm/"` |
| 0x5f136 | `"cm/"` |

즉 엔진은 **base 경로를 받아 `%s` 로 하위 디렉터리를 붙이는 방식**으로 동작한다.
`"fixed_db/"` 와 `"userdict/userdict_kor.csv"` 도 같은 방식으로 조합된다.

### 관련 전역 변수 (BSS)
| 심볼 | vaddr | 비고 |
|---|---|---|
| `pcMainDBPath` | 0xa20c4 | get_MainDBPath 결과가 들어가는 곳 |
| `PathKey` | 0xa2e48 | `VT_GetPathKey_KOR`(0x29df8)가 읽음 |
| `pcFixedDBPath` | 0xa3048 | fixed DB 경로 |
| `nPrepareDBFlag` | 0xa118c | `PrepareDBAccess` 가 세팅 |
| `DefUserDictName` | 0x91fb4 (.data) | `GetDefUserDictName_COMMON` 이 사용 |

### PrepareDBAccess (0x1b304) — 무의미한 스텁 (CONFIRMED)
```
1b304: movs r0, #1
1b306: bx   lr
1b308 <UnprepareDBAccess>:
1b308: bx   lr
```
**인자를 받지 않고 항상 1을 반환하는 스텁이다.** dbPath 를 받는 함수가 아니다.
`VT_LOADTTS_EXT_KOR` 의 0x1e3aa 에서 호출되어 `nPrepareDBFlag`(r4가 가리키는 곳)를 1로 세팅할 뿐이다.

### GetDefUserDictName_COMMON (0x1b220) — dbPath 무관 (CONFIRMED)
`0x1b226: cbnz r0, 1b248` 로 분기해 `sprintf` 두 번만 수행한다. 인자는
(r0=플래그, r1=?, r2=?) 형태이고 **base 경로를 받거나 저장하지 않는다.**
`VT_LOADTTS_EXT_KOR` 에서 호출되는 곳도 없다.

---

## 6. 질문 4 요약 및 tts_direct.cpp 에 필요한 변경

### 확인된 프로토타입

```c
/* libvt_custom12_H22.so @ 0x177a4, 인자 3개 (CONFIRMED) */
int CUSTOM12_LOADTTS(const char* dbPath, const char* engineId, const char* userDictPrefix);

/* 0x177f8 에서 호출되는 내부 함수 (CONFIRMED, 인자 6개) */
int VT_LOADTTS_EXT_KOR(void* hTTS, int speakerId, const char* dbPath,
                       int a4, int a5, int a6);
```

### 현재 코드의 문제
`tts_direct.cpp:24`
```c
typedef int (*loadtts_t)(const char* db_path, const char* engine_id);   // 2인자 -> 틀림
```
`tts_direct.cpp:84`
```c
ret = g_load(db, id);   // r0=db, r1=id, r2=미정의
```
- 엔진은 arg1(r0) 을 **dbPath 로 사용**하므로 r0=db 는 우연히 맞는 위치다.
  (즉 Lead 의 원래 진단 "dbPath 가 r0 에 없어서 실패" 는 **틀렸다.** dbPath 는 r0 에 있다.)
- 실패 원인은 **arg2(r1)=id 가 `"TTS.KOK.L-NRv24.101.240603-L2voice"` 와 일치하지 않는 것**이다.
  0x177d6 의 비교가 0이 아니게 되어 **0x177de `bne 1783e` → 0x1783e → -300 반환.**
- 비교가 실패하면 0x177f8 의 `VT_LOADTTS_EXT_KOR` 는 **호출조차 되지 않는다.**
  따라서 스피커 인스턴스가 생성되지 않고, 이후 `CUSTOM12_TextToBuffer` 가 -6 을 반환한다.
  (task 설명의 관측과 정확히 일치)
- 3번째 인자(r2)는 비교가 성공한 뒤에만 쓰이므로, 실패 경로에서는 미정의여도 무해하다.

### 필요한 변경 (SPECULATIVE — 빌드/실행 검증 필요)

**(1) 타입을 3인자로 바꾼다.**
```c
typedef int (*loadtts_t)(const char* db_path, const char* engine_id, const char* user_dict_prefix);
```

**(2) 호출부를 3인자로 바꾼다.** 두 번째 인자에는 **버전 리터럴**을 넘겨야 한다.
**engineId 를 두 번째 자리에 그대로 두면 안 된다.**
```c
// 엔진이 요구하는 버전 문자열 (0x5d38c 에서 디코드, 34바이트)
static const char kEngineVersion[] = "TTS.KOK.L-NRv24.101.240603-L2voice";
ret = g_load(db, kEngineVersion, "");
```

- `db` 는 base 디렉터리(r0). NULL/빈 문자열이면 엔진이 `get_MainDBPath` 로 `"../"` 를 쓴다.
- **`id`(engineId) 는 이 호출에서 쓰이지 않는다.** 엔진은 어떤 스피커인지
  arg2 의 버전 문자열과 speakerId(0x177ea 에서 82로 고정)로만 판단한다.
  `Java_com_mediazen_ttsapp_TtsDirect_nInit` 의 `engineId` 파라미터는
  로그/검증 용도로만 남기거나 제거해도 이 호출에는 영향이 없다 (SPECULATIVE).
- 3번째 인자는 유저 사전 접두어다. `""` 를 넘기면
  `sprintf(buf, "%s%s", "", "userdict_kor.csv")` → `"userdict_kor.csv"` (상대 경로)가 된다.
  실제 사전이 `<db>/userdict/userdict_kor.csv` 에 있다면 `"<db>/userdict/"` 를 넘겨야 한다.
  **정확한 값은 실제 db 트리를 확인해야 한다 (SPECULATIVE).**

**(3) custom15 도 동일함 (CONFIRMED — 확인 완료).**
`libvt_custom15_H22.so` 는 섹션 배치가 custom12 와 완전히 같다
(`.text` vaddr 0x176b8 / off 0x176b8, `.rodata` vaddr 0x5d318 / off 0x5d318, 크기 656516).
`strings -a -t x` 결과 두 파일 모두 **파일오프셋 0x5d38c 에 정확히 같은 리터럴**을 갖는다:

```
custom12: 5d318 VW_Custom12   5d38c TTS.KOK.L-NRv24.101.240603-L2voice
custom15: 5d318 VW_Custom15   5d38c TTS.KOK.L-NRv24.101.240603-L2voice
```

`CUSTOM15_LOADTTS`(0x177a4)의 명령열도 `CUSTOM12_LOADTTS` 와 바이트 단위로 동일하다
(`mov r8,r2` / `mov r6,r0` / `mov r7,r1` / 0x177d0 `mov r1,r7`).
또한 custom15 에도 `PrepareDBAccess`(0x1b304), `get_MainDBPath`(0x1cf14),
`VT_LOADTTS_EXT_KOR`(0x1e0b4) 가 **같은 주소에** 존재한다.

→ **버전 리터럴은 두 라이브러리에서 동일하다. `kEngineVersion` 상수 하나로 양쪽 모두 처리 가능하다.**
`tts_direct.cpp:74` 의 `find_symbol(h, "CUSTOM12_LOADTTS", "CUSTOM15_LOADTTS")` 는 그대로 유효하다.

### 남은 미확인 사항 (SPECULATIVE)
1. ~~`libvt_custom15_H22.so` 의 비교 리터럴~~ → **6절 (3)에서 CONFIRMED 로 해소됨.**
2. 3번째 인자(유저 사전 접두어)의 정확한 값 — 실제 `db` 디렉터리 구조 확인 필요.
   `""` 를 넘기면 `"userdict_kor.csv"` 라는 상대 경로가 되므로,
   엔진의 CWD 기준으로 찾게 된다. `<db>/userdict/userdict_kor.csv` 가 실제 위치라면
   `"<db>/userdict/"` 를 넘겨야 한다.
3. `db` 에 넘길 base 경로가 `"data-common/"` 을 포함해야 하는지,
   즉 `%sdata-common/` 의 `%s` 가 `db` 인지 `pcMainDBPath` 인지 미확정.
   `VT_LOADTTS_EXT_KOR` 의 0x1e1a6~0x1e1ae 는 `r8`(= 정규화된 dbPath) 을 `%s` 로 쓰지만,
   0x5d87c 의 포맷 문자열이 정확히 어떤 것인지는 호출 시점의 `sl` 값을
   동적으로 확인해야 확정된다.
4. 버전 문자열이 기기/펌웨어별로 달라질 수 있는지 미확인.
   단, custom12 와 custom15 가 같은 값을 쓰는 것은 확인했으므로
   적어도 이 두 라이브러리에서는 동일하다.
5. **가장 중요한 미검증 항목**: 위 (1)~(2) 변경을 적용한 뒤
   `CUSTOM12_LOADTTS` 가 0을 반환하고 `CUSTOM12_TextToBuffer` 가 PCM 을 내는지는
   실제 빌드/실행으로만 확인 가능하다. 본 분석은 정적 디스어셈블리에 근거한다.

---

## 7. 검증 방법 재현

`libvt_custom12_H22.so` 는 `.text`/`.rodata` 구간에서 delta 0, 0x68810 이상에서 delta 0x1000.
문자열을 읽을 때는 반드시 이 규칙을 적용할 것. `libhkport.so` 는 `.text` 에서 delta 0x1000.

Thumb 리터럴 풀 규칙: `ldr rX, [pc, #N]` 의 대상 주소는
`(해당 ldr 명령 주소 + 4)` 를 4바이트 정렬한 값이고,
`add rX, pc` 에서 쓰이는 pc 는 **그 add 명령 주소 + 4** 다.
두 pc 가 다르므로 슬롯 주소와 add 주소를 혼동하면 엉뚱한 문자열을 읽는다.
