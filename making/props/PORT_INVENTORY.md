# PORT_INVENTORY.md

1단계 산출물. 코드 없음. 측정값은 전부 이 세션에서 실제로 돌려서 얻었습니다.

**범위 정정 먼저.** 프롬프트는 "프랍 19종"으로 되어 있지만 현재 킷은 **37종**입니다(수경 정원 2종 삭제, 신규 20종 추가). 아래 전부 37종 기준입니다. LUMI는 `lumi.js`를 아직 받지 못해 이 인벤토리에 없습니다 — 표 1·3·6·7에 빈 행으로 남겨 두었습니다.

---

## 표 1. 에셋 목록

37종, 전부 `propkit.js`의 `propDef(name, room, sub, size, buildFn)` 호출 하나씩입니다. 생성 함수는 전부 익명 클로저이므로 "함수명"이 없고, 식별자는 `PROPS[]` 인덱스와 이름입니다.

| # | 이름 | 방 | 면 | 영역 |
|---|---|---|---|---|
| 0 | Cargo Container | cargo | 568 | 101 |
| 1 | Supply Crate | storage | 229 | 71 |
| 2 | Console Terminal | lobby | 365 | 70 |
| 3 | Storage Locker | storage | 400 | 113 |
| 4 | Reactor Valve Stack | engine | 893 | 86 |
| 5 | Medbay Cabinet | medbay | 372 | 117 |
| 6 | Crew Bench | lobby | 156 | 35 |
| 7 | Bunk Bed | quarters | 713 | 130 |
| 8 | Foot Locker | quarters | 306 | 59 |
| 9 | Mess Table | mess | 221 | 42 |
| 10 | Ration Dispenser | mess | 361 | 72 |
| 11 | Med Bed | medbay | 588 | 55 |
| 12 | Auxiliary Generator | engine | 458 | 73 |
| 13 | Coolant Tank | engine | 611 | 75 |
| 14 | Cargo Shelf | storage | 711 | 113 |
| 15 | Storage Drum | cargo | 389 | 20 |
| 16 | Loaded Pallet | cargo | 387 | 72 |
| 17 | Cryo Pod | medbay | 721 | 49 |
| 18 | Airlock Hatch | corridor | 726 | 61 |
| 19 | Data Core Rack | engine | 531 | 64 |
| 20 | Deck Grating | corridor | 382 | 27 |
| 21 | Gas Cylinder Rack | engine | 1363 | 95 |
| 22 | Ceiling Light | corridor | 287 | 22 |
| 23 | EVA Suit Locker | corridor | 286 | 40 |
| 24 | Wall Junction Box | engine | 301 | 31 |
| 25 | Specimen Tank | medbay | 482 | 32 |
| 26 | Suppression Station | corridor | 374 | 51 |
| 27 | Overhead Pipe Run | corridor | 627 | 41 |
| 28 | Access Ladder | engine | 1099 | 50 |
| 29 | Monitor Bank | lounge | 355 | 55 |
| 30 | Control Pedestal | lounge | 307 | 33 |
| 31 | Fuel Cell Stack | engine | 500 | 52 |
| 32 | Drone Dock | engine | 404 | 42 |
| 33 | Energy Core | cargo | 316 | 18 |
| 34 | Energy Pack Interface | gallery | 880 | 65 |
| 35 | Escape Capsule Hatch | gallery | 505 | 51 |
| 36 | Escape Bay Platform | gallery | 976 | 65 |
| — | **LUMI** | — | — | **lumi.js 미수령** |

합계 19,094면.

---

## 표 2. 생성 프리미티브

**전부 CPU(JS), Three.js 의존 0건입니다.** `propkit.js`와 `propview.html`에서 `THREE.`를 검색하면 히트가 15건 나오지만 전수 확인 결과 **전부 주석의 영단어 "three"** 입니다. 코드 의존은 없습니다. 렌더러는 raw WebGL2입니다.

