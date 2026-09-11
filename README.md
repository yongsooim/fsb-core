# FSB Core — 소스 보존 스냅샷

원본 게임 동작을 재현하는 플랫폼 독립 C++20 코어의 **보존용 스냅샷**입니다.
이 저장소는 소스를 그대로 보관하기 위한 vault이며, **개발은 계속 기존 작업공간에서**
진행합니다. 이곳에 기능을 추가하거나 개발 이력을 쌓지 않습니다.

개발 작업공간: `/Users/ysim/Documents/Codex/2026-09-08/repo-fsb-x20/outputs/fsb-core`

## 스냅샷 출처

| 항목 | 값 |
| --- | --- |
| 원본 커밋 | `1e64690f5040b3feeb7c61fe8f653b8d9ada5f8f` — "Finalize template checkpoint and restore arena return context" |
| 원본 커밋 시각 | 2026-09-11 20:54:19 +0900 |
| 복사 시각 | 2026-09-11 22:36 +0900 |
| 미커밋 변경 | **없음.** 복사 전후로 워킹트리가 clean이고 HEAD도 동일했습니다. 복사한 모든 파일은 해당 커밋 내용과 바이트 단위로 일치합니다. |

아래 "사본에서 변경한 빌드 설정"을 제외하면 소스는 원본과 동일합니다.

## 포함·제외 범위

포함: `CMakeLists.txt`, `src/`(생성 본문 `src/recovered/` 포함), `include/`, `tests/`,
`tools/`(생성기·감사 스크립트·호스트 코드·매니페스트), `run-game.sh`, `run-event0.sh`.

제외한 항목과 원본 위치(모두 위 개발 작업공간 기준 상대 경로):

| 제외 | 원본 위치 | 크기 | 필요한 경우 |
| --- | --- | --- | --- |
| 게임 리소스·원본 EXE | `assets/` | 229 MB | 게임 실행과 대부분의 계약 테스트 |
| 원본 x86 비교 덤프·회귀 입력 | `reference/` | 381 MB | 계약 테스트의 기대값 |
| 측정·검증 보고서 | `reports/` | 556 MB | 과거 검증 근거 열람 |
| 병렬 세션 인계 기록 | `tasks/` | 1.2 MB | 과거 분업 이력 |
| 빌드 캐시 | `build*/`, `.build-host/` | — | — |
| 프로젝트 문서 | `*.md` (AGENTS/STATUS/NATIVE_MIGRATION 등) | — | 설계·진행 기록 |
| git 이력 | `.git/` | — | — |

`reference/`와 `reports/`는 컴파일에는 필요하지 않습니다. 소스 어디에서도 이들 경로를
`#include` 하지 않으며, CMake도 **테스트 실행 인자**로만 참조합니다.

### 제외한 리소스를 다시 연결하기

저장소 루트에 심볼릭 링크를 만들면 원래의 테스트 구성이 그대로 복원됩니다.

```sh
WS=/Users/ysim/Documents/Codex/2026-09-08/repo-fsb-x20/outputs/fsb-core
ln -s "$WS/assets" assets
ln -s "$WS/reference" reference
```

게임 실행만 할 때는 링크 대신 리소스 경로를 지정할 수 있습니다.

```sh
FSB_ASSETS_DIR="$WS/assets" ./run-game.sh          # SDL3 호스트
./build/fsb_event0_sdl "$WS/assets" --campaign      # 직접 실행
```

## 빌드

의존성: CMake 3.20+, C++20 컴파일러, FreeType. SDL3 호스트(`FSB_CORE_BUILD_SDL_HOST=ON`)는
선택이며 SDL3 3.2.12+가 필요합니다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

리소스를 링크하지 않은 상태에서도 **라이브러리·도구·테스트 실행 파일 전부가 빌드**됩니다.
원본 자료가 필요한 테스트는 등록되지 않고, 자체 완결형 테스트 3개만 실행됩니다.
`assets/`와 `reference/`를 링크하면 144개 계약 테스트가 등록됩니다.

### 사본에서 변경한 빌드 설정

원본과 다른 부분은 다음 두 곳뿐이며, 모두 "제외한 리소스" 때문입니다.

- `CMakeLists.txt`: `add_test`/`set_tests_properties`를 감싸, 명령 인자가 `assets/`나
  `reference/`를 가리키는 테스트는 두 디렉터리가 없을 때 등록하지 않습니다.
  기대값을 바꾸거나 검사를 약화시키지 않고, 등록 여부만 제어합니다.
- `run-event0.sh`: 리소스 경로를 `FSB_ASSETS_DIR` 환경변수로 덮어쓸 수 있게 했습니다.

## 검증 범위

이 스냅샷에서 **확인한 것**:

- 기존 작업공간을 참조하지 않는 새 폴더에서 Release 전체 빌드 성공 (AppleClang 17, macOS arm64).
  빌드 산출물에 원본 작업공간 경로가 나타나지 않는 것도 확인했습니다.
- 리소스 미연결 상태의 자체 완결형 테스트 3개 통과
  (`integer_presentation_contract`, `input_trace`, `checkpoint_context_contract`).
- 리소스를 링크한 구성에서 계약 테스트 144개가 등록되고, 표본으로 실행한
  `core_contract`·`battle_action_contract`가 통과.

이 스냅샷에서 **확인하지 않은 것**:

- 게임의 실제 실행(SDL3 호스트 기동, 화면·음향 출력).
- 원본 실행과의 동등성 검사 전체. 소스 빌드 검증과 원본 동등성 검증은 별개이며,
  후자의 근거는 제외한 `reports/`에 있습니다.

## 현재 진행 상태

일반 C++ 로직·상태로의 전환은 **미완료**입니다. 아래 수치는 이 스냅샷의 코드에서 직접 센 값입니다.

- 원본 함수 **851개**를 일반 C++ 구현으로 전환 (`tools/native_reconstructions.json`의 entries).
- 기계어식 생성 본문 **638개**가 남아 있음 (`src/recovered/`의 `RecoveredBattle::fn_*` 정의).
  둘을 합한 1,489개가 생성기가 다루는 전체 함수입니다.
- 4단계 목표 중 1단계는 진행 중, 2단계는 난수(`RandomState`)와 장면 선택(`SceneState`)만
  실제 저장소 이전을 마쳤고, 3·4단계(스크립트 주소 참조의 경계 이전, 제품 실행 경로의
  레거시 주소 접근층 제거)는 시작하지 않았습니다.
- 대형 대화 템플릿 함수처럼 일부 분기만 전환한 함수는
  `tools/partial_native_reconstructions.json`에 잔여 경로와 함께 선언되어 있습니다.

## 남은 목표

1. 남은 638개 생성 본문의 일반 C++ 전환.
2. 도메인별 상태 구조체로의 이전 완료(2단계).
3. 스크립트의 주소 참조·원본 저장 형식을 입출력 경계로 이동(3단계).
4. 제품 실행 경로에서 레거시 주소 접근층 제거(4단계).
5. 모든 분기·선택 퀘스트를 포함한 원본 동등성 검증.

세부 설계 근거와 검증 기록은 개발 작업공간의 `NATIVE_MIGRATION.md`,
`INCREMENTAL_REFACTORING.md`, `PORT_GAPS.md`와 `reports/`에 있습니다.
