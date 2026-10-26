# ToyBox 충성 · 지지 구현 계획

> **에이전트에게:** superpowers:executing-plans(또는 subagent-driven-development)로 태스크마다 진행한다. 단계는 체크박스(`- [ ]`)다.

**목표:** 점령지의 충성 대상을 내 나라로 바꾸는 단추와 유지, 국민 · 군부 지지율과 세계 시장 여론의 최소 유지.

**구조:** 칸 레코드는 병합(`game_annex`)과 같은 스냅숏 · 꼴 검사로 읽고, 쓰기는 칸마다 dword CAS(`+0` 주인 | `+2` 충성 대상). 새 자리 둘(군부 지지율 `+0x14B08`, 하위 지역의 위 나라 `+0x8c`)은
배율의 찾기 표 `MULTS` 에 14 · 15 번째로 붙인다(예비 인원과 같은 길). 유지는 `keeper` 의 틱에 붙인다(지지 · 여론 0.5초, 점령지 5초).

**기술:** C++(native/srtoybox, Dear ImGui), Python(srkit, pytest + ctypes 의 가짜 게임).

**설계서:** [2026-10-26-toybox-loyalty-design.md](../specs/2026-10-26-toybox-loyalty-design.md) — 계획은 이것에 기대므로 함께 읽는다.

## 지킬 것 (설계서에서 그대로)

- 쓰는 곳은 넷뿐이다: 주인이 플레이어인 칸의 `+2`(word), 플레이어의 `+0x14B08`(군부 지지율), `+0x14B04`(국민 지지율 — 이미 쓴다), `+0x14AF8`(세계 시장 여론 — 이미 쓴다. MORE 의 `opinion[1]`).
- 남의 칸 · 남의 지역 객체(`loyalty` `+0x14B34` 포함), 칸의 주인 `+0`, 게임의 함수 — 쓰지도 부르지도 않는다.
- "내 편" = 충성 대상 L 이 플레이어 인덱스이거나, L 의 지역 객체의 `+0x8c`(word) = 플레이어 인덱스. L = 0 이거나 L ≥ `MAX_REGIONS` 인 칸은 건드리지 않는다. `parentregion`(`+0x76`)은 보지 않는다.
- 쓰는 값 = 플레이어가 가진 "내 편" 칸에 가장 많이 든 L(동률이면 작은 인덱스). "내 편" 칸이 하나도 없으면 **한 칸도 쓰지 않는다**(설계서의 안전장치 — 플레이어 인덱스로 되돌아가지 않는다).
- 지도의 꼴 검사는 `game_annex` 와 같다(칸 수가 너비의 배수 · 세로 ≤ 65535 · 레코드를 통째로 읽음 · 주인과 충성 대상이 모두 `MAX_REGIONS` 미만). 하나라도 아니면 한 칸도 쓰지 않는다.
- 설정 줄: `keep.loyalty`(0/1), `keep.civapproval` · `keep.civapproval.value`, `keep.milapproval` · `keep.milapproval.value`, `keep.opinion` · `keep.opinion.value`(값은 0 … 100, 퍼센트 정수. 기본 0 · 끔).
- 창의 글: `점령지 충성: <N>칸 → <나라 이름>`, `점령지 충성: 바꿀 칸이 없습니다`, `점령지 충성: 내 편인 칸이 없어 쓰지 않았습니다`. 상태 줄의 `유지 중:` 이름: `점령지 충성` · `국민 지지` · `군부 지지` · `여론`.
- **효과의 확인이 먼저다**: 태스크 3 의 게임 안 확인에서 시야 · 자원 · 반란이 나아지지 않으면 거기서 멈추고 사용자에게 알린다(태스크 4 이후를 하지 않고 머지하지 않는다).
- 게임 안 확인은 백그라운드 에이전트로, 임시 `SRTOYBOX_HOME`, 게임은 한 번만, 저장하지 않는다(CLAUDE.md).

## 눈여겨볼 것 (테스트가 따로 지키는 것)

