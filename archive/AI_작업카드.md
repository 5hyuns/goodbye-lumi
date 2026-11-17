# AI 작업카드 — 세션별 지시서

각 카드를 **새 세션에서 하나씩** 붙여넣는다. 카드 위의 공통 헤더는 매번 함께 붙인다.
엔진 관습(파일 확장자, 컴포넌트 이름)은 `{엔진}` 자리에 실제 이름을 채운다.

---

## 공통 헤더 (모든 카드 앞에 붙임)

```
프로젝트: 침몰하는 함선 FPS. 명세서는 LUMI_FPS_구현명세서_v1.md, 데이터는 data/*.json, 현재 구조는 ARCHITECTURE.md.
규칙:
1. 이번 세션은 아래 작업 하나만 한다. 다른 시스템을 손대지 않는다.
2. 코딩 전에 (a) 새로 만들 파일 (b) 수정할 파일 (c) 접근 방식 3줄을 먼저 보여주고 내 승인을 기다린다.
3. 기존 이동·총기·적·맵 생성 코드는 수정 금지. 필요하면 어댑터/래퍼를 새로 만든다.
4. 표와 JSON이 다르면 JSON이 우선. JSON을 코드에 복붙하지 말고 로드해서 쓴다.
5. 엔진 API는 확신 없으면 지어내지 말고 "확인 필요"라고 표시한다.
6. 끝나면 완료 조건을 디버그 콘솔 명령으로 직접 검증하고, 검증 명령과 결과를 보고한다.
7. ARCHITECTURE.md에 이번 세션에서 추가된 시스템·이벤트 이름·파일 경로를 추가한다.
8. 커밋 메시지 한 줄을 제안한다.
```

---

## 카드 0 — 좌표 매핑 + 디버그 콘솔

```
작업: SVG 좌표 → 월드 좌표 변환 유틸과 디버그 콘솔을 만든다.

1. data/rooms.json의 coordinate_system을 읽어 svgToWorld(x, y, h=0) 함수를 만든다.
   현재 절차적 맵의 방 배치와 rooms.json의 rect가 일치하는지 먼저 비교하고, 다르면
   (a) 맵을 rooms.json에 맞추거나 (b) 변환 함수에 오프셋/스케일을 넣는 두 방법 중 어느 쪽이
   적은 변경인지 판단해 제안한다. 결정 전 코딩하지 않는다.
2. 디버그 콘솔(틸드 키). 명령:
   goto <S0..S12 | R-XXX | C-XX>   해당 위치로 텔레포트 + 그 씬 진입 플래그 세팅
   flag <name> <value>              플래그 강제
   spawn <E-xx>                     교전 강제 발동
   say <L-xxx>                      대사 강제 재생
   door <D-xx> open|close
   give <W1|W2|W3|ammo|hp>
   mark <x> <y>                     SVG 좌표에 5초간 마커
   god                              무적 토글
   (flag/spawn/say/door는 해당 시스템이 아직 없으면 "미구현"을 출력하도록 스텁)
완료 조건:
- mark 197 211 / mark 168 590 / mark 497 728 세 마커가 각각 숙소 남문·슬롯 콘솔·화물칸 상부 플랫폼 위치에 찍힌 스크린샷.
- goto R-ENG 후 플레이어가 기관실 안에 있음.
```

## 카드 1 — 플래그 저장소

```
작업: 전역 플래그 저장소.
- data/scenes.json의 flags 목록과 initial 값을 로드.
- get(name), set(name, value), 변경 시 이벤트 "flag_changed:{name}" 발행.
- pack_state[1..3], slot_charge[1..6]은 배열 접근 지원.
- 콘솔 flag 명령을 실제 연결. flag dump로 전체 출력.
완료 조건: flag sink_stage 2 → flag dump에 반영, 구독자 콜백 1회 호출 로그.
```

## 카드 2 — 트리거 볼륨 + 이벤트 버스

```
작업: 이벤트 버스와 트리거 볼륨.
- 버스: emit(id, payload), on(id, cb), once(id, cb). 이벤트 ID는 문자열. 로그 토글.
- 트리거 볼륨: data/rooms.json의 rooms/corridors/junctions rect마다 자동 생성.
  진입 시 "enter_{ID}", 최초 진입 시 추가로 "enter_{ID} first" 발행. 이탈 시 "exit_{ID}".
- 특수 볼륨: C-03 y>310 존("E01_zone"), 덕트 입구 [445,822]("enter_duct"), 캡슐 B6 앞 3m("near_capsule").
- 아이템 습득/문 개방/교전 종료는 나중 시스템이 "pickup_{ID}", "door_opened_{ID}", "clear_{E-xx}"로 발행하도록 이름만 확정해 ARCHITECTURE.md에 기록.
완료 조건: 로그 켜고 숙소→로비까지 걸으면 enter_C-01 first, enter_C-02 first, enter_J-1 first, enter_C-03 first, E01_zone, enter_R-LBY first 순으로 찍힘.
```

## 카드 3 — 대사·자막 재생기

