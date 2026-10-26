// 값 쓰기 요청의 대기열과 최소 유지. 창의 단추(그리는 스레드)가 넣고, 게임 창의 타이머(창 스레드)가 비운다.
// 내장 치트를 거치지 않는다 — 지금 값을 읽고(game.h) 쓸 값을 셈해(values.h) 그 칸에 쓴다.
// 요청은 다섯 가지다: 국고, 물자 한 칸, 쓰는 물자 모두(3단계 1), 기술 수준 +1, 세계 시장 여론 최고, 한 나라와의 관계(3단계 2).
// 연구 요청(3단계 3)은 따로 줄을 선다 — 기술 · 부대 설계를 완료 · 미완료로 바꾸고, 게임의 "효과를 다시 셈"을 부른다.
#pragma once

#include <memory>
#include <string>

#include "locate.h"
#include "research.h"
#include "values.h"

const int TREASURY = -1;                       // Request.slot: 국고. 0 … STOCK_SLOTS - 1 은 그 칸의 물자 재고
const int ALL_STOCK = -2;                      // Request.slot: 이번 판에서 쓰는 물자 모두 — 쓸 때 칸마다의 요청으로 풀린다
const int TECH = -3;                           // Request.slot: 기술 수준에 1 을 더한다(change · amount 는 보지 않는다)
const int OPINION = -4;                        // Request.slot: 세계 시장 여론의 세 칸을 최고(1.0)로(〃)
const int PEOPLE = -6;                         // Request.slot: 인구 칸과 그 풀에 amount(명)를 더한다(change 는 보지 않는다)
const int APPROVAL = -7;                       // Request.slot: 국내 지지율을 100% 로(change · amount 는 보지 않는다)
const int RESERVE = -8;                        // Request.slot: 예비 인원에 amount(명)를 더한다(끝 RESERVE_MAX. change 는 보지 않는다)
const int LOYALTY = -9;                        // Request.slot: 점령지 충성 — 내 편이 아닌 내 칸의 충성 대상을 내 편으로(change · amount 는 보지 않는다)
const int RELATION = -5;                       // Request.slot: region 의 나라와의 관계를 amount 로, 전쟁 명분을 0 으로(change 는 보지 않는다)

struct Request {
    int slot;
    Change change;
    double amount;                             // 국고는 달러, 재고는 수량, 관계는 수준(1 최고, 0 중립)
    int region = 0;                            // RELATION: 대상 나라의 지역 번호
};

const float TECH_LIMIT = 999.0f;               // 기술 수준의 한도 — 이것을 넘게 되면 올리지 않는다

// 연구 요청: 무엇을(what) 완료로 · 미완료로(action). 쓸 때 그때의 표를 다시 읽어 규칙(research.h)으로 바꿀 것을 정한다.
struct ResearchRequest {
    Research action;
    ResearchWhat what;
    std::string label;                         // 알림과 로그에 적을 이름: "고른 것" · "보이는 것" · "기술 수준 120 이하" · "대기열"
};

const size_t RESEARCH_QUEUE = 4;               // 기다리는 연구 요청은 이것까지

// 게임의 함수를 부르는 요청(3단계 4): 고른 나라를 식민지로. 연구 요청처럼 따로 줄을 서고(RESEARCH_QUEUE 까지), 틱마다 하나를
// keeper 의 잠금을 놓고 "게임의 함수 안" 깃발을 세운 채 부른다. 받지 못하면 false: 가득 찼다, 그 기능을 쓸 수 없다(game_act_off).
bool keeper_enqueue_colonize(int region);
// 지도에서 고른 나라(attacker)와 목록에서 고른 나라(target)를 싸움 붙인다. 같은 줄에 선다.
bool keeper_enqueue_fight(int attacker, int target);
// 플레이하는 나라를 그 나라로 바꾼다. 같은 줄에 선다.
bool keeper_enqueue_become(int region);
// 그 나라의 지도자를 없앤다. 같은 줄에 선다.
bool keeper_enqueue_novichok(int region);
// 그 나라와 동맹을 맺는다(이미 동맹이면 아무 일도 없다). 같은 줄에 선다.
bool keeper_enqueue_treaty(int region);
// 그 나라의 땅을 모두 플레이어의 것으로 만든다. 같은 줄에 선다.
bool keeper_enqueue_annex(int region);
// 최강 부대 묶음을 받는다(game_best). domain 이 -1 이면 모든 영역. 받지 못하면 거짓.
bool keeper_enqueue_best(int domain, int each);
// 게임에서 고른 내 부대의 보급을 가득(fill) 또는 0 으로 쓴다(game_supply). 받지 못하면 거짓.
bool keeper_enqueue_supply(bool fill, bool all = false);   // all: 고른 부대가 아니라 내 부대 전부에
// 부대의 상태(which: STAT_*)를 percent(0 … 100)로 쓴다(game_unit_stat) — 고른 내 부대에, all 이면 내 부대 전부에. 받지 못하면 거짓.
bool keeper_enqueue_unit_stat(int which, int percent, bool all);
// 게임의 지도에서 고른 칸에 그 설계들의 부대를 each 기씩 만든다(game_spawn). 받지 못하면 거짓.
// stats: 만든 부대에 쓸 경험 · 사기 · 효율의 퍼센트(STAT_* 의 순서, 0 … 100. -1 이면 그대로). 비었으면 모두 그대로.
bool keeper_enqueue_spawn(const std::vector<int> &designs, int each, const std::vector<int> &stats = {});

