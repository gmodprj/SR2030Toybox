// 올라와 있는 게임 실행 파일에서 ToyBox 가 쓰는 주소를 찾는다. 주소를 코드에 적어 두지 않는다.
// 창도 게임의 상태도 모르는 순수 함수다. 읽다가 예외가 나면(읽을 수 없는 쪽) "못 찾았다"로 친다.
//
// 찾는 길이 둘이다(docs/11-game-internals.md):
//   새 찾기 locate_state   게임 상태를 읽는 전역 일곱. 내장 치트와 무관한 코드의 서명으로(sigs.h) — 게임이 치트를 없애도 된다.
//           locate_values  값을 읽고 쓰는 자리(국고 칸, 재고 칸 …). 같은 규칙의 서명으로.
//           locate_more    더 쓰는 값의 자리(기술 수준, 세계 시장 여론, 관계). 기능마다 한 묶음 — 따로 찾고 따로 실패한다.
//           locate_research 연구: 기술 · 부대 설계의 표, 지역별 연구 목록, 지역의 효과를 다시 셈하는 게임의 함수.
//   옛 찾기 locate_legacy  치트 명령 처리 함수와 그 둘레의 셋. "cheat allowcheats" 문자열을 닻으로.
//                          **실행 중인 ToyBox 는 부르지 않는다**(2026-10-11) — 검증 도구(gamedrive.py peek 의 치트 허용 비트)와 테스트의 대조만 쓴다.
#pragma once

#include <cstddef>
#include <cstdint>

// 모두 이미지의 시작으로부터의 거리(RVA). 0 이면 찾지 못한 것이다.
struct GameAddresses {
    uint32_t handler;          // [옛 찾기] 명령 처리 함수: void f(void *context, const char *line)
    uint32_t context;          // [옛 찾기] 그 함수의 첫 인자로 넘기는 전역 객체(게임이 넘기는 것과 같은 주소)
    uint32_t multiplayer;      // byte. 0 이 아니면 멀티플레이다
    uint32_t options;          // [옛 찾기] dword. 비트 0x40 = 치트 허용
    uint32_t program_state;    // dword. 1 = 월드가 돌고 있다
    uint32_t mode_state;       // dword. 2 = 게임
    uint32_t player_index;     // dword. 플레이어 지역의 인덱스(지역 표의 칸)
    uint32_t player_pointer;   // qword. 플레이어 지역 객체
    uint32_t region_table;     // qword × 1024. 인덱스순 지역 객체 포인터
    uint32_t region_count;     // dword. 지역 표의 마지막 인덱스
};

const int STATE_WANTED = 7;     // 상태 묶음에서 찾을 것의 수
const int STATE_SIGS = 3;       // 찾을 것마다의 서명 수
const int STATE_NEED = 2;       // 그 가운데 정확히 한 번 맞아야 하는 수

// 서명 하나의 결과(로그와 srkit locate 가 쓴다).
struct SigRow {
    const char *name;           // 찾을 것(GameAddresses 의 필드 이름)
    const char *text;           // 서명 글
    int count;                  // 실행 구역에서 맞은 횟수(2 에서 멈춘다)
    uint32_t at;                // 처음 맞은 자리
    uint32_t value;             // 거기서 읽어 낸 주소나 상수(둘을 읽는 서명이면 첫째 — 간격)
    uint32_t value2;            // 둘째로 읽어 낸 상수(첫 칸). 없으면 0
};

// image: RVA 대로 펼쳐진 실행 파일(올라와 있는 모듈의 시작 주소), size: 그 크기.

// 새 찾기: 찾을 것마다 서명 STATE_SIGS 개 가운데 STATE_NEED 개 이상이 정확히 한 번 맞고 같은 주소를 내야 한다.
// 찾은 주소는 쓸 수 있는 자료 구역 안이어야 하고 서로 겹치지 않아야 한다.
// 찾으면 true 와 out 의 일곱 필드(handler · context · options 는 건드리지 않는다). 못 찾으면 false 와 why(UTF-8).
// rows: STATE_WANTED * STATE_SIGS 칸(서명마다의 결과)이거나 nullptr.
bool locate_state(const uint8_t *image, size_t size, GameAddresses *out, SigRow *rows, char *why, size_t why_size);

// 값 묶음: 플레이어의 국고와 물자 재고를 읽고 쓰는 자리. world_pointer 만 RVA 이고 나머지는 객체 안의 자리다.
struct ValueLayout {
    uint32_t world_pointer;    // qword. 세계 자료 객체("이 물자를 쓰는가"의 표가 그 안에 있다)
    uint32_t treasury;         // 지역 객체 안. double, 달러
    uint32_t stock_first;      // 지역 객체 안. 첫 물자의 재고(float)
    uint32_t stock_step;       // 물자 사이의 간격
    uint32_t used_first;       // 세계 자료 객체 안. 첫 물자의 "쓰는가"(float. 0 보다 크면 이번 판에서 쓴다)
    uint32_t used_step;
};

const int VALUE_WANTED = 4;     // 값 묶음에서 찾을 것의 수: 세계 자료 포인터, 국고 칸, 재고 칸(간격 · 첫 칸), 쓰는 물자 표(간격 · 첫 칸)
const int STOCK_SLOTS = 12;     // 재고의 칸 수. 코드에서 한 가지 꼴로 읽어 낼 자리가 없어 상수로 둔다(docs/11-game-internals.md)