| 프리미티브 | 입력 | 출력 | 계층 |
|---|---|---|---|
| `face4(p0..p3, opts)` | 임의 4점 | 쿼드 1 + 영역 1 | 기반 |
| `addFace` | 4점 + 명시 n/w/h | 쿼드 1 + 영역 1 | 기반 |
| `panel(O,U,V,N,w,h,opts)` | 평면 프레임 | 면 1 + 마이터 4 | face4 위 |
| `hullRing(A,B,opts,smooth)` | 링 2개 | 쿼드 n | face4 위 |
| `capPoly(ring,opts)` | 링 1개 | 팬 | face4 위 |
| `capRound(R,r,seg,...)` | 4링 | 인셋 캡 + 스커트 | face4 위 |
| `loft(B,T,S)` | **4링 2개** | 측면 + 크라운 + 캡 | roundRing/hullRing 위 |
| `octBox(...)` | AABB 파라미터 | loft 래퍼 | loft 위 |
| `hull8(B,T,S)` | 4링 2개 | loft 래퍼 | loft 위 |
| `box/plainBox` | AABB | panel ×6 | panel 위 |
| `tube(p0,p1,r,opts)` | 축 + 반지름 | 벽 + 롤드 캡 + 칼라 | hullRing 위 |
| `stack(base,axis,sect[],opts)` | **프로파일 배열** | 회전체 | hullRing 위 |
| `capBar(a,b,w,t,N,seg,opts)` | 선분 | 캡슐 | face4 위 |
| `xbrace(...)` | 평면 + 크기 | capBar ×2 | capBar 위 |
| `rrFrame(...)` | 평면 + 크기 | 라운드 사각 칼라 | roundRing 위 |
| `boss(c,axis,r,h,opts)` | 축 + 반지름 | 필렛 원기둥 | hullRing 위 |
| `wheelZ(...)` | 원 파라미터 | 림 + 허브 + 스포크 | hullRing 위 |
| `strap` | 선분 | `capBar` 위임 | capBar 위 |
| **순수 함수** (형상 안 만듦) | | | |
| `rectRing / octFoot / ringAt / revRing / cutRing / insetRing / roundRing` | | 점 배열 | — |

**프롬프트의 `pushLathe` 전제는 틀렸습니다.** 프롬프트는 "`pushLathe(프로파일 배열, 세그먼트, 베벨) — propkit.js loft()와 동일"이라고 썼지만, `loft()`는 **선반(회전체)이 아닙니다.** 4점 링에서 4점 링으로 가는 로프트에 코너 필렛과 롤드 크라운이 붙은 것입니다. 회전체에 해당하는 건 `stack()`이고, 이건 별개 함수이며 킷 전체에서 9번만 쓰입니다.

`loft` 하나를 이식해서 킷을 덮을 수는 없습니다. 실제로 필요한 최소 집합은 **face4 + panel + hullRing + capPoly + roundRing**이고, 나머지는 전부 그 위의 조합입니다. 조합 계층까지 이식해야 프랍이 데이터가 됩니다.

---

## 표 3. 에셋 × 프리미티브 사용표

호출 총계 (37종 합):

| 프리미티브 | 호출 |
|---|---|
| panel | 131 |
| tube | 119 |
| rectRing (순수) | 107 |
| plainBox | 97 |
| octBox | 74 |
| capBar | 56 |
| hull8 | 55 |
| boss | 54 |
| box | 48 |
| rrFrame | 16 |
| socket | 12 |
| ringAt (순수) | 12 |
| hullRing | 10 |
| stack | 9 |
| wheelZ | 8 |
| capPoly | 6 |
| strap | 6 |
| xbrace | 2 |
| **합계** | **822** (순수 119 제외 시 703) |

**한 에셋만 쓰는 프리미티브** (프롬프트가 따로 표시하라고 한 항목):

| 프리미티브 | 유일 사용 에셋 |
|---|---|
| `capPoly` (직접 호출) | Energy Pack Interface |
| `ringAt` (직접 호출) | Energy Pack Interface |
| `xbrace` | Airlock Hatch |

셋 다 일반화가 이미 되어 있습니다(전용 로직 아님). 다만 Energy Pack Interface의 슬롯 보어는 `ringAt` + `hullRing` + `capPoly`를 직접 조합한 것이라, 중간 포맷이 **원시 점 배열을 실을 수 있어야** 합니다. Escape Capsule Hatch의 팔각 포탈도 같습니다.

---

## 표 4. 부품 계층·변환