- 하위 지역에 충성하는 본토 칸(서독 등)이 다시 쓰이지 않는다 → 태스크 2 의 `test_mainland_hexes_loyal_to_a_subregion_are_left_alone`.
- `+0x8c` 를 못 찾았을 때 점령지 단추가 꺼진다(본토를 "내 편 아님"으로 오판하지 않는다) → 태스크 1 · 2 의 `..._without_the_parent_cell`.
- 쓰는 사이 게임이 칸을 바꿨으면 그 칸을 건너뛴다(CAS) → 태스크 2 의 `test_a_hex_the_game_changed_is_skipped`.
- 지지율 · 여론 칸이 NaN · 범위 밖이면 유지가 쓰지 않는다 → 태스크 4.
- 유지를 켜도 메뉴 · 멀티플레이 · 오류 가드 뒤에는 쓰지 않는다 → 태스크 4.

---

### 태스크 1: 자리 찾기 — 군부 지지율 · 위 나라 칸

**파일:** `native/srtoybox/locate.h`(MultLayout · MULT_WANTED) · `locate.cpp`(MULTS) · `game.h`/`game.cpp`(MULT_PART_*) · `src/srkit/toybox.py`(MultLayout 미러 · 이름) · `tests/test_toybox_mult.py`

**만드는 것:** `MultLayout::mil_approval`(float, 지역 객체 안 — build 21347933 에서 `0x14B08`), `MultLayout::parent`(word, `0x8c`). `MULT_WANTED = 15`.
`const int MULT_PART_MILAPPROVAL = 6, MULT_PART_PARENT = 7;` — `game_mult_off(part)` 가 못 찾은 까닭을 돌려준다.

- [ ] 서명을 뽑는다: `uv run srkit sig-mine --offset 14b08` · `uv run srkit sig-mine --offset 8c --with 8c`(셋씩, 서로 다른 함수, 치트 코드 밖). `+0x8c` 는 흔한 수라 후보를 `0x741530`(칸의 주인 바꾸기)처럼 "지역 객체의 `+0x8c` word 를 읽어 지역 표를 다시 찾는" 자리로 고른다.
- [ ] 실패하는 테스트: `test_the_addresses_are_found_in_the_installed_game` 의 기대에 `mil_approval == 0x14B08` · `parent == 0x8c` 를 더하고, `test_each_part_says_why_it_is_off` 에 두 부분을 더한다. 돌려 실패를 본다.
- [ ] 표 `MULTS` 에 두 줄(`"mil_approval", "군부 지지율 칸"` · `"parent", "위 나라 칸"`, `ACT_CONSTANT`), 필드 · 미러 · `srkit locate` 출력을 더한다. `uv run srkit toybox-build` 뒤 테스트 통과.
- [ ] 커밋: `feat(toybox): 군부 지지율 · 위 나라 칸 찾기`

### 태스크 2: 점령지 충성 — 게임 쪽

**파일:** `native/srtoybox/game.h`/`game.cpp` · `exports.cpp` · `tests/toybox_fake_game.py`(가짜 지도) · `tests/test_toybox_loyalty.py`(새로)

**만드는 것:**
- `int swap_dword(uint64_t cell, uint32_t was, uint32_t now);` — `swap_qword` 와 같은 꼴(0 바꿨다 · 1 값이 달랐다 · −1 쓸 수 없다).
- `Wrote game_loyalty(int *hexes, int *target);` — 위 "지킬 것"의 규칙. `*hexes` 바꾼 칸 수, `*target` 쓴 충성 대상의 인덱스(못 쓰면 0). 바꿀 칸이 없으면 `Done` 과 0. "내 편"이 없으면 `BadValue`. 지도의 전역(`g_map`)과 `MULT_PART_PARENT` 가 없으면 `Off`. `game_writes()` 가 거짓이면 쓰지 않는다.
- `std::string game_loyalty_off();` — 쓸 수 없는 까닭(빈 글 = 쓸 수 있다).
- 내보내기 `int srtoybox_loyalty(int *hexes, int *target)`(Wrote 의 정수), `srtoybox_loyalty_off(char*, int)`.
- 가짜 게임: 지도 객체(`MAP_HEXES` · `MAP_WIDTH` · `MAP_COUNT`)와 칸 레코드 버퍼를 만드는 `FakeGame.map(width, owners, loyal)` · `hex(i) -> (owner, loyal)`, 지역 객체의 `+parent` word 를 쓰는 `FakeGame.set_parent(index, parent)`.