// 새 찾기(값 묶음): 규칙은 locate_state 와 같다. 찾으면 true 와 out. 못 찾으면 false 와 why(UTF-8) — out 은 그대로다.
// 읽어 낸 값이 말이 되는지도 본다: 포인터는 쓸 수 있는 자료 구역 안의 8 의 배수 자리, 자리는 0 보다 크고 0x100000 보다 작다,
// 국고 칸은 8 의 배수 · 표의 첫 칸과 간격은 4 의 배수, 국고 칸(8바이트)과 재고 칸들(4바이트 × STOCK_SLOTS)이 겹치지 않는다.
// rows: VALUE_WANTED * STATE_SIGS 칸(서명마다의 결과)이거나 nullptr.
bool locate_values(const uint8_t *image, size_t size, ValueLayout *out, SigRow *rows, char *why, size_t why_size);

// 두 묶음을 함께 본다(둘 다 찾은 뒤에): 세계 자료 포인터가 상태 전역 일곱 가운데 어느 것과도 겹치지 않아야 한다.
// 겹치면 false 와 why(UTF-8) — 값 묶음을 못 찾은 것으로 친다(상태 묶음은 그대로 쓴다).
bool locate_fits(const GameAddresses &state, const ValueLayout &values, char *why, size_t why_size);

// 더 쓰는 값(3단계 2 · 4): 모두 지역 객체 안의 자리(float)다. 필드는 서명 표의 순서대로 uint32_t 열이다.
struct MoreLayout {
    uint32_t tech;             // [지식] 기술 수준
    uint32_t opinion[3];       // [여론] 세계 시장 여론과 그 둘레의 세 칸
    uint32_t relation[2];      // [관계] 지역 인덱스로 찾는 표 둘의 첫 칸: 그 지역과의 관계(-1 … 1)
    uint32_t casus;            // [관계] 같은 꼴의 표: 그 지역에 대한 전쟁 명분(0 … 1)
    uint32_t people[2];        // [인구] 인구 칸(게임이 자정마다 다시 쓴다)과, 그 셈에 그대로 더해지는 풀(다시 쌓이지 않는 칸)
    uint32_t approval;         // [지지율] 국내 지지율(0 … 1)
};

const int MORE_TECH = 1, MORE_OPINION = 2, MORE_RELATIONS = 4, MORE_PEOPLE = 8, MORE_APPROVAL = 16;   // 묶음의 비트
const int MORE_GROUPS = 5;      // 묶음의 수(지식 · 여론 · 관계 · 인구 · 지지율 순)
const int MORE_WANTED = 10;     // 찾을 것의 수: 기술 수준 칸, 여론 칸 셋, 관계 표 둘, 전쟁 명분 표, 인구 칸 둘, 지지율 칸
const int REGION_SLOTS = 1024;  // 지역 표의 칸 수 = 관계 표 하나의 칸 수
const size_t MORE_WHY = 160;    // 까닭 한 줄의 크기