```
작업: data/dialogue.json을 읽는 자막 재생기.
- 각 line의 trigger 이벤트를 버스에 구독. "chain"은 직전 줄 종료 즉시 재생.
- 재생 길이 = 글자수/chars_per_sec + tail_pad_s. one_shot(한 번만).
- 자막 UI: 하단 중앙, "루미:" 화자 표기, 42자/줄 자동 개행, 최대 2줄.
- 전투 중(활성 적 > 0) 재생 보류 → 큐잉 → 전투 종료 후 순서대로. combat_exceptions는 예외.
- 재생 시작/종료 시 "dialogue_start:{id}", "dialogue_end:{id}" 발행. (루미 디스플레이가 이걸 구독)
- 60초 무대사·비전투면 idle_barks 중 하나 랜덤(중복 없이).
- 비프음: 글자당 1비프, 문장 끝 하강. display가 WARN이면 경고 피치.
완료 조건: say L-016 → L-020까지 chain으로 이어짐. spawn E-02 중 say L-014 → 보류되었다가 전멸 후 재생.
```

## 카드 4 — 루미 상태 머신

```
작업: 기존 루미 프랍 위에 data/lumi.json의 상태 머신을 얹는다.
- 위치 모드 LEAD/HUD/PANEL/DOCKED/OFF. 전환 보간 1.0s. 최대 속도 8 m/s. 고스트 콜라이더.
- 적 인지(활성 적 > 0) → HUD, 없으면 이동 중 LEAD.
- 디스플레이 8종. 기본 VITALS(체력% 바 + BPM). dialogue_start → 해당 line의 display 상태, dialogue_end 0.5s 후 VITALS.
  "A>B" 표기는 A로 시작해 대사 중 B로 전환.
- BPM: 평시 78~92 / 전투 120~165 / 체력 30% 미만 170+ 로 보간. 심박 SFX 동기.
- 체력 30% 미만이면 VITALS 대신 WARN 점멸(2초 주기로 VITALS와 교대).
- 스포트라이트는 항상 루미 전방. OFF에서 소등.
- intro_sequence 함수: EYES_IDLE → HEARTBEAT 2s → EYES_IDLE → emit intro_recognize.
완료 조건: 콘솔에서 lumi mode HUD/LEAD/PANEL 전환 확인. flag player_hp 20 → WARN 교대. say L-036 → SCAN에서 WARN으로 전환.
```

## 카드 5 — 스폰 디렉터

```
작업: data/spawns.json을 읽는 스폰 디렉터. 기존 적 프리팹 3종(M1/M2/M3)을 그대로 사용, M4 코쿤은 새로 만든다.
- 각 encounter의 trigger를 버스에 구독(&&, first 문법 파싱). one-shot.
- waves: t초 후 또는 or_when_remaining_le 조건. units의 method별 연출:
  VENT 1.2s 선행 해치음 / CRACK 0.8s 선행 균열음 / CRATE 즉시 / COCOON은 씬 로드 시 미리 배치 /
  CEILING 천장 고정 / WALLBREAK lead_s 후 벽 파괴 / CHASE 플레이어 뒤 15m에서 생성, 속도 -0.4.
- 스폰 위치는 svgToWorld. 플레이어 시야각 안 & 12m 이내면 가장 가까운 시야 밖 지점으로 밀어낸다.
- 동시 활성 20 상한, 초과분 대기열.
- 활성 적 0 → "clear_{E-xx}" 발행 → on_clear 실행(say:, spawn_item:, open:duct).
- E-11 chase_stream, E-12 schedule(entry 지점, static 스피터, ends 조건) 별도 처리.
- E-06은 blackout 플래그: 발동 시 R-ENG 조명 전부 off, 1.5s 후 손전등 on.
완료 조건: goto R-ENG; spawn E-06 → 0/8/16초에 각 2마리, 전멸 후 (300,880)에 상자 2개. spawn E-12 → 90초 스케줄 로그.
```

## 카드 6 — 문 · 아이템 · 에너지팩 · 슬롯 콘솔

```
작업: data/doors.json, data/items.json.
- 문: 플레이어 5m 이내 && 루미 3m 이내 → 루미 PANEL 이동 → OPEN 2.2s → 개방 → "door_opened_{ID}".
  initial locked이고 unlock이 flag:면 플래그 감시, lumi_on_approach면 위 규칙, sealed(SD-01)는 event로 breach 연출.
  flag_final_defense=true → 전부 상시 개방.
- 아이템: type별 프리팹. 습득 시 "pickup_{ID}". health는 heal, weapon은 기존 무기 시스템에 지급(어댑터), ammo는 예비탄.
  spawned_by가 있으면 씬 로드 시 숨기고 해당 이벤트에 생성. respawn_box는 S10에서만 활성, 30초 리필.
- 에너지팩: 들면 왼손에 표시(무기 사용 가능). PACK-3은 duct_landing 이벤트로 cracked 상태(누출 파티클).
- 슬롯 콘솔 [168,555]-[194,625]: 6칸 LED. slot_charge 반영. 팩 들고 상호작용 → 3초 충전 → "install_slot{N}".
  PACK-3(cracked)은 51에서 정지 + 경고음.
- 캡슐 B6: timer_launch<=25에서만 "board" 프롬프트.
완료 조건: give pack1; goto R-GAL; 콘솔 상호작용 → 4번 LED 녹색, install_slot4 발행. flag pack3_cracked true 후 6번 → 51 정지.
```