- [ ] 실패하는 테스트(가짜 판: 독일 176 이 플레이어, 서독 171 · 172 의 `parent` = 176, 덴마크 157, 스웨덴 160):
  - `test_conquered_hexes_become_loyal_to_the_players_side` — 주인 176 · 충성 157 인 칸 셋이 충성 171 이 된다(본토 칸은 171 이 다섯, 172 가 둘 → 171). `hexes == 3`, `target == 171`.
  - `test_mainland_hexes_loyal_to_a_subregion_are_left_alone` — 충성 171 · 172 · 176 인 칸은 그대로.
  - `test_other_countries_hexes_are_left_alone` — 주인 160 · 충성 157 인 칸은 그대로.
  - `test_hexes_loyal_to_nobody_are_left_alone` — 충성 0 인 칸은 그대로.
  - `test_nothing_is_written_without_a_loyal_hex` — 플레이어 칸이 모두 157 에 충성하면 `BadValue`, 한 칸도 안 바뀐다.
  - `test_nothing_is_written_when_the_map_makes_no_sense` — 칸 수가 너비의 배수가 아님 · 충성 대상 ≥ 1024 → `BadValue`, 그대로.
  - `test_a_hex_the_game_changed_is_skipped` — 스냅숏 뒤 · 쓰기 전에 한 칸의 충성을 바꾸는 갈고리(가짜 게임의 `on_peek` 이든 `swap_dword` 를 직접 부르는 시험이든 구현자가 고른다)로, 그 칸은 게임이 쓴 값 그대로.
  - `test_loyalty_is_off_without_the_parent_cell` — `parent = 0` 인 MultLayout 이면 `Off` 이고 까닭이 빈 글이 아니다.
  - `test_nothing_is_written_in_the_menu_or_in_multiplayer`.
- [ ] 돌려 실패를 본다 → 구현 → `uv run srkit toybox-build` → `uv run pytest tests/test_toybox_loyalty.py` 통과.
- [ ] 커밋: `feat(toybox): 점령지 충성 — 칸의 충성 대상을 내 편으로`

### 태스크 3: 점령지 충성 단추 · 게임 안 확인 (멈추는 곳)

**파일:** `native/srtoybox/keeper.h`/`keeper.cpp`(요청 `LOYALTY = -9`) · `ui.cpp`(인구·여론 탭의 "충성 · 지지" 묶음) · `exports.cpp` · `tests/test_toybox_loyalty.py` · 화면 검사의 탭 · 줄 목록 기대(`tests/test_toybox.py` 등 — 깨지는 곳만)

- [ ] 실패하는 테스트: `test_the_loyalty_button_flips_and_tells` — `srtoybox_keeper_request(LOYALTY, 0, 0) == 1` → 틱 → 칸이 바뀌고 창 바닥 `점령지 충성: 3칸 → 서독`(가짜 판의 이름표). 바꿀 칸이 없을 때 · 내 편이 없을 때의 글도 위 "지킬 것"대로. `game_loyalty_off()` 가 비어 있지 않으면 요청을 받지 않는다(0).
- [ ] 구현: 단추는 되돌릴 수 없으니 두 번 누르게 한다(병합과 같은 "한 번 더 누르면 실행합니다"). 못 쓰면 단추 자리에 까닭 한 줄.
- [ ] 전체 `uv run pytest -m "not real_key"` 통과 → 커밋: `feat(toybox): 점령지 충성 단추`
- [ ] **게임 안 확인(백그라운드 에이전트)**: 설계서 "게임 안에서 볼 것"의 1 · 2. 2030 - 세계 · 독일, ToyBox 로 덴마크 병합 → 단추 → `hexdump.py` 식 덤프 앞뒤(옛 덴마크 칸의 `+2` 가 서독 등으로, 본토 · 스웨덴 칸은 그대로), 국토 패널의 "충성 대상 · 충성도" 풍선말, 그 땅 시설의 일시 정지 경고 · 시야 · 자원을 자정 두세 번 동안. 먼저 `peek` 식으로 서독 1401 의 `+0x8c` = 176 을 본다.
- [ ] **나아지지 않으면 여기서 멈추고 사용자에게 알린다.** 나아지면 결과를 설계서의 "게임에서 본 것"에 적고 다음 태스크로.