// 부르는 게임의 함수(3단계 4): 게임이 스스로도 부르는 함수를, 그것을 부르는 자리(치트 함수 밖)의 서명으로 찾는다. 필드는 함수의 RVA.
struct ActLayout {
    uint32_t colonize;         // void f(void *지역 객체, int 다른 지역의 인덱스, bool): 그 지역이 다른 지역을 식민지로 삼는다
    uint32_t fight;            // void f(void *지역 객체, bool, int 다른 지역의 인덱스, bool, bool): 그 지역이 다른 지역과 전쟁에 들어간다
    uint32_t become;           // void f(void *지역 객체, int 0): 플레이하는 나라를 바꾼 뒤 게임이 그 지역으로 화면을 맞춘다
    uint32_t elect;            // void f(void *지역 객체, int 0, int 0): 그 지역의 새 지도자를 세운다(게임이 지도자가 죽었을 때 부른다)
    uint32_t news;             // void f(void *지역 객체, int 알림 번호 × 4, bool, void *, int × 6, bool): 알림 · 뉴스를 낸다
    uint32_t map_pick;         // qword(자료). 그 포인터가 가리키는 word 가 "지도에서 고른 지역"의 인덱스다 — 읽기만 한다
    uint32_t player_index2;    // dword(자료). 플레이어의 인덱스의 둘째 사본 — "이 나라로 플레이"가 첫째(GameAddresses.player_index)와 함께 쓴다
    uint32_t player_pointer2;  // qword(자료). 플레이어의 지역 객체를 가리키는 둘째 포인터 — 〃 (첫째는 GameAddresses.player_pointer).
                               // 판을 시작한 날에는 첫째와 같고, 첫 자정부터는 게임이 그 지역의 전날 사본으로 돌린다
    uint32_t leader_table;     // qword(자료). 지도자 표(레코드 LEADER_SIZE 바이트)의 포인터
    uint32_t leader_id;        // 상수: 지역 객체 안에서 그 지역의 지도자 번호(word)가 든 자리
    uint32_t treaty;           // void f(void *지역 객체, bool 쪽, int 조약 번호, bool 맺는다, int 다른 지역의 인덱스, bool, bool, bool): 조약을 맺거나 깬다
    uint32_t treaties;         // 상수: 지역 객체 안의 표 — 조약 번호마다 qword 포인터(그 조약을 맺은 지역들의 비트 묶음. 비트의 번호가 지역 인덱스)
    uint32_t annex;            // void f(HexRef *칸, int 새 주인의 인덱스, int 0, int 0, bool, bool): 그 칸의 주인을 바꾼다
    uint32_t map;              // qword(자료). 지도 객체의 포인터 — 읽기만 한다
    uint32_t spawn;            // void *f(void *지역 객체, int 부대 설계의 번호, int 수, uint32 칸, bool, bool, void **, bool, int, bool): 그 칸에 부대를 만든다
    uint32_t map_hex;          // dword(자료). "지도에서 고른 칸" — 낮은 word 가 가로, 높은 word 가 세로. 0 이면 고르지 않았다. 읽기만 한다
    uint32_t picked_units;     // qword(자료). 게임에서 고른 부대들의 목록의 첫 노드(없으면 0). 노드: +8 다음 노드, +0x18 부대 객체. 읽기만 한다
    uint32_t upgrades;         // 상수: 세계 객체 안의 "부대 개량의 보정" 표에서 첫 레코드의 깃발(dword)의 자리. 읽기만 한다
    uint32_t game_over;        // dword(자료). 판이 끝난 꼴(0: 진행 중. 내장 치트 instantwin 은 1 을 쓴다) — "즉시 승리"가 쓴다
    uint32_t winners;          // qword(자료). 이긴 지역들의 비트 묶음(OWNERS_BYTES 바이트, 비트의 번호가 지역 인덱스)의 포인터. 없으면 0 — 〃
    uint32_t ui_flags;         // dword(자료). 화면의 깃발들 — 비트 GUI_HIDDEN 이 "GUI 를 숨긴다". "GUI 숨기기/보이기"가 그 비트를 뒤집는다
    uint32_t gui;              // qword(자료). GUI 객체의 포인터(없으면 0). 읽기만 한다
    uint32_t gui_vtable;       // 그 객체의 가상 함수 표(읽기 전용 자료의 RVA). 객체의 표가 이것일 때만 표의 GUI_SET 번째 함수를 부른다
    uint32_t deploy_hex;       // uint32 *f(void *지역 객체, uint32 *out, int 유형): 그 지역이 그 유형(설계의 DESIGN_MOVE)의 부대를 내놓는 칸을 out 에 적고 돌려준다
    uint32_t units;            // qword(자료). 세계의 모든 부대(시설 포함)의 목록의 첫 부대(없으면 0). 부대의 UNIT_NEXT 로 이어진다. 읽기만 한다
    uint32_t region_name;      // 상수: 지역 객체 안에서 그 지역의 이름 글(이번 판 · 지금 언어의 것)의 포인터(qword)가 든 자리. 읽기만 한다
};

// 찾을 것의 비트(ActLayout 의 필드 순서)
const int ACT_COLONIZE = 1, ACT_FIGHT = 2, ACT_BECOME = 4, ACT_ELECT = 8, ACT_NEWS = 16, ACT_MAP_PICK = 32, ACT_INDEX2 = 64,
          ACT_POINTER2 = 128, ACT_LEADERS = 256, ACT_LEADER_ID = 512, ACT_TREATY = 1024, ACT_TREATIES = 2048,
          ACT_ANNEX = 4096, ACT_MAP = 8192, ACT_SPAWN = 16384, ACT_MAP_HEX = 32768,
          ACT_PICKED_UNITS = 65536, ACT_UPGRADES = 131072, ACT_GAME_OVER = 262144,
          ACT_WINNERS = 524288, ACT_UI_FLAGS = 1048576, ACT_GUI_OBJECT = 2097152, ACT_GUI_VTABLE = 4194304, ACT_DEPLOY = 8388608,
          ACT_UNITS = 16777216, ACT_REGION_NAME = 33554432;