// 최소 유지: 켜진 항목이 바닥보다 작아지면 바닥으로 올린다. KEEP_EVERY_MS 마다 보고, 설정 창이 닫혀 있어도 돈다.
// 플레이하는 나라가 바뀌면 그 뒤로는 새 나라의 값을 본다. 물자는 이번 판에서 쓰는 것만 올린다.
struct Keep {
    bool treasury = false;
    double treasury_floor = 0;                 // 달러
    bool stock[STOCK_SLOTS] = {};
    double stock_floor[STOCK_SLOTS] = {};      // 수량
};

const unsigned long long KEEP_EVERY_MS = 500;

// 받지 못하면 false: 가득 찼다(32개), 그 요청이 쓰는 값을 쓸 수 없다(국고 · 재고는 game_values_off, 나머지는 그 묶음의 game_more_off)
bool keeper_enqueue(const Request &request);
// 받지 못하면 false: 가득 찼다(RESEARCH_QUEUE), 연구를 쓸 수 없다(game_research_off)
bool keeper_enqueue_research(const ResearchRequest &request);
// 대기열을 비우고, 때가 됐으면 유지를 보고, 연구 요청을 하나 처리한다. 게임 창을 가진 스레드에서 부른다(now_ms: 흐르는 시계, 밀리초).
// 게임 밖 · 멀티플레이 · 오류 가드 뒤에는 남은 요청을 버리고 유지는 쉰다.
// 게임의 함수(연구의 "효과를 다시 셈" 등) 안에서 다시 온 틱이면 아무것도 하지 않는다.
// 연구 요청은 keeper 의 잠금을 놓고 처리한다 — 게임의 함수가 안에서 ToyBox 로 되돌아와도(타이머 · 단추) 서로 기다리지 않는다.
// 그 함수에서 예외가 나면 오류 가드를 건다(runner_win.h): 그 뒤로 ToyBox 는 아무것도 실행하지도 쓰지도 않는다.
void keeper_tick(unsigned long long now_ms);
// 게임 창(HWND)을 알려 준다 — "즉시 승리"가 그 창에 메시지를 보낸다. 창을 가진 스레드에서 틱 앞에 부른다.
void keeper_set_window(void *window);
// GUI 숨기기/보이기(game_gui_toggle). 받지 못하면 거짓.
bool keeper_enqueue_gui();
// 즉시 승리(game_victory). 받지 못하면 거짓.
bool keeper_enqueue_victory();
// 마지막으로 쓴 것: "국고 14.43 B -> 24.43 B" · "석유 2.5 M -> 3.5 M" · "모든 물자 11개" · "기술 수준 130 -> 131" ·
// "세계 시장 여론 최고" · "관계 최고 — 폴란드 (1106)" · "기술 12개(선행 5개 포함) · 부대 설계 1개를 완료로 — 대기열" ·
// "대기열에서 2개를 뺌 — 대기열". 없으면 빈 글
std::string keeper_last();
std::string keeper_notice();                   // 쓰지 못한 까닭(창에 보인다). 없으면 빈 글

// 연구 목록의 스냅숏. 창의 연구 탭이 보이는 동안 창 스레드의 틱이 뜬다(쓰기와 같은 스레드): RESEARCH_EVERY_MS 마다,
// 연구를 쓴 직후, 고른 나라가 바뀌었을 때. 탭이 보이지 않으면(keeper_watch_research 가 불리지 않으면) 뜨지 않는다.
struct ResearchShot {
    unsigned long long serial = 0;             // 뜰 때마다 커진다 — 창은 이것이 바뀌었을 때만 줄을 다시 만든다
    bool ok = false;
    std::string why;                           // 읽지 못한 까닭(ok 가 거짓일 때)
    int picked = 0;                            // 그때의 고른 나라(행의 picked 가 이 나라의 보유다)
    int player = 0;                            // 그때의 플레이어(지역 번호)
    ResearchTables tables;
};

