# LayeredFace 추가 눈 표정

## 목적

SD 카드의 `eye_closed`, `eye_left`, `eye_puzzled`, `eye_right` PNG를 LayeredFace에 등록해 깜빡임, neutral 시선, doubt 표정에 사용한다.

## 범위와 방식

- `src/face/LayeredFace.cpp/.h`만 변경한다.
- 기존 마젠타 투명 키와 overlay bounding-box 크롭 경로를 그대로 사용한다.
- `eye_closed`는 eye-open ratio가 닫혔을 때 최우선으로 사용한다.
- `eye_puzzled`는 `Expression::Doubt`에 사용하고, 파일이 없으면 기존 surprised 눈으로 폴백한다.
- left/right는 neutral 상태에서 수평 gaze가 각각 -0.20/+0.20을 넘을 때만 사용한다. 감정 표정은 기존 눈을 유지한다.

## 메모리 기준

새 PNG의 투명 영역을 뺀 예상 RGB565 상주량은 closed 5,440B, left 20,520B, puzzled 22,016B, right 27,886B로 합계 75,862B다. 전체 화면 4장(614,400B) 대신 크롭 레이어를 사용한다. base와 composite 및 말풍선 sprite 크기는 바꾸지 않는다.

## 확인

- `m5stack-cores3`, `m5stack-cores3-realtime-camera`를 빌드한다.
- 기기에서 새 레이어의 `320x240 -> WxH @ (x,y)` 로그, doubt/blink/neutral gaze 표시, 기존 lip-sync·말풍선·감정 표정을 확인한다.