### 태스크 4: 지지율 · 여론의 최소 유지

**파일:** `game.h`/`game.cpp`(읽기 · 쓰기 셋) · `keeper.h`/`keeper.cpp` · `settings.h`/`settings.cpp` · `ui.cpp` · `exports.cpp` · `tests/test_toybox_loyalty.py`

**만드는 것:** `enum class Floor { Civ = 0, Mil = 1, Opinion = 2 };` `bool game_floor_value(Floor which, float *out);` `Wrote game_write_floor_value(Floor which, float value);`(value 와 지금 값이 0 … 1 의 유한한 수일 때만 — 아니면 `BadValue`. Civ = MORE_APPROVAL 칸, Mil = `MULT_PART_MILAPPROVAL`, Opinion = MORE `opinion[1]`).
`void keeper_set_floor_keep(Floor which, bool on, int percent);`(0 … 100) — `KEEP_EVERY_MS` 마다 지금 값 < percent / 100 이면 그 값으로. 내보내기 `srtoybox_keeper_floor(int which, int on, int percent)`.

- [ ] 실패하는 테스트: `test_each_floor_raises_its_cell_every_tick`(셋 각각 60 % — 0.38 → 0.6, 0.7 은 그대로, 남의 나라 칸 그대로), `test_a_floor_leaves_a_cell_that_makes_no_sense`(NaN · 1.5 → 그대로), `test_floors_wait_in_the_menu_multiplayer_and_after_a_fault`, `test_the_military_floor_is_off_without_its_cell`, `test_the_settings_keep_the_floor_lines`(1 · 100 은 남고 2 · 101 · −1 은 기본으로).
- [ ] 구현(예비 인원의 `reserve_tick` · `send_reserve` · 설정 줄과 같은 꼴) → 빌드 → 테스트 통과 → 커밋: `feat(toybox): 국민 · 군부 지지율과 여론의 최소 유지`

### 태스크 5: 점령지 충성의 유지

**파일:** `keeper.h`/`keeper.cpp` · `settings.*` · `ui.cpp` · `exports.cpp` · `tests/test_toybox_loyalty.py`

**만드는 것:** `const unsigned long long LOYALTY_EVERY_MS = 5000;` `void keeper_set_loyalty_keep(bool on);` — 5초마다 `game_loyalty` 를 부른다. 바꾼 칸이 있을 때만 로그 `유지: 점령지 충성 <N>칸`. 내보내기 `srtoybox_keeper_loyalty(int on)`.

- [ ] 실패하는 테스트(`srtoybox_keeper_tick_at` 으로 시각을 준다): `test_the_loyalty_keep_runs_every_five_seconds`(0 · 4999 ms 에는 한 번, 5000 ms 에 새로 얻은 칸이 바뀐다), `test_the_loyalty_keep_writes_nothing_while_off`, 설정 줄 `keep.loyalty`.
- [ ] 구현 → 빌드 → 테스트 통과 → 커밋: `feat(toybox): 점령지 충성의 유지`

### 태스크 6: 문서 · 마무리 확인 · PR

- [ ] `CLAUDE.md` 의 "ToyBox 가 게임의 메모리에 쓰는 곳"에 **"충성 · 지지"(2026-10-26)** 문단(칸의 `+2` · 군부 지지율 `+0x14B08` · 지지율 · 여론의 유지는 저장되는 설정), `native/srtoybox/game.h` 머리말에 같은 내용.
- [ ] `docs/10-toybox.md`(탭 표 · 기능 · 한계 · 확인 기록), `docs/11-game-internals.md`(지역 객체 표에 `+0x14B08` · `+0x14B34` · `+0x8c` · `+0x76`, 칸 레코드의 `+2`, 키워드 표), `README.md` 의 인구·여론 줄.
- [ ] 게임 안 확인(백그라운드 에이전트): 설계서의 3 · 4 · 5(유지 60 % 가 자정 뒤에도 남는가, 새로 병합한 땅이 5초 안에 따라오는가, 남의 값 · 사용자 `toybox.ini` 그대로). 결과를 설계서와 docs/10 에 적는다.
- [ ] `uv run pytest -m "not real_key"` 통과 → `feat/toybox-loyalty` 를 푸시하고 `develop` 으로 PR(본문에 테스트 결과).