const unsigned long long RESEARCH_EVERY_MS = 500;

void keeper_watch_research(int picked);        // 창: 연구 탭이 보인다(프레임마다 부른다). picked 는 고른 나라(없으면 0)
std::shared_ptr<const ResearchShot> keeper_research_shot();   // 마지막으로 뜬 것. 아직 없으면 nullptr

void keeper_set_keep(const Keep &keep);        // 창이 설정을 읽었거나 고쳤다. 다음 틱에 바로 본다
Keep keeper_keep();
int keep_count(const Keep &keep);              // 켜진 항목의 수
// 상태 줄의 뒤에 붙는 글. used: 이번 판에서 쓰는 물자(STOCK_SLOTS 칸).
// " · 유지 중: 국고, 석유, 전력" — 넷을 넘으면 " · 유지 중: 국고 외 5개". 지금 유지되는 것이 없으면 빈 글.
std::string keep_active_text(const Keep &keep, const bool *used);
// 쉬는 동안의 글: " · 유지 3개 켜짐(<까닭>)". 켜진 것이 없으면 빈 글.
std::string keep_resting_text(const Keep &keep, const char *why);

// 배율(묶음 7 — docs/superpowers/specs/2026-10-11-toybox-stage7-design.md). 모두 퍼센트(100 이 "그대로")이고 on 이 꺼져 있으면 아무것도 쓰지 않는다.
// 플레이어의 나라에만 듣는다. 설정 창이 닫혀 있어도, 새 판에서도 돈다(저장되는 설정이다).
//   out · use · raw · unitbuild · facbuild
//                              게임의 곱 보정 표의 플레이어 칸에 곱해 쓴다. KEEP_EVERY_MS 마다 보고, 게임이 다시 셈해 지웠으면 다시 쓴다.
//                              100 으로 돌리거나 끄면 그 칸이 아직 ToyBox 가 쓴 값일 때 게임의 값으로 되돌린다.
//   tax · defense · social     날이 바뀔 때 국고에 (배율 − 1) × 하루치 세수를 더하고 (배율 − 1) × 하루치 국방 지출 · 사회 지출을 뺀다.
//                              (사회 지출은 처음에 곱 보정 칸 108 … 115 로 했는데 게임에서 듣지 않았다 — 설계서의 "게임에서 본 것")
//   research                   날이 바뀔 때 진행 중인 연구 노드의 남은 기간에서 (배율 − 1) × 하루치를 더 뺀다.
// 날은 게임의 날의 칸이 1 … MULT_DAYS_MAX 올랐을 때만 센다(그 밖의 변화 — 불러오기 · 새 판 — 와 플레이어가 바뀐 때에는 다시 맞추기만 한다).
// 끝은 묶음마다 다르다(docs/superpowers/specs/2026-10-19-toybox-mult-unit-caps-design.md — 게임에서 본 범위까지):
const int MULT_FAST_MAX = 10000;               // 세금 · 국방비 · 사회 지출 · 연구 속도
const int MULT_BUILD_MAX = 2000;               // 유닛 생산 · 시설 건설 속도(게임에서 ×20 까지 봤다)
const int MULT_GOODS_MAX = 1000;               // 물자의 생산 · 소비(×20 에서 게임의 생산이 무너졌다)
const int MULT_CELL_MIN = 1;                   // 곱 보정 칸의 배율(생산 · 소비)의 처음 — 0 을 쓰지 않는다
const int MULT_RESEARCH_MIN = 10;              // 연구 속도의 처음
const uint32_t MULT_DAYS_MAX = 3;
struct Mult {
    bool on = false;
    int tax = 100, defense = 100, social = 100;   // 0 … MULT_FAST_MAX
    int research = 100;                        // MULT_RESEARCH_MIN … MULT_FAST_MAX
    int unitbuild = 100, facbuild = 100;       // 유닛 생산 · 시설 건설 속도 — 곱 보정 칸 15 · 16. MULT_CELL_MIN … MULT_BUILD_MAX
    // 아래 셋은 MULT_CELL_MIN … MULT_GOODS_MAX
    int out[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};   // 물자마다의 생산
    int use[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};   // 물자마다의 인구 소비
    int raw[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};   // 물자마다의 산업 원자재 소비
};
bool operator==(const Mult &a, const Mult &b);
void keeper_set_mult(const Mult &mult);         // 창이 설정을 읽었거나 고쳤다. 다음 틱에 바로 본다
Mult keeper_mult();
int mult_count(const Mult &mult);              // 100 이 아닌 배율의 수(on 과 상관없이)
// 다음 자정에 완료(docs/superpowers/specs/2026-10-14-toybox-mult-build-design.md). 누르면 그날의 셈 하루 동안 칸 16(시설 건설) 에 게임의 값 × RUSH_TIMES 를
// 쓴다(배율 대신). 날의 칸이 오른 뒤 게임이 그 칸을 다시 셈한 것을 보면 푼다. 저장하지 않고 배율 적용과 상관없이 듣는다. 판 · 플레이어가 바뀌면 버린다.
const float RUSH_TIMES = 1000.0f, RUSH_VALUE_MAX = 100000.0f;
const uint32_t RUSH_DAYS_MAX = 2;               // 누른 날로부터 이만큼 지나면 게임이 다시 셈하지 않았어도 푼다
std::string keeper_rush();                      // 시작한다. 못 하면 까닭(게임 밖 · 값 쓰기 꺼짐 · 곱 보정 표나 날의 칸을 못 찾음), 되면 빈 글
bool keeper_rush_pending();
// 지금 플레이어의 줄에서 ToyBox 가 쥔 칸을 모두 게임의 값으로 되돌리고 대기를 버린다 — "이 나라로 플레이"가 게임을 부르기 직전에
// (앞 나라의 줄에 배율 · 대기 값이 남아 그 나라에 듣지 않게). g_lock 을 쥐지 않은 채로 부른다.
void keeper_mult_put_back();
// 이번 실행에서 배율이 한 일: 날마다의 일을 한 날수, 곱 보정 칸을 쓴 횟수(게임이 지워 다시 쓴 것 포함), 지금 ToyBox 의 값이 든 칸의 수.
struct MultStats {
    unsigned long long days = 0, writes = 0;
    int held = 0;
};
MultStats keeper_mult_stats();