**계층이 없습니다.** 이 킷에는 부모-자식도, 로컬 변환 행렬도, 미러도, 인스턴스 배치도 없습니다. 모든 프리미티브가 **월드 좌표를 직접 받습니다.** 반복은 JS의 `for` 루프가 좌표를 계산해 넘기는 방식이고, 미러는 `for (const s of [-1,1])`로 부호를 곱합니다.

프롬프트 2단계 2번의 "인스턴스/미러/반복은 변환 행렬 배열로 처리"는 **이 킷에는 해당 사항이 없습니다.** 변환기가 호출을 인자 해석된 상태로 기록하면 루프와 미러가 전부 숫자로 접힙니다. 행렬식 부호 문제도 생기지 않습니다.

부작용 하나: 인스턴스가 없으므로 같은 프랍을 12번 배치할 때 텍스처 변주가 없습니다. (배치 계획서에서 이미 지적)

---

## 표 5. 재질 파라미터 → EntFx 매핑

프리뷰의 재질은 **면당 `PkRegion` 하나**입니다.

| 프리뷰 필드 | 의미 | EntFx 대응 | 비고 |
|---|---|---|---|
| `kind` (0–10) | 표면 생성기 종류 | `fx.sk` | doors.c는 `sk=3` MACHINED 하나만 씀. 종류 매핑표 필요 |
| `seed` | 생성기 시드 | **매핑 불가** | EntFx에 시드 필드 없음. 추가 필요 |
| `params[4]` | 생성기 인자(피치·반지름·개수) | 부분적으로 `fx.sf` | `sf`는 스칼라 1개. 4개 필요 |
| `accent` (0–5) | 액센트 색 역할 | **매핑 불가** | 색이 아니라 역할 인덱스 |
| `tint[3]` | 실제 알베도 | `pushBox`의 r,g,b | 대응 |
| `paint` | 유전체 도장 여부 | `fx.si` (메탈니스) | paint=1 → si≈0.02 |
| `rim` | 영역 테두리 베벨 | **매핑 불가** | 추가 필요 |
| `w`, `h` | **영역 실측 크기(m)** | **매핑 불가** | 표 8 참조. 가장 중요한 누락 |
| — | | `fx.rim`, `fx.ao`, `fx.rough` | 프리뷰는 러프니스를 생성기가 계산. 스칼라로 못 받음 |
| — | | `fx.lit`, `fx.ldir` | 엔진 고유. 프리뷰 대응 없음 (조명은 달라도 됨) |
| — | | `fx.zone` | 엔진 고유 |
| — | | `fx.sp` | 프리뷰 대응 없음 |

**결론: EntFx에 최소 `seed`(f32), `params[4]`(f32), `accent`(u8), `rim`(u8), `regionW/H`(f32) 추가가 필요합니다.**

---

## 표 6. 텍스처 생성기

전부 **GPU(GLSL)**, `propview.html`의 `LIB_GLSL` + `BAKE_FS`에 있습니다. Three.js 의존 없음.

**SDF·프로파일 기반 함수**

| 함수 | 역할 |
|---|---|
| `wx(x)` | 타일링 래핑 |
| `triw(x)` | 삼각파 |
| `vnoise(p)` | 바이리니어 값 노이즈 |
| `sdB / sdR / sdA` | 박스 / 라운드 박스 / 링 SDF |
| `cham(d,w)` | 챔퍼 프로파일 |
| `rndp(d,r)` | 라운드오버 프로파일 |
| `domep(p,r)` | 돔 프로파일 |
| `boltp(p,r)` | 볼트 합성 (돔 + 링 함몰) |
| `glyph(bits,q,cell)` | 3×5 비트맵 폰트 (숫자 10 + 문자 8) |
| `hazardPhase` | 영역 폭에 정수 주기로 양자화된 대각 줄무늬 |

**높이 함수 (kind별)** — `H_panel`, `H_rib`, `H_screen`, `H_buttons`, `H_hazard`, `H_valve`, `H_seam`, `H_pipe`, `H_plain`, `H_perf`. 9번 슬롯은 비어 있음(K_FOLIAGE 삭제).

**마스크·노이즈** — `fcap(f)`(텍셀 나이퀴스트 주파수 클램프), `fbm(p,f0,oct,seed)`, 그리고 `BAKE_FS` 본문의 캐비티/컨벡시티/슬로프 마스크, 곡률 기반 마모, 중력 흘러내림, 재질 ID 채널(`gMat`).

