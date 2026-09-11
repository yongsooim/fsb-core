# 브라우저 호스트

`tools/event0_web.cpp`는 `event0_sdl.cpp`와 같은 루프를 돌리되, SDL이 주던 것만
브라우저 API로 바꾼 것이다. 코어는 자기가 브라우저에 있다는 사실을 모른다.

| 기능 | 네이티브 | 브라우저 |
|---|---|---|
| 시간 | `SDL_GetTicks` | `emscripten_get_now` |
| 루프 | `while` + `SDL_PollEvent` | `emscripten_set_main_loop`(rAF) |
| 화면 | `SDL_UpdateTexture` | `putImageData` |
| 입력 | `SDL_Scancode` | `KeyboardEvent.code` |
| 오디오 | `HostAudio` | `AudioWorklet`이 직접 믹싱 |
| 빨리감기 | 백틱 누름·뗌, F6~F8 | 백틱 누름·뗌 |

`present_image()`가 내주는 RGBA와 `post_input()`이 받는 구조체는 양쪽이 같다.

게임 내부 프레임버퍼는 SDL 호스트와 같이 원본 EXE의
`globals::framebuffer_width`/`height`에서 읽는다(800×600).
`fsb_start`의 width/height와 캔버스 크기는 최종 표시 크기다. 이 값으로
논리 화면을 대신하거나 `Runtime`의 검사 기본값640×480을 사용하면,
원본800×450 이벤트 화면의 좌우가 잘리고 배우 옆 말풍선의 공간 판정도 바뀐다.
[Event7 말풍선 위치 수정·원본 대조](../../reports/dialogue-placement/verification.md).

## 빨리감기

백틱을 누르고 있는 동안 `PlaybackClock`이 16배속으로 1ms tick을 공급한다. 건너뛰는
tick은 없고, 누르는 동안에는 네이티브와 같은 기준으로 일반 대사 확인도 자동 처리한다.
`KeyboardEvent.code`가 물리 키를 가리키므로 한글 배열에서도 같은 키다. 키 반복과
조합 키는 무시하고, 페이지가 blur되면 keyup이 오지 않으므로 그때 해제한다.

소리는 배속과 무관하다. 워클릿이 곧 장치다 — PCM과 각 보이스의 표본 위치를 들고
오디오 클럭으로 직접 믹싱한다. 호스트는 샘플을 넘기지 않고 엔진이 보이스에 내리는
명령(load·play·stop·volume·loop)만 그대로 전달한다. 네이티브 호스트의 장치측 믹서가
받는 것과 같은 명령이며, 워클릿의 믹싱은 `AudioOutput::mix`를 정수 연산까지 그대로
옮긴 것이다.

그래서 빨리감기 진입·탈출에 seek도, 믹서 교대도, 맞춰야 할 두 번째 위치도 없다.
실측으로 백틱을 누른 직후·뗀 직후 400 ms 동안 오디오 명령은 0건이고, 재생 중인
보이스는 그대로 이어진다. 게임 쪽 음향 상태(`Audio::advance_silently`)는 예전처럼
가상 시각을 따르므로 원본의 완료 시점 계약은 유지된다.

대가는 메모리다. 한 번 재생된 파형은 워클릿 쪽에 복사본으로 남는다.

## 이벤트 경계

페이지는 네이티브의 `--intro`와 같이 원본 타이틀 화면에서 시작한다. START가
Event0으로 들어가고, LOAD는 원본 저장 창을 연다(브라우저 저장은 아직 연결되지
않아 슬롯이 비어 있다). 그래서 자산 경계는 최소 9로 올려 등록한다 — 타이틀에서
어느 맵의 저장이든 불러올 수 있기 때문이다.

`?event=N`은 그 이벤트가 끝나면 엔진을 멈추는 검사 경계다. 붙이지 않으면
네이티브 실행기처럼 원본 이벤트 연쇄를 그대로 따라간다. 경계를 2 이상으로 잡으면
`sequence-assets.tsv`가, 9 이상이면 `field-assets.tsv`가, 18 이상이면
`campaign-assets.tsv`가 시작 시점에 전부 읽히므로 그만큼이 마운트돼 있어야 한다.

원본 실행 파일은 자원 이름을 대소문자를 섞어 부르고 디스크의 파일은 대문자다.
macOS·Windows는 이 차이를 숨기지만 MEMFS와 Linux는 숨기지 않는다 — 카탈로그의
BGM 34개가 여기 걸린다. `fsb::lab::read()`가 열기에 실패하면 같은 디렉터리에서
대소문자를 무시하고 한 번 더 찾는다.

## 빌드

전체 자산은 229 MB라 그대로 패키징할 수 없다. 요청한 이벤트 경계가 실제로 여는
파일만 골라 스테이징한 뒤 그 디렉터리를 가리킨다.

```sh
tools/web/stage_assets.py assets /tmp/web-assets            # 813개 파일, 68.9 MB
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release \
    -DFSB_WEB_ASSETS=/tmp/web-assets
cmake --build build-web --target fsb_event0_web -j8
```