const int ACT_GUI = ACT_UI_FLAGS | ACT_GUI_OBJECT | ACT_GUI_VTABLE;   // GUI 숨기기/보이기: game_act_off 에 주는 이름
const uint32_t GUI_HIDDEN = 0x20000;        // 화면의 깃발에서 "GUI 를 숨긴다"
const uint32_t GUI_OBJECT_FLAGS = 0x2C;     // dword. GUI 객체의 깃발(표를 찾는 서명에 이 자리가 박혀 있다). 비트 GUI_OBJECT_HIDDEN 이 숨김
const uint32_t GUI_OBJECT_HIDDEN = 0x10000;
const int GUI_SET = 5;                      // 가상 함수 표에서 "깃발을 바꾼다"의 자리
const int ACT_VICTORY = ACT_GAME_OVER | ACT_WINNERS;   // 즉시 승리: game_act_off 에 주는 이름
const unsigned VICTORY_MESSAGE = 0x8002, VICTORY_WPARAM = 0x63;   // 게임 창에 보내는 메시지(내장 치트 instantwin 이 PostMessageA 로 보내는 그대로)
const int ACT_BEST = ACT_DEPLOY | ACT_SPAWN;   // 최강 부대 묶음: game_act_off 에 주는 이름(배치 칸 함수와 부대 만들기 함수)
const int ACT_SUPPLY = ACT_PICKED_UNITS | ACT_UPGRADES;   // 보급 채우기 · 비우기: game_act_off 에 주는 이름(둘 다 찾아야 한다)
// 부대의 상태 쓰기: game_act_off 에 주는 이름 — 고른 부대에(ACT_PICKED_UNITS) / 내 부대 전부에(ACT_UNITS) / 내 부대 전부의 보급(ACT_SUPPLY | ACT_UNITS)
const int ACT_SUPPLY_ALL = ACT_SUPPLY | ACT_UNITS;
const int SPAWN_MAX_EQUIP = 21999;   // 게임이 받는 부대 설계 번호의 끝(내장 치트 spawnunit 의 검사와 같다)
const int SPAWN_LIMIT = 200;         // ToyBox 가 한 번에 만드는 부대의 끝(설계의 수 × 수량)
const int ACT_WANTED = 26;
const uint32_t UNIT_NEXT = 0x08;            // qword. 세계의 부대 목록에서 다음 부대
const uint32_t UNIT_FACILITY = 0x2000;      // UNIT_FLAGS4 의 비트: 시설이다(부대가 아니다)
const uint32_t UNIT_UNBUILT = 0x800;        // 〃: 아직 지도에 없는 부대(칸 (0, 0), 지휘 패널의 수에 들지 않는다 — 생산 중으로 보인다). "내 부대 전부"에서 뺀다
// 부대의 상태 셋(float, 0 … 1) — 게임의 부대 카드의 막대. 새 판에서의 값의 분포로 가렸다(효율 0.72 … 1 · 사기 0.8 … 1 은 모든 부대에, 경험은 거의 0)
const uint32_t UNIT_EFFICIENCY = 0x9C, UNIT_EXPERIENCE = 0xA0, UNIT_MORALE = 0xA4;
const int STAT_EXPERIENCE = 0, STAT_MORALE = 1, STAT_EFFICIENCY = 2, STAT_COUNT = 3;   // game_unit_stat 의 "무엇을"
const int UNITS_LIMIT = 500000;             // 세계의 부대 목록에서 이만큼까지만 본다
// 최강 부대 묶음(내장 치트 darran 의 "받기" 쪽): 병과마다 이 능력치(설계의 DESIGN_STATS + 4 × 번호, float)가 가장 높은 설계를 고른다.
// −1 인 병과(수송 · 미사일 · 공중 수송)는 받지 않는다. 치트의 표(실행 파일의 자료) 그대로다 — build 21347933.
const int BEST_CLASSES = 20;
const int BEST_STAT[BEST_CLASSES] = {0, 0, 1, 1, 1, 1, -1, 1, -1, 4, 1, 3, 2, 4, -1, 7, 6, 6, 6, 7};
const uint32_t DESIGN_STATS = 0x84;         // float × … 능력치들
const uint32_t DESIGN_MOVE = 0x12;          // byte. 유형(배치 칸 함수의 셋째 인자)
const int BEST_EACH_MAX = 10;               // 병과마다 받는 수의 끝(치트는 7)
// 부대 객체의 꼴(보급 채우기 — 내장 치트 stranded 가 읽고 쓰는 자리들. 서명에 박혀 있지 않다 — 쓰기 전에 설계 포인터가 설계 표 안인지로 지킨다)
const uint32_t UNIT_READ = 0x238;           // 읽을 크기(설계 포인터까지)
const uint32_t UNIT_OWNER = 0x1E;           // word. 그 부대의 주인인 지역의 인덱스
const uint32_t UNIT_STORE_A = 0x80;         // float. 보급의 첫째 칸(가득 = 규모 × 설계의 DESIGN_CAP_A × 보정) — ToyBox 가 쓴다
const uint32_t UNIT_STORE_B = 0x84;         // float. 보급의 둘째 칸(가득 = 규모 × 설계의 DESIGN_CAP_B × 보정) — ToyBox 가 쓴다
const uint32_t UNIT_HELD = 0x88;            // float. 0 일 때만 실은 부대의 몫을 둘째 칸에서 뺀다
const uint32_t UNIT_SIZE = 0x90;            // float. 부대의 규모
const uint32_t UNIT_SLOTS = 0xDC;           // byte × 8. 개량의 단계(0 이면 없다)
const uint32_t UNIT_TECHS_ON = 0xE7;        // byte. 0 이 아니면 설계의 기술 여덟의 효과를 곱한다
const uint32_t UNIT_FLAGS4 = 0xF4;          // dword. 0x800: 실린 부대일 때 그 부대의 둘째 칸을 세지 않는다
const uint32_t UNIT_FLAGS = 0xF8;           // byte. 0x40: 개량의 보정을 곱한다
const uint32_t UNIT_CARGO = 0x130;          // qword. 실은 부대(그 부대의 +0x130 으로 이어진다)
const uint32_t UNIT_CARRIER = 0x138;        // qword. 이 부대를 실은 부대
const uint32_t UNIT_DESIGN = 0x230;         // qword. 부대 설계 레코드(부대 설계 표 안)
const uint32_t DESIGN_CLASS = 0x08;         // byte. 병과
const uint32_t DESIGN_TECHS = 0x38;         // word × 8. 효과를 주는 기술의 번호
const uint32_t DESIGN_CARGO_USE = 0x58;     // float. 실릴 때 차지하는 몫(규모를 곱한다)
const uint32_t DESIGN_CARGO_CAP = 0x60;     // float. 실을 수 있는 몫
const uint32_t DESIGN_CAP_A = 0x78;         // float. 첫째 칸의 용량
const uint32_t DESIGN_CAP_B = 0x80;         // float. 둘째 칸의 용량
const uint32_t TECH_EFFECTS = 0x0C;         // word × 4. 기술의 효과 번호
const uint32_t TECH_EFFECT_VALUES = 0x14;   // float × 4. 그 효과의 값(1 을 더해 곱한다)
const uint16_t EFFECT_CAP_A = 0xA6, EFFECT_CAP_B = 0xA5;
const uint32_t UPGRADE_SIZE = 0x78;         // 개량의 보정 레코드. 번호 = 병과 × 25 + 3 × (칸 + 1) + max(2, 단계 − 1)
const uint32_t UPGRADE_VALUES = 0x68;       // 깃발에서 이만큼 앞이 값(float)들의 0번째다. 깃발의 비트 n 이 켜져 있으면 값 n 은 곱하지 않고 그대로 쓴다
const int UPGRADE_CARGO = 14, UPGRADE_CAP_A = 16, UPGRADE_CAP_B = 18;   // 값 · 비트의 번호
const uint32_t PICKED_NEXT = 0x08, PICKED_UNIT = 0x18;                  // 고른 부대의 목록의 노드
const int PICKED_LIMIT = 4096;              // 목록에서 이만큼까지만 본다      // 찾을 것의 수(ActLayout 의 필드 순서와 같다). 함수 · 자료의 주소 · 상수가 섞여 있다(표 ACTS 가 종류를 안다)
// 지도 객체의 꼴 — 칸 수 · 너비의 자리는 지도 객체를 찾는 서명에 박혀 있다(cmp …, [… + 0x88] / mov …, [… + 0x80])
const uint32_t MAP_HEXES = 0x00;            // qword. 칸 레코드(HEX_SIZE 바이트 × 칸 수)의 포인터. 레코드의 첫 word 가 주인 지역의 인덱스
const uint32_t MAP_SIDE_A = 0x08;           // qword. 칸마다 8바이트인 배열의 포인터(게임의 "칸" 묶음에 그 칸의 주소가 들어간다)
const uint32_t MAP_SIDE_B = 0x10;           // qword. 〃
const uint32_t MAP_WIDTH = 0x80;            // dword. 가로 칸 수
const uint32_t MAP_COUNT = 0x88;            // dword. 칸 수(가로 × 세로)
const uint32_t HEX_SIZE = 16;
const int TREATY_ALLIANCE = 1;  // 조약 번호: 동맹(내장 치트 treaty 의 기본값 — 맺으면 조약 13종이 함께 맺어진다)
// 지도자 표의 꼴 — 표를 찾는 서명 셋에 박혀 있다(imul …, 0xC8 / or dword ptr […+0x28], …)
const uint32_t LEADER_SIZE = 0xC8;          // 지도자 레코드
const uint32_t LEADER_FLAGS = 0x28;         // dword. 깃발
const uint32_t LEADER_GONE = 2;             // 그 깃발: 죽었다 — 게임이 날마다 이 비트를 보고 새 지도자를 세운다
const int NEWS_LEADER_GONE = 0x65C;         // 알림 번호: 지도자의 죽음과 권력 이양(알림 함수를 찾는 서명 하나에 박혀 있다)

