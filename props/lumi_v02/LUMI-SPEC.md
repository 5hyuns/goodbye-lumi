# LUMI 사양서 (동결본)

이 문서와 `lumi_geometry.js`가 루미의 확정 사양이다.
`lumi_v6.html`은 이 사양을 눈으로 확인하는 뷰어이며, 사양의 출처가 아니다.

형상은 `lumi_geometry.js`의 `buildLumi()`가 반환한다. 렌더러는 이 정점 버퍼를
그대로 올리고, 재질과 애니메이션만 이 문서의 값으로 붙인다. §6의 체크섬이
같으면 같은 형상이다.

---

## 1. 좌표계와 치수

| 항목 | 값 |
|---|---|
| 좌표계 | Y+ 위, Z+ 정면(바이저 방향), 오른손 |
| 단위 | 미터 |
| 몸통 반경 | 0.222 |
| 세로 눌림 계수 | 0.97 |
| 바운딩 박스 | X ±0.222 / Y −0.290…0.303 / Z −0.223…0.226 |
| 전고 (안테나 포함) | 0.593 |
| 부양 이미터 평면 Y | −0.20478 |

구체 프로파일은 극각 `a`로 정의한다. `a=0`이 정수리, `a=π`가 바닥.

```
r(a) = 0.222 · sin(a) − inset
y(a) = 0.222 · 0.97 · cos(a)
```

---

## 2. 몸통 존

몸통은 하나의 회전체가 아니라 **7개 존**이며 각각 다른 재질을 쓴다.
존 경계에서 정점을 복제해 노멀을 끊는다 — 이것이 하드서페이스 에지를 만든다.
이 처리를 빼면 매끄러운 달걀이 된다.

| 존 | 극각 범위 (rad) | 인셋 | 재질 |
|---|---|---|---|
| shell_cap | 0.00 – 0.44 | 0 | paint |
| band_upper | 0.44 – 0.55 | 0.0085 | band |
| shell_main | 0.55 – 2.02 | 0 | paint |
| band_lower | 2.02 – 2.13 | 0.0085 | band |
| shell_lower | 2.13 – 2.34 | 0 | paint |
| flange | 2.34 아래 | — | steel |
| emitter_housing | 플랜지 아래 | — | dark |

회전체 분할 수는 64. UV의 `u=0.5`가 정면(+Z)이고, `v`는 전체 프로파일
길이에 대해 존별로 연속 배분된다.

---

## 3. 바이저와 데칼

평면이 아니다. **슈퍼타원 외곽선을 구면에 투영한 패치**다.
평면으로 만들면 구체에 파묻히거나 가장자리가 뜬다.

| 파트 | 반경 | 반폭 A | 반높이 B | 지수 | f 범위 | 재질 |
|---|---|---|---|---|---|---|
| visor_bezel | 0.2268 | 0.134 | 0.074 | 3.2 | 1.00 – 1.19 | panel |
| visor_rim | 0.2242 | 0.134 | 0.074 | 3.2 | 0.99 – 1.02 | dark |
| visor_screen | 0.2236 | 0.134 | 0.074 | 3.2 | 0.002 – 1.0 | screen |

바이저 그룹 전체를 X축으로 **−0.26 rad** 회전(위를 향함).

데칼도 같은 패치 방식. 표면 반경 0.2230.

| 데칼 | 회전 | A | B |
|---|---|---|---|
| decal_front (LUMI) | X +0.258 | 0.080 | 0.036 |
| decal_back | Y π, X −0.08 | 0.082 | 0.041 |
| decal_side ×2 | Y ±0.72 | 0.040 | 0.040 |

정면 데칼의 위치는 여유가 크지 않다. 바이저 하단(1.647 rad)과
하단 밴드 상단(2.02 rad) 사이에 양쪽 7mm 마진으로 들어간다.
둘 중 하나를 움직이면 데칼도 다시 맞춰야 한다.

---

## 4. 재질

| 키 | 설명 | 파라미터 |
|---|---|---|
| paint | 흰 도장 (클리어코트) | base #dedace, roughness map, metalness 0.04, clearcoat 0.92, coatRoughness 0.07, normalScale 0.85, envIntensity 1.35 |
| band | 다크 밴드 | #24282c, roughness 0.72, metalness 0.28 |
| steel | 금속 플랜지·링 | #939aa0, roughness 0.26, metalness 0.96 |
| dark | 리세스·구멍 | #0d1013, roughness 0.58, metalness 0.40 |
| panel | 베젤·포드 후프 | #15191d, roughness 0.30, metalness 0.62 |
| emissive_cyan | 상태 LED | #62d8ff |
| emissive_dim | 후면 LED, 포드 슬릿 | #2f7f9e |
| screen | 디스플레이 | 불투명 검정 바탕 + 발광 그래픽. 투명 금지 |
| glow_radial | 이미터 발광면 | 방사형 그라디언트, 애디티브 |
| glow_beam | 확산층 | 노이즈 텍스처 × 정점 컬러 페이드, 애디티브 |