## 카드 7 — 씬 S0 (오프닝)

```
작업: data/scenes.json의 S0 steps를 순서대로 구현. 다른 씬은 만들지 않는다.
누운 카메라, 루미 접근 경로, intro_sequence, 기립 애니메이션, HUD 부착, HK-00 스크립트 회복, D-01 개방, 퀘스트 배너 첫 표시, CP-1.
완료 조건: 새 게임 시작 → 조작 없이 L-001~L-003 → 룩 해금 → 기립 → 체력 10% 표시 → 키트 → 80% → 문 개방 → L-009 → 퀘스트 배너.
```

## 카드 8 — HUD (퀘스트 배너 · 나침반 · 탄약 · 피격)

```
작업: 명세서 4장. 루미 VITALS는 이미 있으므로 나머지.
- 퀘스트 배너 좌상단: quest_set(text, target_svg) 이벤트로 갱신, 1.5s 강조.
- 나침반 상단 중앙: 방위 눈금 + 목표 ◆ + 거리 m.
- 탄약 우하단, 상호작용 프롬프트 중앙, 피격 비네트 + 방향.
- S10 모드: VITALS 없음, 체력은 비네트 강도만.
완료 조건: goto S4 → 배너 '에너지팩 회수 (0/3)', 나침반이 [325,908]을 가리키고 거리 감소.
```

## 카드 9 — 씬 S1~S3 배선

```
작업: scenes.json의 S1, S2, S3 triggers를 버스 구독으로 연결. 새 시스템 만들지 않음.
갤러리 베이 조명 순차 연출(1.2s 간격, L-016 싱크), flag_gallery_briefed, D-22 해금, 퀘스트 갱신, CP-2.
완료 조건: 새 게임에서 S3 종료까지 명세서 8장 흐름대로 진행. 페이크 통로 C-17 진입 시 L-010.
```

## 카드 10 — 씬 S4~S5

```
작업: S4, S5 triggers 배선 + 침몰 단계 1(적색 조명, 기울기 2.5°, 경보 루프) + 루미 자가 충전 연출(4초, 무대사).
완료 조건: goto S4 → 팩 ① 회수 시 암전 → 귀환 추격 → 장착 → L-024/025 → 조명 적색 + 기울기.
```

## 카드 11 — 씬 S6~S7

```
작업: S6, S7 배선 + 소화 폼 연출(시야 5m, 3초) + SD-01 파손 개방 연출 + 침몰 단계 2(4.7°, 스파크, 증기 5곳, 구르는 소품).
완료 조건: goto S6 → 팩 ② 회수 → 폼 + E-08 + SD-01 개방 → 지름길 귀환 중 E-09 → 장착 → 단계 2.
```

## 카드 12 — 씬 S8

```
작업: S8 배선 + 의무실 30초 대사 침묵 + 덕트 낙하 시퀀스(4초, 착지 [450,866]) + PACK-3 균열 연출 + E-11 추격.
완료 조건: goto S8 → 의무실 무전투 → W3 습득 → E-10 3파 → 덕트 → 균열음 → 루미 무대사 → 추격 귀환.
```

## 카드 13 — 씬 S9~S12 (엔딩)

```
작업: S9~S12 배선.
- 51% 정지, L-036~038, 루미 도킹 이동(평소 속도, 프롬프트 없음), L-039~041, 단계 3.
- 루미 OFF(광원·VITALS·문 제어 동시 소실), 90초 타이머, E-12, 스피커 대사, 리스폰 상자, 탑승 규칙(<=25, 2초 자동 탑승, 0초 실패 → CP-9).
- 사출 8초 연출, 심박음만, 크레딧, 에필로그 토글.
검수(명세서 17장 체크리스트) 전 항목 확인.
완료 조건: goto S9 → 엔딩까지 끊김 없이. 도킹 순간 음악 없음, 루미 속도 변화 없음.
```

## 카드 14 — 밸런싱

```
작업: 코드 변경 최소. 탄약 총량 검증 스크립트를 만들어 items.json 배치 탄약 + 드롭 기대값을 합산하고,
적 105마리 × 체력 / (약점 30% 가정 유효 피해)로 필요량을 계산해 1.35배 이상인지 출력.
부족하면 items.json 수치만 조정 제안(코드 수정 아님). AC-3 미회수 시나리오도 별도 계산.
```

---

## 피드백 줄 때의 형식

나쁨: "루미가 이상해" / "좀 더 긴장감 있게"
좋음: "HUD 모드에서 루미가 조준선을 가림. 0.55m→0.7m, 화면 좌하 25%→20%로" / "E-10 W2가 W1 전멸 전에 뜸. or_when_remaining_le 판정이 M4 코쿤을 활성 적으로 세는 듯, 확인"

문제가 생기면: 콘솔 로그 + 재현 명령(`goto S6; spawn E-08`) + 기대/실제를 붙여 넣는다.