// 새 찾기(부르는 함수): 함수마다 서명 셋이 모두 정확히 한 번 맞고 같은 주소를 내야 하며, 그 주소가 함수 표에 있는 함수의 시작이어야 한다.
// 자료의 주소는 쓸 수 있는 자료 구역에 있고 제 크기(4 · 8)의 배수여야 한다. 상수(구조체 안의 자리)는 0 보다 크고 0x100000 보다 작아야 한다.
// 돌려주는 값은 찾은 함수의 비트. 못 찾은 것의 필드는 0, why 의 그 줄에 까닭(UTF-8).
// rows: ACT_WANTED * STATE_SIGS 칸이거나 nullptr.
int locate_acts(const uint8_t *image, size_t size, ActLayout *out, SigRow *rows, char (*why)[MORE_WHY]);

// 새 찾기(더 쓰는 값): 서명의 규칙은 locate_state 와 같다. 돌려주는 값은 찾은 묶음의 비트다 — 찾은 묶음의 필드만 채우고
// 못 찾은 묶음의 필드는 0 으로 둔다. 한 묶음은 그 안의 것을 모두 찾아야 찾은 것이다(반쪽 묶음은 없다).
// why 는 MORE_GROUPS 줄: 못 찾은 묶음의 까닭(UTF-8). 찾은 묶음은 빈 글.
// 읽어 낸 자리가 말이 되는지도 본다: 0 보다 크고 0x100000 보다 작은 4 의 배수, 칸(4바이트)과 표(4바이트 × REGION_SLOTS)가
// 서로 겹치지 않는다 — 겹치면 어느 쪽이 엉뚱한 것을 읽었는지 모르므로 두 묶음 다 버린다.
// values 를 주면(값 묶음을 찾았을 때) 국고 칸 · 재고 칸들과 겹치는 묶음도 버린다(값 묶음은 그대로 둔다).
// rows: MORE_WANTED * STATE_SIGS 칸(서명마다의 결과)이거나 nullptr.
int locate_more(const uint8_t *image, size_t size, const ValueLayout *values, MoreLayout *out, SigRow *rows, char (*why)[MORE_WHY]);
// 찾을 것(표의 wanted 째. rows 의 칸 번호 / STATE_SIGS)이 든 묶음: 0 지식, 1 여론, 2 관계.
int locate_more_group(int wanted);