**해상도** — 프롭당 1024² 또는 2048², 3 타깃(albedo+rough / normal+metal / emissive+ao). 결정은 패킹 결과를 측정해서 내림(`atlasFor`).

**면적 기준 kind 비중** (변환기 우선순위):

| kind | 면적 | 비중 |
|---|---|---|
| PLAIN | 190.2 m² | 59.3% |
| PANEL | 68.5 m² | 21.4% |
| PERF | 27.4 m² | 8.6% |
| RIB | 13.0 m² | 4.0% |
| SEAM | 8.2 m² | 2.6% |
| SCREEN | 5.6 m² | 1.8% |
| HAZARD | 5.3 m² | 1.7% |
| PIPE | 1.6 m² | 0.5% |
| BUTTONS | 0.8 m² | 0.3% |
| VALVE | 0.03 m² | 0.0% |

상위 6개가 97.6%입니다.

---

## 표 7. 난수·해시

| 위치 | 구현 | 문제 |
|---|---|---|
| `propkit.js:54` | `jHash(s) = fract(sin(s*127.1+311.7)*43758.545) - 0.5` | **sin 기반. float64** |
| `propview.html:129` | `h21(p) = fract(sin(dot(p,vec2(127.1,311.7)))*43758.545)` | **sin 기반. float32** |
| `propview.html:870` | `jHash(r.seed*1.7+0.3)` 영역별 톤 지터 | 위와 동일 |

**Math.random 사용 0건. 라이브러리 노이즈 0건.** 여기까지는 프롬프트 규칙 4를 만족합니다.

**그러나 sin 해시는 1:1을 깨뜨립니다.** `sin(x)*43758.545`의 소수부는 x가 커질수록 입력의 최하위 비트에 지배되고, 그 비트는 float32(GLSL)와 float64(JS)에서 다릅니다. 같은 GLSL을 두 GPU에서 돌려도 다릅니다. 프롬프트 규칙 4가 요구하는 "양쪽 동일한 정수 해시"로 **먼저 교체해야** 텍스처 픽셀 차 ≤ 1/255가 성립할 수 있습니다.

이건 이식 문제가 아니라 프리뷰 문제이므로, 규칙 4대로 **프리뷰를 먼저 고쳐야 합니다.**

---

## 표 8. 좌표계·UV 규약

| 항목 | 프리뷰 | 엔진(doors.c/prim.h 관찰 범위) |
|---|---|---|
| up | +Y | +Y |
| 단위 | 미터 | 미터 |
| 감기 | CCW seen from outside | **확인 필요** (pushBox 내부 미확인) |
| 프리미티브 | 임의 쿼드 | 축 정렬 박스 |
| 면 방향 | 임의 | 2축(`ax` 스왑)만 |
| UV | **아틀라스 좌표** (`finalizeProp`이 패킹 후 기록) | 없음 — `sf`가 미터 피치 |
| 탄젠트 | 정점 노멀에서 파생 (`T=(N.z,0,-N.x)`) | **확인 필요** |
| 표면 좌표 | **영역 로컬 2D, 미터** (`p`, `ww`, `wh`) | 월드/트라이플래너 추정 |

**여기가 이 이식에서 가장 위험한 지점입니다.** 프리뷰의 높이 함수는 전부 영역 로컬 좌표를 씁니다. `H_panel`의 볼트는 면 테두리에서 안쪽으로 배치되고, `hazardPhase`는 영역 폭에 정수 개 주기가 들어가도록 양자화합니다. 월드나 트라이플래너 좌표로 대체하면 **볼트 위치와 줄무늬 정렬이 둘 다 어긋납니다.** 지오메트리가 정확해도 그림이 달라집니다.

---

## 표 9. 엔진 부족 기능

| 필요 | doors.c에서 관찰됨? | 조치 |
|---|---|---|
| 임의 쿼드/메시 프리미티브 | 아니오 (`pushBox`만) | **확인 필요** — `prim.h` 미수령 |
| 정점 어트리뷰트에 영역 로컬 uv | 아니오 | 추가 필요 |
| EntFx의 seed/params/accent/rim/regionWH | 아니오 | 필드 추가 |
| 노멀맵·마스크·에미시브 텍스처 슬롯 | 아니오 (절차적 `sk`만) | 노선 결정에 따라 다름 (아래) |
| 오프스크린 베이크 | **확인 필요** | 노선 결정에 따라 다름 |
| 인스턴싱 | 불필요 (표 4) | — |