// 사회 — 품질 등급의 배율과 최소 유지(docs/superpowers/specs/2026-10-21-toybox-social-grades-design.md). 저장되는 설정이다. 플레이어의 나라에만 듣는다.
//   up · down     on 일 때: 날의 칸이 1 … MULT_DAYS_MAX 오르면 항목마다 그날 게임이 움직인 양 d(자정 전에 읽어 둔 값과의 차)를 보고
//                 d > 0 이면 (up − 1) × d, d < 0 이면 (down − 1) × d 를 더 움직인다. 결과는 0 … 2.0(게임의 끝)으로 자른다.
//                 자정 전의 값이 2.0 을 넘었으면(최소 유지가 올렸다) 2.0 으로 보고 셈한다 — 게임이 자른 것을 내린 날로 치지 않는다.
//   keep          on 과 상관없이 KEEP_EVERY_MS 마다: 켜진 항목의 등급이 keep_value ÷ 100 보다 작으면 그 값으로(배율 뒤에).
//                 날이 오른 틱에서 등급이 아직 그대로면(게임이 자정의 셈 도중에 메시지를 돌렸다) 바뀐 것을 볼 때까지 SOCIAL_SETTLE_MS 기다린다 —
//                 그동안 아무것도 쓰지 않는다. 끝내 바뀌지 않으면 그날은 넘긴다.
const unsigned long long SOCIAL_SETTLE_MS = 2000;
const int SOCIAL_RATE_MAX = 10000;             // 오름 · 내림 배율의 끝(퍼센트, 처음은 0)
const int SOCIAL_KEEP_MAX = 999;               // 최소 유지 값의 끝(퍼센트 — 게임 화면의 "품질 등급"과 같은 단위)
struct Social {
    bool on = false;
    int up[SOCIAL_COUNT] = {100, 100, 100, 100, 100, 100, 100, 100};
    int down[SOCIAL_COUNT] = {100, 100, 100, 100, 100, 100, 100, 100};
    bool keep[SOCIAL_COUNT] = {};
    int keep_value[SOCIAL_COUNT] = {};
};
bool operator==(const Social &a, const Social &b);
void keeper_set_social(const Social &social);     // 창이 설정을 읽었거나 고쳤다. 다음 틱에 바로 본다
Social keeper_social();
int social_count(const Social &social);           // 100 이 아닌 배율(on 과 상관없이)과 켜진 최소 유지의 수

// 예비 인원의 최소 유지(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md). KEEP_EVERY_MS 마다, 켜져 있으면 예비가 value 보다 적을 때 value 로 쓴다. 저장되는 설정이다.
const long long RESERVE_KEEP_MAX = 100000000;    // 유지 값의 끝(명)
void keeper_set_reserve_keep(bool on, long long value);

void keeper_reset_for_test();