// 배율(묶음 7 — docs/superpowers/specs/2026-10-11-toybox-stage7-design.md). 앞의 둘은 전역의 RVA, 나머지는 지역 객체 안의 자리다.
struct MultLayout {
    uint32_t mul_table;        // float. 곱 보정 표의 첫 칸: 지역 인덱스마다 MUL_STEP 바이트, 그 안에 MUL_CELLS 칸(칸 번호 = 기술 효과 번호).
                               // ToyBox 는 플레이어의 줄의 칸에만 쓴다
    uint32_t day;              // dword. 날마다 도는 게임의 함수가 1 씩 올린다. 읽기만 한다
    uint32_t tax_total;        // double. 연간 세수의 합(÷ 365 = 재무 패널의 "일일 세금"). 읽기만 한다 — 아래도
    uint32_t army_pay_level;   // float. 군 인건비의 지출 수준(1.0 = 100 %)
    uint32_t army_upkeep_level;   // float. 군 유지비의 지출 수준
    uint32_t army_pay;         // float. 연간 군 인건비의 바탕
    uint32_t army_upkeep;      // float. 연간 군 유지비의 바탕
    uint32_t unit_cost;        // float. 어제의 부대 생산비(하루치)
    uint32_t research_step;    // float. 연구의 하루치 — 게임이 진행 중인 노드마다 남은 기간에서 이만큼 뺀다
    uint32_t social_cost;      // float × SOCIAL_COUNT. 사회 지출 여덟 항목의 연간 비용의 바탕
    uint32_t social_level;     // float × SOCIAL_COUNT. 그 항목들의 지출 수준 — 비용 × 수준 ÷ 365 가 재무 패널의 여덟 줄이다
    uint32_t social_grade;     // float × SOCIAL_COUNT. 그 항목들의 품질 등급(0 … 2 — 게임의 사회 지출 화면의 "품질 등급 NN.N %" = × 100).
                               // ToyBox 가 플레이어의 것에 쓴다(docs/superpowers/specs/2026-10-21-toybox-social-grades-design.md)
    uint32_t reserve;          // float. 예비 인원(지휘 패널의 "예비 인원"). 쌓이는 칸 — 게임이 더하고 뺀다. ToyBox 가 플레이어의 것에 쓴다(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md)
    uint32_t mil_approval;     // float. 군부 지지율(0 … 1. CVP 의 milapproval). 날마다 다시 셈해진다. ToyBox 가 플레이어의 것에 최소 유지로 쓴다
    uint32_t group;            // word. 묶인 하위 지역(서독 등)이 속한 나라의 **지역 번호**(서독 1401 → 1499. 살아 있는 나라는 0). 읽기만 한다 —
                               // 점령지 충성의 "내 편" 판정. 2026-10-27 게임에서 봤다(+0x8c 는 해외 영토 → 본국의 인덱스였다)
                               // (docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md)
};

const int MULT_WANTED = 15;                 // 찾을 것의 수(MultLayout 의 필드 순서와 같다)
const int MULT_TABLE = 1;                   // 찾은 것의 비트: 곱 보정 표 — 물자의 생산 · 소비의 배율
const int MULT_DAY = 2, MULT_TAX = 4, MULT_ARMY = 8 | 16 | 32 | 64 | 128, MULT_STEP_BIT = 256;
const uint32_t MUL_STEP = 0x320;            // 곱 보정 표에서 지역 하나의 줄(표를 찾는 서명 둘에 박혀 있다)
const int MUL_CELLS = 200;                  // 그 줄의 칸 수
const int MUL_RAW = 24, MUL_USE = 60, MUL_OUT = 72;   // 물자 i 의 산업 원자재 소비 · 인구 소비 · 산출의 칸(+ i, i = 0 … MULT_GOODS - 1)
// 유닛 생산 · 시설 건설 속도(기술 효과 15 · 16 — 날마다의 진행에 곱한다, 0x843567 · 0x77fb14). 15 는 2026-10-15 에 뺐다가 되살렸다 —
// 게임에서 ×5 · ×20 이 들었고 ×1000 은 듣지 않았다(docs/superpowers/specs/2026-10-19-toybox-mult-unit-caps-design.md)
const int MUL_UNIT_BUILD = 15, MUL_FAC_BUILD = 16;
const int SOCIAL_COUNT = 8;                 // 사회 지출의 항목 수(보건 · 교육 · 인프라 · 환경 · 가족 수당 · 치안 · 문화 지원 · 사회 보장)
const int MULT_GOODS = 11;                  // 배율을 두는 물자의 수(재고 칸의 앞 열하나 — 열두째 칸은 쓰지 않는다)
const uint32_t NODE_LEFT = 0x28;            // float. 연구 노드의 남은 기간(일) — 연구 속도 배율이 쓴다
const uint32_t NODE_ACTIVE = 0x40000000;    // 둘째 깃발: 진행 중이다(게임이 날마다 남은 기간을 줄이는 노드)