정적 호스트에 올릴 배포본은 한 번에 만든다. 자산을 스테이징하고, 소리를 Opus로
다시 굽고, 남은 것을 파일 크기 제한 아래로 나눠 담고, 페이지와 모듈을 함께 모은다.
모듈은 `-DFSB_WEB_ASSETS=`로 자산 없이 빌드한 것을 쓴다.

```sh
emcmake cmake -S . -B build-web-nodata -DCMAKE_BUILD_TYPE=Release -DFSB_WEB_ASSETS=
cmake --build build-web-nodata --target fsb_event0_web -j8
tools/web/package_deploy.py assets build-web-nodata /tmp/deploy --limit 24
```

소리는 8비트 22 kHz PCM이라 전체 트리 218 MiB 중 104 MiB를 차지하면서 압축도 거의
안 된다. `encode_audio.py`가 같은 223개를 Opus 21 MiB로 굽고, 페이지가 브라우저
코덱으로 디코드해 원래 표본율·채널·비트수·프레임 수 그대로 WAV를 MEMFS에 써 넣는다.
손실 압축이며 네이티브 빌드는 원본 PCM을 그대로 읽는다.

`FSB_WEB_ASSETS`를 바꿔도 CMake는 다시 패키징하지 않는다. 스테이징 내용을 바꿨다면
`rm build-web/fsb_event0_web.data` 후 다시 빌드한다.

산출물은 `build-web/`에 `index.html`, `fsb_event0_web.js`(142 KB),
`.wasm`(4.9 MB), `.data`(68.9 MB)로 나온다.

## 실행

`file://`로는 안 된다. WASM과 `.data`를 fetch해야 하므로 HTTP가 필요하다.

```sh
cd build-web && python3 -m http.server 8731
# http://localhost:8731/index.html
# http://localhost:8731/index.html?event=9  로 경계 지정
```

브라우저 정책상 오디오는 사용자 조작 이후에만 시작되므로 시작 버튼을 둔다.

## 검증

```sh
npm install playwright && npx playwright install chromium
node tools/web/verify.js http://localhost:8731/index.html /tmp/shot.png 10
```

캔버스를 읽어 실제로 그려졌는지(`colours > 1`) 확인하고 콘솔 오류를 수집한다.
스크린샷만으로는 빈 화면과 그려진 화면을 구분하기 어려워 색 수를 센다.

이어서 백틱을 누른 채 `fsb_logical_ms()`가 실제 시간 대비 얼마나 나아가는지 잰다.
표시 프레임은 rAF에, 샘플은 장치 클럭에 묶여 있어 배속이 걸렸는지 드러내지 않기
때문에 논리 시각만이 판별 근거다. 측정값은 `playback`으로 출력된다.

## 확인된 것과 아닌 것

확인됨 — Event 0 오프닝 재생, 한글 대사 렌더링, Enter 입력으로 장면 진행,
맵/스프라이트 로드, 콘솔 오류 0.

빨리감기도 헤드리스로 측정했다. 논리 시각은 평소 실제 시간의 1.00배, 백틱을 누르는
동안 15.3배, 떼면 다시 0.99배였다. 16.0배가 아니라 15.3배인 것은 이 기기가 그만큼만
실행했다는 뜻이다 — 표시 배속은 목표이지 보장이 아니다.

소리는 워클릿이 직접 만든다. 백틱을 누른 직후·뗀 직후 400 ms 동안 오디오 명령 0건,
재생 중 보이스는 1개로 유지, 출력 레벨은 곡 자체의 셈여림만큼만 움직였다.
`window.fsbAudioReport()`가 재생 중 보이스 수, 무음 프레임 수, 최근 구간의 출력
레벨을 돌려준다 — 헤드리스에서 소리에 대해 확인할 수 있는 전부다.

배속 중이든 아니든 소리를 사람이 들어 확인하지는 않았다. 헤드리스에는 출력 장치가
없다.

확인 안 됨 — 장시간 실행에서 장치 클럭 편차(보통 ±0.1%)가 쌓이는지,
후반 이벤트, 저장/불러오기(브라우저 저장소 미연결).

## 남은 것

- **저장/불러오기.** 네이티브 호스트는 `SaveDirectory`를 쓴다. 브라우저에서는
  IDBFS나 localStorage로 옮겨야 한다.
- **자산 크기.** 68.9 MB는 첫 로드에 부담이다. `.data`는 이미 압축되지 않은 채로
  나가므로 서버에서 brotli/zstd를 켜면 실측 기준 3분의 1까지 줄어든다.
- **후반 이벤트.** `--through-event`가 커지면 맵·BGM이 더 필요하다.
  `stage_assets.py`가 준비된 카탈로그를 읽어 확장하지만, 그만큼 패키지가 커진다.
  결국은 요구 시점 fetch가 필요하다.
