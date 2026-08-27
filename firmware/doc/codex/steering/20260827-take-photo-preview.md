# take_photo save 후 서브창 JPEG 프리뷰

## 목적

`take_photo`의 `save` 성공 후, 포토프레임과 같이 Avatar 서브창에 저장 JPEG를 표시한다.

## 대상

- `firmware/src/llm/ChatGPT/FunctionCall.cpp` — save 성공 후 `updateSubWindowJpg` + enable
- `firmware/src/face/LayeredFace.cpp` — 서브창 enable 시 서브창을 실제로 그림 (현재 fast path에서 생략)
- `firmware/lib/m5stack-avatar/src/SubWindow.*` — `isEnabled()`, JPG를 320×240에 fit
- `firmware/lib/m5stack-avatar/src/Face.cpp` — scale=1 경로에서도 서브창 그리기

## 방침

1. PhotoFrame과 동일: `sd_bus_lock` → `avatar.updateSubWindowJpg(path)` → unlock → `set_isSubWindowEnable(true)`
2. LayeredFace/Face가 서브창을 매 프레임 합성하도록 보완 (아니면 프리뷰가 안 보임)
3. VGA JPEG는 `drawJpg(..., maxW=320, maxH=240)`로 화면에 맞춤
4. 프리뷰는 사용자가 끄거나 모드 전환(`set_isSubWindowEnable(false)`)까지 유지

## 확인

- camera env 빌드에서 「사진 찍어줘」→ SD 저장 + 화면 서브창/전체 프리뷰
- 시리얼: `JPEG loaded` / `[photo-preview]`