// 새 찾기(배율): 찾을 것마다 서명 셋 가운데 STATE_NEED 개 이상이 정확히 한 번 맞고 같은 값을 내야 한다. 따로 찾고 따로 실패한다.
// 전역 둘은 쓸 수 있는 자료 구역 안의 4 의 배수 자리(표는 MUL_STEP × REGION_SLOTS 바이트가 모두), 자리는 0 보다 크고 0x100000 보다 작다.
// 돌려주는 값은 찾은 것의 비트. 못 찾은 것의 필드는 0, why 의 그 줄에 까닭(UTF-8). rows: MULT_WANTED * STATE_SIGS 칸이거나 nullptr.
int locate_mult(const uint8_t *image, size_t size, MultLayout *out, SigRow *rows, char (*why)[MORE_WHY]);

// 연구(3단계 3). 앞의 넷은 전역의 RVA, world 는 전역 객체의 RVA, lists 는 그 객체 안의 자리, recompute 는 함수의 RVA 다.
struct ResearchLayout {
    uint32_t tech_table;       // qword. 기술 표(레코드 TECH_SIZE 바이트 × 자리 수)의 포인터
    uint32_t tech_count;       // dword. 기술 표의 자리 수
    uint32_t design_table;     // qword. 부대 설계 표(레코드 DESIGN_SIZE 바이트 × 자리 수)의 포인터
    uint32_t design_count;     // dword. 설계 표의 자리 수
    uint32_t world;            // 세계 객체(큰 전역 구조체)의 시작. 지역 표와 연구 목록이 그 안에 있다
    uint32_t lists;            // 세계 객체 안. 지역 인덱스마다 LIST_STEP 바이트 — 그 지역의 연구 목록(맨 앞이 머리 노드의 포인터)
    uint32_t recompute;        // void f(void *세계 객체, int 지역 인덱스): 그 지역의 효과를 다시 셈한다(-1 이면 모든 지역)
};

const int RESEARCH_WANTED = 7;  // 찾을 것의 수(ResearchLayout 의 필드 순서와 같다)
const int RESEARCH_NEED = 3;    // 찾을 것마다 서명 셋이 모두 정확히 한 번 맞아야 한다 — 한 서명에만 박힌 꼴의 상수도 지켜지게