텍스처는 전부 코드 생성이다 (알베도, 하드서페이스 하이트맵 → 소벨 노멀맵,
러프니스, 데칼, 노이즈, 환경맵). 이미지 파일 0개.

**흰 도장의 광택은 환경맵에 반사할 밝은 조명 스트립이 있어야 보인다.**
환경맵이 밋밋하면 클리어코트를 올려도 티가 나지 않는다.

---

## 5. 애니메이션

| 항목 | 값 |
|---|---|
| 호버 bob | `(0.026·sin(2.62t) + 0.007·sin(3.70t)) · lift` — 두 주기가 나누어떨어지지 않아야 반복감이 없다 |
| 시선 추적 감쇠 | yaw 0.20, pitch 0.24 (임계감쇠) |
| 뱅킹 | `roll = clamp(−lateralVel · 0.30, ±0.32)` |
| 부양 출력 lift | 목표까지 `pow(0.004, dt)` 보간 |
| 부양 정지 시 sag | `(1 − lift) · 0.10` |
| 깜빡임 | 간격 1.7 – 5.1초 랜덤, 지속 0.15초, 최소 높이 5px |

**도킹은 이동과 완전히 같은 파라미터를 쓴다.** 루미는 감정을 표현하지
않는 캐릭터이고, 도킹에 따로 느려지거나 멈칫하는 이징이 없는 것이 그 표현이다.

디스플레이 캔버스는 512 × 290. 눈은 64px 정사각, 중심에서 ±82px.

디스플레이 상태 10종: idle / scan / calc / pulse / count / warn / power /
door / countdown / dying.
엔딩 도킹 시퀀스 5단계: 도킹 시작 → 접속 완료 → 라이트 소등 →
디스플레이 어두워짐 → 마지막 LED 점멸.

---

## 6. 검증 체크섬

`buildLumi()` 결과가 아래와 일치해야 한다.

```
parts: 35
vertices: 9627
triangles: 14772
```

파트별:

| 파트 | 재질 | 정점 | 삼각형 |
|---|---|---|---|
| shell_cap | paint | 1040 | 1664 |
| band_upper | band | 455 | 640 |
| shell_main | paint | 1950 | 3456 |
| band_lower | band | 455 | 640 |
| shell_lower | paint | 585 | 1024 |
| flange | steel | 390 | 384 |
| emitter_housing | dark | 520 | 512 |
| visor_bezel | panel | 292 | 432 |
| visor_rim | dark | 219 | 288 |
| visor_screen | screen | 585 | 1024 |
| decal_front | decal_front | 205 | 320 |
| decal_back | decal_back | 205 | 320 |
| decal_side_l / _r | decal_greeble | 125 각 | 192 각 |
| led_top | emissive_cyan | 24 | 12 |
| led_dot | emissive_cyan | 18 | 16 |
| pod_hoop_l / _r | panel | 384 각 | 768 각 |
| pod_cap_l / _r | dark | 94 각 | 88 각 |
| pod_ring_l / _r | steel | 189 각 | 312 각 |
| pod_holes_l / _r | dark | 70 각 | 60 각 |
| pod_slit_l / _r | emissive_dim | 24 각 | 12 각 |
| back_leds | emissive_dim | 120 | 60 |
| back_slot | dark | 24 | 12 |
| back_grill | panel | 24 | 12 |
| antenna | panel | 46 | 40 |
| antenna_tip | steel | 99 | 160 |
| emit_ring | emissive_cyan | 405 | 704 |
| emit_disc | glow_radial | 46 | 44 |
| beam_0 / beam_1 | glow_beam | 74 각 | 72 각 |

```js
var LG = require("./lumi_geometry.js");
console.log(LG.checksum(LG.buildLumi()));
// { parts: 35, vertices: 9627, triangles: 14772 }
```

---

## 8. 네이티브(C) 포팅 시

`lumi_geometry.js`는 배열 조작과 `Math` 함수만 쓰므로 C로 거의 1:1 대응된다.
포팅 시 실제로 바뀌는 부분은 셋뿐이다.

1. **동적 배열** → 사전 계산한 고정 버퍼 (§6의 정점 수가 그 값이다)
2. **텍스처 생성** → 캔버스 API 대신 픽셀 버퍼 직접 조작.
   그라디언트와 블러는 직접 구현해야 하고, **데칼의 텍스트는 캔버스
   폰트 렌더링에 의존하므로 비트맵 글리프로 대체해야 한다.**
   "LUMI"와 "MAINTENANCE UNIT"은 고정 문자열이라 글리프 몇 개면 된다.
3. **재질** → 엔진 머티리얼 대신 셰이더 유니폼

형상 코드와 수치는 그대로 옮겨진다.