---

## 요약 A — 변환기 v1이 지원할 기능

지오메트리 변환기는 **이미 만들어져 있고 검증까지 끝났습니다**(`cport/`). 프롬프트 2단계의 완료 조건 대비:

| 2단계 완료 조건 | 상태 |
|---|---|
| 정점 수·인덱스 수 동일 | **통과** — 19,094면 전부 |
| 최대 위치 오차 ≤ 1e-4 m | **2.2e-4** (float32 반올림, 아래 참조) |
| UV 최대 오차 ≤ 1e-5 | **해당 없음** — 아틀라스를 안 씀. 노선 결정 필요 |
| 탄젠트 부호 불일치 0 | **해당 없음** — 같은 이유 |
| 재질 필드 일치 | **통과** — 불일치 0면 |
| 프랍별 전용 C 코드 없음 | **통과** — 703 호출을 자동 기록·생성 |

위치 오차 2.2e-4가 기준 1e-4를 넘습니다. 원인은 float32 정규화이며, 발생 지점은 Escape Capsule Hatch 외곽 팔각의 코너 컷 8 mm 같은 **거의 퇴화한 쿼드의 법선**입니다. 좌표 자체가 아니라 단위 법선의 성분 차이이므로 각도로는 0.013°입니다. 기준을 "위치 ≤ 1e-4 m, 법선 ≤ 1e-3" 두 개로 분리하면 통과합니다. 판단이 필요합니다.

## 요약 B — 미지원 / 결정 필요

**1. sin 해시 → 정수 해시 교체 (프리뷰 선행 작업).** 표 7. 이걸 안 하면 3단계 완료 조건이 원리적으로 달성 불가입니다.

**2. 표면 노선 결정 — 아틀라스 베이크 vs 프래그먼트 평가.** 3단계 전체가 여기에 달려 있습니다.

- **노선 A(아틀라스 베이크).** 프리뷰 파이프라인을 그대로 옮깁니다. UV·탄젠트 기준이 살아나고 `tex_diff.py`가 의미를 갖습니다. 비용: 프롭당 1024²~2048² × 3 타깃. 37종 전부면 **136 Mpx × 3 = 1,632 MB**. 실행 파일이 아니라 VRAM이지만 그래도 불가능합니다. 512²로 낮추면 116 MB인데 텍셀 밀도가 볼트를 못 담습니다.
- **노선 B(프래그먼트 평가).** 같은 2D 필드를 영역 로컬 좌표로 프래그먼트마다 평가합니다. 텍스처 0 바이트. 대신 정점 어트리뷰트에 영역 로컬 uv와 (w,h)가 필요하고, 검증 기준은 "픽셀 차"에서 "같은 카메라·같은 조명으로 렌더한 두 이미지의 차"로 바뀝니다.

저는 **B**를 권합니다. 근거는 위 VRAM 숫자 하나입니다. 판단이 뒤집히는 조건: 엔진에 이미 오프스크린 RT와 아틀라스 관리가 있고, 한 방에 보이는 프롭만 베이크하는 스트리밍이 가능하다면 A가 가능해집니다.

**3. LUMI.** `lumi.js` 미수령. 프롬프트 3단계 3번이 말하는 바이저 디스플레이 10모드 + 도킹 5단계, 호버 글로우는 propkit의 프리미티브 집합으로 표현되지 않습니다. 별도 인벤토리가 필요합니다.

**4. 엔진 API 확인 필요 (규칙 6).** `prim.h`, `ent.h`, `sk`/`sf`/`si`/`sp`를 소비하는 셰이더. 특히 `pushBox`가 임의 정점 경로를 백엔드로 갖는지 여부. 이게 표 9 첫 줄의 답을 정합니다.

**5. 원시 점 배열.** Energy Pack Interface의 슬롯 보어와 Escape Capsule Hatch의 팔각 포탈은 `ringAt`+`hullRing`+`capPoly`를 직접 조합합니다. 중간 포맷이 점 배열을 실을 수 있어야 하며, 현재 변환기는 이미 그렇게 하고 있습니다(리터럴 인라인).