// 표와 목록의 꼴. 읽어 내지 않고 상수로 둔다 — 대신 이 상수가 박힌 명령을 서명에 넣었다(locate.cpp 의 표 RESEARCH,
// docs/11-game-internals.md). 꼴이 바뀐 빌드에서는 서명이 맞지 않아 묶음을 못 찾는다.
const uint32_t TECH_SIZE = 0x88;            // 기술 레코드. 번호가 곧 자리다
const uint32_t TECH_KIND = 0x00;            // byte. 분류(1 … 6). 0 이면 빈 자리
const uint32_t TECH_LEVEL = 0x01;           // byte. 기술 수준
const uint32_t TECH_NEEDS = 0x04;           // word × TECH_NEED_COUNT. 선행 기술의 번호(0 이면 없다)
const int TECH_NEED_COUNT = 2;
const uint32_t TECH_OWNERS = 0x50;          // qword. 보유 비트 묶음의 포인터(아무도 보유하지 않았으면 0)
const uint32_t DESIGN_SIZE = 0x168;         // 부대 설계 레코드. 번호가 곧 자리다
const uint32_t DESIGN_NAME = 0x00;          // qword. 이름 글의 포인터. 0 이면 빈 자리
const uint32_t DESIGN_YEAR = 0x0A;          // byte. 등장 연도 - 1900 — 〃
const uint32_t DESIGN_OPEN = 0x20;          // word. 0 이면 연구 대상이 아니다
const uint32_t DESIGN_NEEDS = 0x34;         // word × DESIGN_NEED_COUNT. 선행 기술의 번호
const int DESIGN_NEED_COUNT = 4;
const uint32_t DESIGN_HOLD_A = 0xEC;        // dword. DESIGN_HOLD_A_BIT 이 켜진 설계는 게임이 "기술을 뺄 때" 건드리지 않는다
const uint32_t DESIGN_HOLD_A_BIT = 0x1;
const uint32_t DESIGN_HOLD_B = 0xF0;        // dword. DESIGN_HOLD_B_BIT 〃
const uint32_t DESIGN_HOLD_B_BIT = 0x1000000;
const uint32_t DESIGN_OWNERS = 0xF8;        // qword. 보유 비트 묶음의 포인터
const uint32_t TECH_MASK = 0x28;            // qword. 지역 마스크 — 0 이 아니면 플레이어 지역의 REGION_MASK 와 겹쳐야 연구할 수 있다(0x4e3a44). 읽기만
const uint32_t DESIGN_MASK = 0x18;          // qword. 〃 (0x7abcb1). 읽기만
const uint32_t DESIGN_SPECIAL_BIT = DESIGN_HOLD_A_BIT;   // DESIGN_HOLD_A 의 이 비트가 켜진 설계는 연구 목록에 나오지 않는다(0x7abc4e)
const uint32_t DESIGN_UPGRADE_TO = 0x24;    // word. 이 설계가 개량되어 되는 설계의 번호(DEFAULT.UNIT 의 UGTo). 그 설계는 앞 설계 하나를 보유해야 연구 목록에 나온다(0x7ac142). 읽기만
const uint32_t DESIGN_UPGRADE_TO_2 = 0x26;  // word. 개량되어 되는 둘째 설계(0x7ac3fe). 둘 가운데 하나를 보유(또는 대기열)하면 이 설계는 연구 목록에 나오지 않는다. 읽기만
const uint32_t DESIGN_RETIRED = 0x0B;       // byte. 단종 연도 − 1900(0 이면 없다) — 지금 해(REGION_YEAR)가 이보다 크면 연구 목록에 나오지 않는다(0x7ac380). 읽기만
const uint32_t DESIGN_KEPT = 0x161;         // byte. DESIGN_KEPT_BIT 이 켜진 설계는 단종돼도 나온다(0x7ac3a2). 읽기만
const uint32_t DESIGN_KEPT_BIT = 0x20;
const uint32_t REGION_YEAR = 0x14CD0;       // float. 지역 객체의 지금 해 − 1900 으로 보인다(단종과 견준다). 읽기만
const uint32_t REGION_MASK = 0x40;          // qword. 지역 객체의 지역 마스크. 읽기만
const uint32_t OWNERS_BYTES = 128;          // 보유 비트 묶음 하나 = 지역 인덱스로 찾는 1024비트
const uint32_t LIST_STEP = 24;              // 지역 하나의 연구 목록 칸
const uint32_t NODE_NEXT = 0x10;            // qword. 다음 노드
const uint32_t NODE_ID = 0x18;              // dword. 기술이나 설계의 번호
const uint32_t NODE_KIND = 0x1C;            // byte. 1 기술, 2 부대 설계
const uint32_t NODE_FLAGS = 0x20;           // dword × 2. 쪽마다의 깃발
const uint32_t NODE_GONE = 0x80000000;      // 그 쪽에서 뺐다
const uint32_t NODE_ENDED = 0x08000000;     // 내장 치트가 켜는 "끝냄"

// 새 찾기(연구): 서명의 규칙은 locate_state 와 같되 찾을 것마다 **셋이 모두** 정확히 한 번 맞고 같은 값을 내야 한다(RESEARCH_NEED).
// 일곱을 모두 찾아야 찾은 것이다(반쪽 묶음은 없다). state 는 이미 찾은 상태 묶음이다.
// 읽어 낸 값이 말이 되는지도 본다: 전역 넷은 쓸 수 있는 자료 구역 안의 제 크기의 배수 자리이고 서로 · 상태 전역과 겹치지 않는다 /
// 세계 객체 + (세계 객체의 서명이 함께 읽어 낸 거리) == 상태 묶음의 지역 표 / 연구 목록 전체가 쓸 수 있는 자료 구역 안이고 지역 표와
// 겹치지 않는다 / 다시 셈 함수는 함수 표에서 함수의 시작이다.
// 찾으면 true 와 out. 못 찾으면 false 와 why(UTF-8) — out 은 그대로다.
// rows: RESEARCH_WANTED * STATE_SIGS 칸(서명마다의 결과. 세계 객체의 둘째 값은 지역 표까지의 거리)이거나 nullptr.
bool locate_research(const uint8_t *image, size_t size, const GameAddresses &state, ResearchLayout *out, SigRow *rows, char *why,
                     size_t why_size);
// 꼴의 상수를 "이름\t값(16진수)" 줄로(테스트가 서명에 박힌 바이트와 댄다).
const char *locate_research_shape();
// 연구 묶음과 값 묶음을 함께 본다(둘 다 찾은 뒤에): 연구의 전역 넷이 세계 자료 포인터와 겹치지 않아야 한다.
// 겹치면 false 와 why(UTF-8) — 연구 묶음을 못 찾은 것으로 친다(값 묶음은 그대로 쓴다).
bool locate_research_fits(const ResearchLayout &research, const ValueLayout &values, char *why, size_t why_size);

// 옛 찾기(전환 기간에만): 찾으면 nullptr 과 out 의 handler · context · options(다른 필드는 건드리지 않는다).
// 못 찾으면 까닭(UTF-8, 정적 문자열)이고 out 은 그대로다.
const char *locate_legacy(const uint8_t *image, size_t size, GameAddresses *out);

// rva 가 든 함수의 시작(함수 표에서. chained unwind 를 뿌리까지). 없으면 0. 테스트와 srkit 이 쓴다.
uint32_t locate_function_root(const uint8_t *image, size_t size, uint32_t rva);
