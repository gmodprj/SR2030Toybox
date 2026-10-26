#include "keeper.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "game.h"
#include "techs.h"
#include "log.h"
#include "products.h"
#include "regions.h"
#include "runner_win.h"

namespace {

const size_t MAX_QUEUE = 32;
const char *const LEFT_GAME = "게임이 진행 중이 아니어서 쓰지 않았습니다.";

std::mutex g_lock;          // 단추(그리는 스레드)와 틱(창 스레드)이 함께 만진다
std::deque<Request> g_queue;
std::deque<ResearchRequest> g_research;     // 연구 요청은 따로 줄을 선다 — 틱마다 값 요청을 비운 뒤 하나
struct Act {
    int attacker;                           // 전쟁: 지도에서 고른 나라. 0 이면 식민지화, BECOME 이면 "이 나라로 플레이"다
    int region;                             // 식민지로 삼을 나라 · 전쟁의 둘째 나라. 부대 생성: 설계마다의 수량
    std::vector<int> designs;               // 부대 생성: 만들 부대 설계의 번호들
    std::vector<int> stats;                 // 부대 생성: 만든 부대의 경험 · 사기 · 효율의 퍼센트(-1 이면 그대로). 비었으면 모두 그대로
};
const int BECOME = -1, NOVICHOK = -2, TREATY = -3, ANNEX = -4, SPAWN = -5, SUPPLY = -6, BEST = -7, VICTORY = -8, GUI = -9, STAT = -10;   // STAT: region 이 무엇을(+ 16 이면 전부에), designs[0] 이 퍼센트
void *g_window;                             // 게임 창(HWND) — "즉시 승리"의 메시지를 받을 곳   // SUPPLY: region 이 1 이면 채우기, 0 이면 비우기
std::deque<Act> g_acts;                     // 게임의 함수를 부르는 요청(식민지로 삼을 나라의 번호) — 연구 요청보다 먼저, 틱마다 하나

// 이번 틱에 잠금을 놓고 할 일 하나: 게임의 함수를 부르는 요청이거나 연구 요청.
struct Job {
    bool act = false;
    Act what = {};
    ResearchRequest research;
};
std::string g_last, g_notice;
Keep g_keep;                        // 최소 유지의 설정(창이 넘긴 것)
bool g_keep_due;                    // 설정이 바뀌었다 — 0.5초를 기다리지 않고 다음 틱에 본다
unsigned long long g_keep_at;       // 마지막으로 유지를 본 때
bool g_kept[1 + STOCK_SLOTS];
std::shared_ptr<const ResearchShot> g_shot; // 연구 목록의 스냅숏(창에 넘긴다)
bool g_watch_seen, g_watch_dirty;           // 지난 스냅숏 뒤에 연구 탭이 그려졌다 / 연구를 썼다 — 기다리지 않고 다시 뜬다
int g_watch_picked;                         // 창이 고른 나라
unsigned long long g_shot_at, g_shot_serial;
bool g_shot_timed;                          // 스냅숏에 걸린 시간을 이번 실행의 로그에 적었다
       // 그 항목을 올린 것을 이번 실행의 로그에 적었다([0] 국고, [1 + 칸] 물자)

Mult g_mult;                        // 배율의 설정(창이 넘긴 것)
bool g_mult_due;                    // 설정이 바뀌었다 — 다음 틱에 바로 본다
unsigned long long g_mult_at;       // 마지막으로 배율을 본 때
struct CellMemo {
    bool have = false;              // 이 칸에 ToyBox 가 쓴 값이 들어 있다
    float base = 0, wrote = 0;      // 그때의 게임의 값 · ToyBox 가 쓴 값
};
CellMemo g_cells[MUL_CELLS];
int g_cells_index;                  // 그 기억이 어느 지역(인덱스)의 줄의 것인가. 0 이면 없다
bool g_day_known;                   // 날의 칸을 본 적이 있다(이번 판 · 이 플레이어에서)
uint32_t g_day;
int g_day_index;
MultStats g_mult_stats;
bool g_rush;                        // 다음 자정에 완료 — 대기 중
uint32_t g_rush_day;                // 누른 때의 날의 칸
int g_rush_index;                   // 누른 때의 플레이어 지역 인덱스
bool g_mult_told;                   // 이 설정으로 날마다의 일을 한 것을 로그에 적었다(설정이 바뀌면 다시 적는다)
Social g_social;                    // 사회 — 품질 등급의 배율과 최소 유지
bool g_social_due;                  // 설정이 바뀌었다 — 다음 틱에 바로 본다
unsigned long long g_social_at;
bool g_social_have;                 // 아래의 셋이 지난 틱에 읽은 값이다
int g_social_index;
uint32_t g_social_day;
float g_social_seen[SOCIAL_COUNT];  // 지난 틱의 등급(ToyBox 가 쓴 뒤의 값) — 자정의 변화는 이것과의 차다
bool g_social_told;                 // 이 설정으로 날마다의 일을 한 것을 로그에 적었다
bool g_reserve_keep;                // 예비 인원의 최소 유지
bool g_floor_on[FLOOR_COUNT];       // 지지율 · 여론의 최소 유지(Floor 순서)
int g_floor_pct[FLOOR_COUNT];
bool g_floor_told[FLOOR_COUNT];
unsigned long long g_floor_at;
bool g_loyalty_keep;                // 점령지 충성의 유지
bool g_loyalty_due;                 // 켠 직후 — 기다리지 않고 한 번
unsigned long long g_loyalty_at;
long long g_reserve_floor;
unsigned long long g_reserve_at;
bool g_reserve_told;
bool g_social_waiting;              // 날이 올랐다 — 게임이 그날의 등급을 셈한 것을 볼 때까지 기다린다
unsigned long long g_social_wait_from;
bool g_social_kept[SOCIAL_COUNT];   // 항목마다 최소 유지가 올린 것을 로그에 적었다(이번 실행의 첫 번째만)

std::string target_name(int slot)
{
    return slot == TREASURY ? std::string("국고") : product_label(slot);
}

// 국고는 재무 패널의 표기로, 물자는 위쪽 자원 표시줄의 표기로 적는다.
std::string shown(int slot, double value)
{
    return slot == TREASURY ? short_number(value) : short_amount(value);
}

// 그 요청이 쓰는 값을 쓸 수 없는 까닭. 쓸 수 있으면 빈 글. 묶음마다 따로다.
std::string request_off(const Request &r)
{
    return r.slot == TECH ? game_more_off(MORE_TECH) : r.slot == OPINION ? game_more_off(MORE_OPINION)
        : r.slot == RELATION ? game_more_off(MORE_RELATIONS) : r.slot == PEOPLE ? game_more_off(MORE_PEOPLE)
        : r.slot == APPROVAL ? game_more_off(MORE_APPROVAL) : r.slot == RESERVE ? game_mult_off(MULT_PART_RESERVE)
        : r.slot == LOYALTY ? game_loyalty_off() : game_values_off();
}

// 수를 있는 그대로 적는다: 130, 130.5
std::string plain(double value)
{
    char text[32];
    snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

// 더 쓰는 값의 요청 하나(기술 수준 · 여론 · 관계). 국고 · 재고는 읽지 않는다 — 그 묶음을 못 찾은 게임에서도 된다.
// 값 쓰기가 꺼졌으면(실패) false — 남은 요청을 버린다. g_lock 을 쥔 채로 부른다.
bool apply_more(const Request &r)
{
    Wrote wrote = Wrote::Off;
    std::string what;
    if (r.slot == TECH) {
        const GameMore now = game_more();
        if (!now.ok || !std::isfinite(now.tech) || now.tech < 0.0f) {
            g_notice = "기술 수준을 읽을 수 없어 쓰지 않았습니다.";
            return true;
        }
        if (now.tech + 1.0f > TECH_LIMIT) {
            g_notice = "기술 수준이 한도(999)라 쓰지 않았습니다.";
            return true;
        }
        what = "기술 수준 " + plain(now.tech) + " -> " + plain(now.tech + 1.0f);
        wrote = game_write_tech(now.tech + 1.0f);
    } else if (r.slot == OPINION) {
        what = "세계 시장 여론 최고";
        wrote = game_write_opinion();
    } else if (r.slot == PEOPLE) {
        const GameMore now = game_more();
        if (!now.ok || !std::isfinite(now.people[0]) || now.people[0] < 0.0f) {
            g_notice = "인구를 읽을 수 없어 쓰지 않았습니다.";
            return true;
        }
        what = "인구 " + short_amount(now.people[0]) + " -> " + short_amount(static_cast<double>(now.people[0]) + r.amount);
        wrote = game_write_people(static_cast<float>(r.amount));
        if (wrote == Wrote::BadValue) {
            g_notice = "인구의 칸이 수가 아니어서 쓰지 않았습니다.";
            return true;
        }
    } else if (r.slot == RESERVE) {
        float now = 0;
        if (!game_reserve(&now) || !std::isfinite(now) || now < 0.0f || now > RESERVE_MAX) {
            g_notice = "예비 인원을 읽을 수 없어 쓰지 않았습니다.";
            return true;
        }
        const float to = static_cast<float>(std::min(static_cast<double>(RESERVE_MAX), static_cast<double>(now) + r.amount));
        what = "예비 " + short_amount(now) + " -> " + short_amount(to);
        wrote = game_write_reserve(to);
        if (wrote == Wrote::BadValue) {
            g_notice = "예비 인원의 칸이 수가 아니어서 쓰지 않았습니다.";
            return true;
        }
    } else if (r.slot == LOYALTY) {
        int hexes = 0, target = 0, number = 0;
        wrote = game_loyalty(&hexes, &target, &number);
        if (wrote == Wrote::Done && hexes == 0) {
            g_notice = "점령지 충성: 바꿀 칸이 없습니다";
            return true;
        }
        if (wrote == Wrote::BadValue || wrote == Wrote::Unreadable) {
            g_notice = wrote == Wrote::BadValue ? "점령지 충성: 내 편인 칸이 없어 쓰지 않았습니다" : "점령지 충성: 지도를 읽을 수 없어 쓰지 않았습니다";
            return true;
        }
        what = "점령지 충성: " + std::to_string(hexes) + "칸 → " + region_label(number);
    } else if (r.slot == APPROVAL) {
        what = "지지율 100%";
        wrote = game_write_approval();
    } else {
        what = std::string(r.amount > 0.5 ? "관계 최고" : "관계 중립") + " — " + region_label(r.region) + " (" + std::to_string(r.region) + ")";
        wrote = game_write_relation(r.region, static_cast<float>(r.amount));
    }
    if (wrote == Wrote::Done) {
        g_last = what;
        log_line("값 쓰기: %s", what.c_str());
        return true;
    }
    if (wrote == Wrote::Failed || wrote == Wrote::Off)
        return false;                          // 까닭은 그 단추의 자리에 보인다(game_more_off)
    g_notice = wrote == Wrote::NoTarget ? "그 나라는 이번 판에 없어 쓰지 않았습니다." : LEFT_GAME;
    return true;
}

// 한 칸(국고나 물자 하나)에 대한 셈: 지금 값(*from)과 쓸 값(*to).
Verdict decide(int slot, Change change, double amount, const GameValues &now, double *from, double *to)
{
    if (slot == TREASURY) {
        *from = now.treasury;
        return treasury_value(now.treasury, change, amount, to);
    }
    float next = 0;
    *from = now.stock[slot];
    const Verdict verdict = stock_value(now.stock[slot], change, amount, &next);
    *to = next;
    return verdict;
}

Wrote write_slot(int slot, double value)
{
    return slot == TREASURY ? game_write_treasury(value) : game_write_stock(slot, static_cast<float>(value));
}

// 요청 하나(국고나 물자 한 칸)를 지금 값(now)에 대어 쓴다. 값 쓰기가 꺼졌으면(실패) false — 남은 요청을 버린다.
// 썼으면 *wrote_it 이 true. g_lock 을 쥔 채로 부른다.
bool apply(const Request &r, const GameValues &now, bool *wrote_it)
{
    *wrote_it = false;
    if (r.slot != TREASURY && (r.slot < 0 || r.slot >= STOCK_SLOTS || !now.used[r.slot])) {
        g_notice = "이번 판에서 쓰지 않는 물자입니다.";
        return true;
    }
    double from = 0, to = 0;
    const Verdict verdict = decide(r.slot, r.change, r.amount, now, &from, &to);
    if (verdict == Verdict::Refuse)
        g_notice = "지금 값이 수가 아니어서 쓰지 않았습니다.";
    if (verdict != Verdict::Write)
        return true;
    const Wrote wrote = write_slot(r.slot, to);
    if (wrote == Wrote::Done) {
        *wrote_it = true;
        g_last = target_name(r.slot) + " " + shown(r.slot, from) + " -> " + shown(r.slot, to);
        log_line("값 쓰기: %s", g_last.c_str());
        return true;
    }
    if (wrote == Wrote::Failed || wrote == Wrote::Off)
        return false;                          // 까닭은 탭이 보인다(game_values_off)
    g_notice = LEFT_GAME;                      // 읽은 뒤 쓰기 전에 게임이 바뀌었다
    return true;
}

// "모든 물자": 이번 판에서 쓰는 물자마다 같은 일을 한다. 한 칸에 쓰는 것은 다른 칸의 값을 바꾸지 않으므로 now 를 다시 읽지 않는다.
bool apply_all(const Request &r, const GameValues &now)
{
    int count = 0;
    for (int slot = 0; slot < STOCK_SLOTS; slot++) {
        if (!now.used[slot])
            continue;
        bool wrote_it = false;
        if (!apply({slot, r.change, r.amount}, now, &wrote_it))
            return false;
        count += wrote_it ? 1 : 0;
    }
    if (count > 0) {                            // 무엇을 했는지도 적는다(바닥 줄이 "모든 물자 11개"뿐이었다 — 2026-10-19)
        const std::string what = r.change == Change::Set ? "-> " + short_amount(r.amount)
            : (r.amount < 0 ? "-" : "+") + short_amount(std::fabs(r.amount));
        g_last = "모든 물자 " + std::to_string(count) + "개 " + what;
    }
    return true;
}

// 대기열을 비운다. 값 쓰기가 꺼졌거나 값을 읽지 못하면 false(남은 요청은 버렸다). g_lock 을 쥔 채로 부른다.
bool drain()
{
    g_notice.clear();
    while (!g_queue.empty()) {
        const Request r = g_queue.front();
        g_queue.pop_front();
        if (r.slot <= TECH) {                  // 더 쓰는 값: 국고 · 재고와 따로다
            if (!apply_more(r)) {
                g_queue.clear();
                return false;
            }
            continue;
        }
        const GameValues now = game_values();  // 요청마다 다시 읽는다 — 앞의 요청이 값을 바꿨다
        if (!now.ok) {
            g_queue.clear();
            g_notice = "게임의 값을 읽을 수 없어 쓰지 않았습니다.";
            return false;
        }
        bool wrote_it = false;
        if (!(r.slot == ALL_STOCK ? apply_all(r, now) : apply(r, now, &wrote_it))) {
            g_queue.clear();
            return false;
        }
    }
    return true;
}

// 유지의 한 항목: 지금 값이 바닥보다 작으면 바닥으로 올린다. 값 쓰기가 꺼졌으면(실패) false.
// 창에 알리지 않는다(0.5초마다 온다). 로그에는 항목마다 이번 실행의 첫 번째만 적는다.
bool hold(int slot, double floor, const GameValues &now)
{
    double from = 0, to = 0;
    if (decide(slot, Change::Floor, floor, now, &from, &to) != Verdict::Write)
        return true;                           // 바닥 위다. 지금 값이 수가 아니면 건드리지 않는다
    const Wrote wrote = write_slot(slot, to);
    if (wrote == Wrote::Done && !g_kept[1 + slot]) {
        g_kept[1 + slot] = true;
        log_line("유지: %s %s -> %s", target_name(slot).c_str(), shown(slot, from).c_str(), shown(slot, to).c_str());
    }
    return wrote != Wrote::Failed && wrote != Wrote::Off;
}

// 유지 검사: 켜진 항목마다 바닥을 댄다. 물자는 이번 판에서 쓰는 것만. g_lock 을 쥔 채로 부른다.
void keep_check()
{
    const GameValues now = game_values();
    if (!now.ok)
        return;
    if (g_keep.treasury && !hold(TREASURY, g_keep.treasury_floor, now))
        return;
    for (int slot = 0; slot < STOCK_SLOTS; slot++)
        if (g_keep.stock[slot] && now.used[slot] && !hold(slot, g_keep.stock_floor[slot], now))
            return;
}

// 틱의 앞쪽: 값 요청을 비우고, 때가 됐으면 유지를 본다. 이번 틱에 처리할 연구 요청이 있으면 *next 에 꺼내고 true —
// 그때는 "게임의 함수 안" 깃발을 세워 둔다(부른 쪽이 runner_leave_call 로 내린다). g_lock 을 쥔 채로 부른다.
bool tick_locked(unsigned long long now_ms, Job *next)
{
    if (runner_calling())
        return false;                          // 게임의 함수 안에서 다시 온 틱이면 기다린다 — 그 함수가 읽고 있는 값을 바꾸지 않는다
    const bool due = keep_count(g_keep) > 0 && (g_keep_due || now_ms - g_keep_at >= KEEP_EVERY_MS);
    if (g_queue.empty() && g_research.empty() && g_acts.empty() && !due)
        return false;
    if (due) {                                 // 쓰든 쉬든, 다음에 보는 것은 지금부터 0.5초 뒤다
        g_keep_due = false;
        g_keep_at = now_ms;
    }
    const GameState game = game_state();
    if (runner_faulted() || !game_writes()) {
        g_queue.clear();                       // 까닭은 창이 보인다(빨간 경고, 탭의 한 줄). 유지도 쉰다
        g_research.clear();
        g_acts.clear();
        return false;
    }
    if (!game.known || !game.in_game || game.multiplayer) {
        if (!g_queue.empty() || !g_research.empty() || !g_acts.empty()) {
            g_queue.clear();                   // 누른 뒤 게임에서 나갔다 — 돌아온 뒤에 쓰이지 않게 버린다
            g_research.clear();
            g_acts.clear();
            g_notice = LEFT_GAME;
        }
        return false;                          // 유지는 게임 밖 · 멀티플레이에서 쉰다
    }
    const bool values = !g_queue.empty();
    if (values && !drain()) {
        g_research.clear();                    // 값 쓰기가 꺼졌다 — 연구도 쓰지 않는다
        g_acts.clear();
        return false;
    }
    if (due && game_values_off().empty())      // 유지는 국고와 물자다 — 그 자리를 못 찾은 게임에서는 쉰다
        keep_check();                          // 단추의 요청을 쓴 뒤에 본다 — 방금 내린 값도 바닥 아래면 올린다
    if ((g_research.empty() && g_acts.empty()) || !runner_enter_call())
        return false;
    if (!values)
        g_notice.clear();                      // 이번 틱에 쓴 값 요청의 알림은 남긴다 — 연구의 알림이 생기면 그것이 덮는다
    if (!g_acts.empty()) {
        next->act = true;
        next->what = g_acts.front();
        g_acts.pop_front();
        return true;
    }
    next->research = g_research.front();
    g_research.pop_front();
    return true;
}

// 연구 요청 하나를 쓴다. g_lock 을 쥐지 않은 채로 부른다 — 끝에 게임의 "효과를 다시 셈"이 불리고, 그 함수가 안에서
// ToyBox 로 되돌아오면(타이머 · 단추) 그것들이 g_lock 을 잡는다. "게임의 함수 안" 깃발은 tick_locked 가 세워 뒀다.
void run_research(const ResearchRequest &r)
{
    ResearchDone done;
    const Wrote wrote = game_write_research(r.action, r.what, &done);
    runner_leave_call(wrote == Wrote::Crashed ? "효과를 다시 셈하는" : nullptr, done.code);

    std::lock_guard<std::mutex> lock(g_lock);
    const char *const verb = r.action == Research::Complete ? "완료" : r.action == Research::Unlock ? "연구 가능으로" : "미완료";
    if (wrote == Wrote::NotInGame) {           // 읽은 뒤 쓰기 전에 게임이 바뀌었다
        g_notice = LEFT_GAME;
        return;
    }
    if (wrote == Wrote::Unreadable) {
        g_notice = "연구의 표를 읽을 수 없습니다 (" + done.why + ")";
        log_line("%s", g_notice.c_str());
        return;
    }
    if (wrote != Wrote::Done) {                // 실패 · 꺼짐 · 예외: 까닭은 그 단추의 자리와 빨간 경고에 보인다. 남은 요청을 버린다
        g_queue.clear();
        g_research.clear();
        g_acts.clear();
        return;
    }
    const std::string skipped = done.skipped == 0 ? std::string()
        : "묶음을 만들 수 없는 게임 판입니다 — 보유한 나라가 없는 항목 " + std::to_string(done.skipped) + "개를 건너뜁니다";
    if (!skipped.empty()) {
        g_notice = skipped;
        log_line("%s", skipped.c_str());
    }
    const std::string summary = research_summary(done.plan, r.action);
    if (r.action == Research::Unlock) {        // 요약이 이미 완성된 글이다("연구 가능으로 — …") — "…를 …로"를 덧붙이지 않는다
        const std::string prefix = "연구 가능으로 — ";
        const std::string detail = summary.compare(0, prefix.size(), prefix) == 0 ? summary.substr(prefix.size()) : summary;
        if (done.plan.techs.empty() && done.plan.designs.empty() && done.plan.region_add == 0) {   // 한 칸도 쓰지 않았다(보유 · 이미 열림 · 열 수 없음뿐) — 앞 설계 · 지역만 연 것도 쓴 것이다
            if (skipped.empty())
                g_notice = "바꿀 것이 없습니다 — " + r.label + (detail.empty() ? "" : " (" + detail + ")");
            log_line("연구 가능으로 (%s): 바꿀 것이 없습니다%s", r.label.c_str(), detail.empty() ? "" : (" — " + detail).c_str());
            return;
        }
        g_last = summary + " — " + r.label;
        log_line("연구 가능으로 (%s): %s%s", r.label.c_str(), detail.c_str(),
                 done.housed > 0 ? (" · 새 묶음 " + std::to_string(done.housed) + "개").c_str() : "");
        return;
    }
    if (summary.empty()) {
        if (skipped.empty()) {
            g_notice = "바꿀 것이 없습니다 — " + r.label;
            log_line("연구 %s (%s): 바꿀 것이 없습니다", verb, r.label.c_str());
        }
        return;
    }
    ResearchPlan bits = done.plan;             // 창의 한 줄에는 바뀐 기술 · 부대 설계만 적는다
    bits.nodes.clear();
    const std::string changed = research_summary(bits, r.action);
    g_last = (changed.empty() ? "대기열에서 " + std::to_string(done.plan.nodes.size()) + "개를 뺌" : changed + "를 " + verb + "로")
        + " — " + r.label;
    log_line("연구 %s (%s): %s%s", verb, r.label.c_str(), summary.c_str(),
             done.housed > 0 ? (" · 새 묶음 " + std::to_string(done.housed) + "개").c_str() : "");
}

}  // namespace

bool keeper_enqueue(const Request &request)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_queue.size() >= MAX_QUEUE || !request_off(request).empty())
        return false;
    g_queue.push_back(request);
    return true;
}

bool keeper_enqueue_research(const ResearchRequest &request)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_research.size() >= RESEARCH_QUEUE || !game_research_off().empty())
        return false;
    g_research.push_back(request);
    return true;
}

namespace {

// 게임의 함수를 부르는 요청 하나: 고른 나라를 식민지로 삼거나, 두 나라를 싸움 붙인다. g_lock 을 쥐지 않은 채로 부른다 — "게임의 함수 안" 깃발은 tick_locked 가 세워 뒀다.
void run_act(const Act &act)
{
    unsigned long code = 0;
    const bool fight = act.attacker > 0, become = act.attacker == BECOME, novichok = act.attacker == NOVICHOK;
    const bool treaty = act.attacker == TREATY, annex = act.attacker == ANNEX, spawn = act.attacker == SPAWN;
    float stat_values[STAT_COUNT] = {-1.0f, -1.0f, -1.0f};   // 부대 생성: 만든 부대의 상태(0 … 1. 음수면 그대로)
    for (int s = 0; s < STAT_COUNT && s < static_cast<int>(act.stats.size()); s++)
        stat_values[s] = act.stats[s] < 0 ? -1.0f : (act.stats[s] > 100 ? 100 : act.stats[s]) / 100.0f;
    const float *const spawn_stats = act.stats.empty() ? nullptr : stat_values;
    int stated = 0;
    const bool supply = act.attacker == SUPPLY, best = act.attacker == BEST;   // BEST: region 이 수량, designs[0] 이 영역
    int hexes = 0, made = 0, hex_x = 0, hex_y = 0, skipped = 0;
    const bool victory = act.attacker == VICTORY;
    void *window = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        window = g_window;
    }
    const bool gui = act.attacker == GUI, stat = act.attacker == STAT;
    const bool everyone = (supply && (act.region & 2) != 0) || (stat && (act.region & 16) != 0);   // 내 부대 전부에
    bool hidden = false;
    const Wrote wrote = gui ? game_gui_toggle(&code, &hidden) : victory ? game_victory(window) : best ? game_best(act.designs[0], act.region, &code, &made)
        : stat ? game_unit_stat(act.region & 15, static_cast<float>(act.designs[0]) / 100.0f, everyone, &made, &skipped)
        : supply ? game_supply((act.region & 1) != 0, everyone, &made, &skipped) : spawn ? game_spawn(act.designs.data(), static_cast<int>(act.designs.size()), act.region, spawn_stats, &code, &made, &stated, &hex_x, &hex_y)
        : fight ? game_fight(act.attacker, act.region, &code) : become ? (keeper_mult_put_back(), game_become(act.region, &code))
        : novichok ? game_novichok(act.region, &code) : treaty ? game_treaty(act.region, &code)
        : annex ? game_annex(act.region, &code, &hexes) : game_colonize(act.region, &code);
    runner_leave_call(wrote != Wrote::Crashed ? nullptr : gui ? "GUI 를 바꾸는" : spawn || best ? "부대를 만드는" : fight ? "전쟁을 붙이는" : become ? "플레이하는 나라를 바꾸는"
                      : novichok ? "지도자를 바꾸는" : treaty ? "동맹을 맺는" : annex ? "병합하는" : "식민지화하는", code);

    std::lock_guard<std::mutex> lock(g_lock);
    const auto name = [](int number) { return region_label(number) + " (" + std::to_string(number) + ")"; };
    const std::string who = fight ? name(act.attacker) + " -> " + name(act.region) : name(act.region);
    if (annex && wrote == Wrote::Crashed)      // 반쯤 넘어간 채로 멈췄다 — 몇 칸까지였는지 남긴다
        log_line("병합 — %s: %d칸을 넘긴 뒤 예외", who.c_str(), hexes);
    if (spawn && wrote == Wrote::Crashed)      // 몇 기까지 만들었는지 남긴다
        log_line("부대 생성: %d기를 만든 뒤 예외", made);
    if (best && wrote == Wrote::Crashed)
        log_line("최강 부대 묶음: 병과 %d종을 만든 뒤 예외", made);
    if (gui && wrote == Wrote::Done) {
        g_last = hidden ? "GUI 숨김" : "GUI 보임";
        log_line("게임의 함수: %s", g_last.c_str());
    } else if (gui && (wrote == Wrote::BadValue || wrote == Wrote::Failed)) {
        g_notice = wrote == Wrote::BadValue ? "게임의 GUI 정보를 읽을 수 없어 하지 않았습니다." : "화면의 깃발을 쓰지 못했습니다.";
        log_line("%s", g_notice.c_str());
    } else if (victory && wrote == Wrote::Done) {
        g_last = "즉시 승리";
        log_line("값 쓰기: %s", g_last.c_str());
    } else if (victory && wrote == Wrote::NotUsed) {
        g_notice = "이미 끝난 판입니다.";
    } else if (victory && (wrote == Wrote::BadValue || wrote == Wrote::Failed)) {
        g_notice = wrote == Wrote::BadValue ? "게임의 승리 정보를 읽을 수 없어 하지 않았습니다." : "승리 정보를 쓰지 못했습니다.";
        log_line("%s", g_notice.c_str());
    } else if (best && wrote == Wrote::Done && made == 0) {
        g_notice = "그 영역의 부대 설계가 없습니다.";
    } else if (best && wrote == Wrote::Done) {
        g_last = "최강 부대 묶음 — " + (act.designs[0] >= 0 ? domain_label(act.designs[0]) : std::string("모든 영역")) + " · 병과 "
            + std::to_string(made) + "종 × " + std::to_string(act.region) + "기";
        log_line("게임의 함수: %s", g_last.c_str());
    } else if (best && wrote == Wrote::BadValue) {
        g_notice = "게임의 부대 설계 표를 읽을 수 없어 하지 않았습니다.";
        log_line("%s", g_notice.c_str());
    } else if ((supply || stat) && wrote == Wrote::Done && made == 0) {
        g_notice = everyone ? "내 부대가 없습니다." : skipped > 0 ? "고른 부대가 모두 남의 부대라 하지 않았습니다."
            : "고른 부대가 없습니다 — 게임에서 내 부대를 고르십시오.";
    } else if ((supply || stat) && wrote == Wrote::Done) {
        static const char *const STATS[STAT_COUNT] = {"경험", "사기", "효율"};
        const std::string what = stat ? std::string(STATS[act.region & 15]) + " " + std::to_string(act.designs[0]) + "%"
            : (act.region & 1) != 0 ? std::string("보급 채우기") : std::string("보급 비우기");
        g_last = what + (everyone ? " — 내 부대 전부 · " + std::to_string(made) + "기" : " — 부대 " + std::to_string(made) + "기")
            + (!everyone && skipped > 0 ? " (남의 부대 " + std::to_string(skipped) + "기는 건너뜀)" : std::string());
        log_line("값 쓰기: %s", g_last.c_str());
    } else if ((supply || stat) && wrote == Wrote::BadValue) {
        g_notice = "게임의 부대 정보를 읽을 수 없어 하지 않았습니다.";
        log_line("%s", g_notice.c_str());
    } else if ((supply || stat) && wrote == Wrote::Failed) {
        g_notice = stat ? "부대의 상태 칸에 쓰지 못했습니다." : "부대의 보급 칸에 쓰지 못했습니다.";
        log_line("%s", g_notice.c_str());
    } else if (spawn && wrote == Wrote::Done) {
        g_last = "부대 생성 — " + (act.designs.size() == 1 ? "설계 " + std::to_string(act.designs[0])
                                                          : "설계 " + std::to_string(act.designs.size()) + "종")
            + " · " + std::to_string(made) + "기 · 칸 (" + std::to_string(hex_x) + ", " + std::to_string(hex_y) + ")"
            + (stated > 0 ? " · 상태 " + std::to_string(stated) + "기에" : std::string());
        log_line("게임의 함수: %s", g_last.c_str());
        if (stated < 0) {                      // 부대는 만들었는데 새 부대를 찾지 못했다
            g_notice = "부대는 만들었지만 상태는 쓰지 못했습니다 — 게임의 부대 목록에서 새 부대를 찾을 수 없습니다.";
            log_line("%s", g_notice.c_str());
        }
    } else if (spawn && wrote == Wrote::NoTarget) {
        g_notice = "지도에서 칸을 고르지 않아 하지 않았습니다 — 게임의 지도에서 칸을 고르십시오.";
    } else if (spawn && wrote == Wrote::BadValue) {
        g_notice = "그 번호의 부대 설계가 없어 하지 않았습니다.";
    } else if (annex && wrote == Wrote::Done && hexes == 0) {
        g_notice = "그 나라의 땅이 없습니다 — " + who;
    } else if (annex && wrote == Wrote::Done) {
        g_last = "병합 — " + who + " · " + std::to_string(hexes) + "칸";
        log_line("게임의 함수: %s", g_last.c_str());
    } else if (wrote == Wrote::Done) {
        g_last = std::string(fight ? "전쟁 붙이기 — " : become ? "이 나라로 플레이 — " : novichok ? "지도자 제거 — " : treaty ? "동맹 맺기 — "
                             : "식민지화 — ") + who;
        log_line("게임의 함수: %s", g_last.c_str());
    } else if (wrote == Wrote::NotUsed) {
        g_notice = "이미 동맹입니다 — " + who;
    } else if (wrote == Wrote::BadValue) {
        g_notice = annex ? "게임의 지도를 읽을 수 없어 하지 않았습니다." : novichok ? "게임의 지도자 표를 읽을 수 없어 하지 않았습니다." : treaty ? "게임의 조약 표를 읽을 수 없어 하지 않았습니다."
            : "게임의 플레이어 정보가 서로 맞지 않아 하지 않았습니다.";
        log_line("%s", g_notice.c_str());
    } else if (wrote == Wrote::NoTarget) {
        g_notice = "그 나라는 이번 판에 없어 하지 않았습니다.";
    } else if (wrote == Wrote::NotInGame) {
        g_notice = LEFT_GAME;
    } else {                                   // 꺼짐 · 예외: 까닭은 그 단추의 자리와 빨간 경고에 보인다. 남은 요청을 버린다
        g_queue.clear();
        g_research.clear();
        g_acts.clear();
    }
}

// 연구 탭이 보이는 동안의 스냅숏. g_lock 을 쥐지 않은 채로 부른다(읽는 동안 그리는 스레드를 세우지 않는다).
void watch_tick(unsigned long long now_ms)
{
    int picked = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        const bool due = g_watch_dirty || g_shot == nullptr || g_shot->picked != g_watch_picked || now_ms - g_shot_at >= RESEARCH_EVERY_MS;
        if (!g_watch_seen || !due || runner_calling())
            return;
        g_watch_seen = g_watch_dirty = false;
        g_shot_at = now_ms;
        picked = g_watch_picked;
    }
    std::shared_ptr<ResearchShot> shot = std::make_shared<ResearchShot>();
    const GameState game = game_state();
    const unsigned long long began = GetTickCount64();
    shot->picked = picked;
    shot->player = game.player;
    if (!game.known || !game.in_game)
        shot->why = "게임이 진행 중이 아닙니다";
    else if (game.multiplayer)
        shot->why = "멀티플레이에서는 연구를 읽지 않습니다";
    else
        shot->ok = game_research(picked, &shot->tables, &shot->why);
    std::lock_guard<std::mutex> lock(g_lock);
    if (shot->ok && !g_shot_timed) {           // 큰 판에서 얼마나 걸리는지 한 번 적어 둔다(첫 스냅숏은 부대 설계의 이름도 읽는다)
        g_shot_timed = true;
        log_line("연구 목록: 기술 %d개 · 부대 설계 %d개 · 노드 %d개를 %llu ms 에 읽음", static_cast<int>(shot->tables.techs.size()),
                 static_cast<int>(shot->tables.designs.size()), static_cast<int>(shot->tables.queue.size()), GetTickCount64() - began);
    }
    shot->serial = ++g_shot_serial;
    g_shot = shot;
}

}  // namespace

void keeper_watch_research(int picked)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_watch_seen = true;
    g_watch_picked = picked;
}

std::shared_ptr<const ResearchShot> keeper_research_shot()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_shot;
}

bool keeper_enqueue_colonize(int region)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_COLONIZE).empty())
        return false;
    g_acts.push_back({0, region});
    return true;
}

bool keeper_enqueue_become(int region)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_BECOME).empty())
        return false;
    g_acts.push_back({BECOME, region});
    return true;
}

void keeper_set_window(void *window)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_window = window;
}

bool keeper_enqueue_gui()
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_GUI).empty())
        return false;
    g_acts.push_back({GUI, 0, {}});
    return true;
}

bool keeper_enqueue_victory()
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_VICTORY).empty())
        return false;
    g_acts.push_back({VICTORY, 0, {}});
    return true;
}

bool keeper_enqueue_best(int domain, int each)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_BEST).empty())
        return false;
    g_acts.push_back({BEST, each, {domain}});
    return true;
}

bool keeper_enqueue_supply(bool fill, bool all)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(all ? ACT_SUPPLY_ALL : ACT_SUPPLY).empty())
        return false;
    g_acts.push_back({SUPPLY, (fill ? 1 : 0) | (all ? 2 : 0), {}});
    return true;
}

bool keeper_enqueue_unit_stat(int which, int percent, bool all)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || which < 0 || which >= STAT_COUNT || percent < 0 || percent > 100
        || !game_act_off(all ? ACT_UNITS : ACT_PICKED_UNITS).empty())
        return false;
    g_acts.push_back({STAT, which | (all ? 16 : 0), {percent}});
    return true;
}

bool keeper_enqueue_spawn(const std::vector<int> &designs, int each, const std::vector<int> &stats)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || designs.empty() || !game_act_off(ACT_SPAWN).empty())
        return false;
    g_acts.push_back({SPAWN, each, designs, stats});
    return true;
}

bool keeper_enqueue_annex(int region)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_ANNEX).empty())
        return false;
    g_acts.push_back({ANNEX, region});
    return true;
}

bool keeper_enqueue_treaty(int region)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_TREATY).empty())
        return false;
    g_acts.push_back({TREATY, region});
    return true;
}

bool keeper_enqueue_novichok(int region)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_ELECT).empty())
        return false;
    g_acts.push_back({NOVICHOK, region});
    return true;
}

bool keeper_enqueue_fight(int attacker, int target)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (attacker <= 0 || g_acts.size() >= RESEARCH_QUEUE || !game_act_off(ACT_FIGHT).empty())
        return false;
    g_acts.push_back({attacker, target});
    return true;
}

namespace {

void forget_cells()
{
    for (CellMemo &m : g_cells)
        m = CellMemo();
    g_cells_index = 0;
    g_mult_stats.held = 0;
    g_rush = false;
}

// 그 칸의 배율(퍼센트). 맡지 않는 칸이면 0.
int cell_percent(const Mult &m, int cell)
{
    if (cell == MUL_UNIT_BUILD)
        return m.unitbuild;
    if (cell == MUL_FAC_BUILD)
        return m.facbuild;
    if (cell >= MUL_RAW && cell < MUL_RAW + MULT_GOODS)
        return m.raw[cell - MUL_RAW];
    if (cell >= MUL_USE && cell < MUL_USE + MULT_GOODS)
        return m.use[cell - MUL_USE];
    if (cell >= MUL_OUT && cell < MUL_OUT + MULT_GOODS)
        return m.out[cell - MUL_OUT];
    return 0;
}

bool cell_sane(float v)
{
    return std::isfinite(v) && v > 0.0f && v < MUL_VALUE_MAX;
}

// 곱 보정 칸: 게임이 다시 셈해 지웠으면 다시 쓰고, 배율이 바뀌었으면 고쳐 쓰고, 100 이 됐으면 게임의 값으로 되돌린다. 쓰기가 실패하면 거짓.
bool mult_cells()
{
    float row[MUL_CELLS];
    int index = 0;
    if (!game_mult_row(row, &index)) {
        forget_cells();                         // 게임 밖이다 — 다음 판의 값은 그때 다시 읽는다
        return true;
    }
    if (index != g_cells_index) {
        const bool keep = g_rush && index == g_rush_index;   // 처음 본 줄이 누른 나라의 것이면 대기는 그대로다
        forget_cells();                         // 플레이하는 나라가 바뀌었다 — 앞 나라의 칸은 게임이 그날 다시 셈한다
        g_rush = keep;
        g_cells_index = index;
    }
    if (g_rush) {                               // 대기를 풀지는 고리 앞에서 한 번 정한다(칸 둘이 함께)
        const Daily today = game_daily();
        bool held = false, changed = true;      // 대기 값을 쥔 칸이 있는가 · 그 칸이 모두 게임의 값으로 바뀌었는가
        for (const int cell : {MUL_FAC_BUILD})
            if (g_cells[cell].have) {
                held = true;
                changed = changed && row[cell] != g_cells[cell].wrote;
            }
        const uint32_t passed = today.ok ? today.day - g_rush_day : 0;   // 뒤로 간 날(불러오기)은 큰 수가 된다
        if (passed >= RUSH_DAYS_MAX || (passed > 0 && (!held || changed))) {
            g_rush = false;                     // 날이 오른 뒤 게임이 다시 셈했다(그날의 셈이 지났다) — 또는 너무 오래 기다렸다
            log_line("배율: 다음 자정에 완료 — 풀었다 (날 %u → %u)", g_rush_day, today.day);
        }
    }
    int held = 0;
    for (int cell = 1; cell < MUL_CELLS; cell++) {
        const int percent = cell_percent(g_mult, cell);
        if (percent == 0)
            continue;
        CellMemo &m = g_cells[cell];
        const float now = row[cell];
        const bool build = cell == MUL_FAC_BUILD;
        const bool rush = g_rush && build;
        const bool scaled = rush || (g_mult.on && percent != 100);
        const float times = rush ? RUSH_TIMES : percent / 100.0f;
        float want = 0;
        if (m.have && now == m.wrote) {         // ToyBox 가 쓴 값이 그대로다
            want = scaled ? m.base * times : m.base;
            if (want == now) {
                held++;
                continue;
            }
        } else {                                // 처음이거나 게임이 다시 셈했다 — 지금 값이 게임의 값이다
            m = CellMemo();
            if (!scaled || !cell_sane(now))
                continue;
            m.base = now;
            want = now * times;
        }
        const float max = rush ? RUSH_VALUE_MAX : MUL_VALUE_MAX;
        if (!(std::isfinite(want) && want > 0.0f && want < max)) {
            m = CellMemo();
            continue;
        }
        const Wrote wrote = game_write_mult(cell, want, max);
        if (wrote != Wrote::Done) {
            m = CellMemo();
            if (wrote == Wrote::Failed)
                return false;
            continue;
        }
        g_mult_stats.writes++;
        if (scaled) {
            m.have = true;
            m.wrote = want;
            held++;
        } else {
            m = CellMemo();                     // 게임의 값으로 되돌렸다
        }
    }
    g_mult_stats.held = held;
    return true;
}

// 날마다의 일: 날의 칸이 오른 만큼 세금 · 국방비를 국고에, 연구 속도를 노드의 남은 기간에.
void mult_daily()
{
    const Daily d = game_daily();
    if (!d.ok) {
        g_day_known = false;
        return;
    }
    if (!g_day_known || d.index != g_day_index) {
        g_day_known = true;                     // 처음 봤거나 나라가 바뀌었다 — 맞추기만 한다
        g_day = d.day;
        g_day_index = d.index;
        return;
    }
    const uint32_t passed = d.day - g_day;      // 뒤로 간 날(불러오기)은 큰 수가 된다
    g_day = d.day;
    if (passed == 0 || passed > MULT_DAYS_MAX || !g_mult.on)
        return;
    double delta = 0;
    if (game_mult_off(MULT_PART_MONEY).empty())
        delta = (g_mult.tax / 100.0 - 1.0) * d.tax * passed - (g_mult.defense / 100.0 - 1.0) * d.defense * passed;
    if (game_mult_off(MULT_PART_SOCIAL).empty())
        delta -= (g_mult.social / 100.0 - 1.0) * d.social * passed;
    int nodes = 0;
    bool did = false;
    if (delta != 0.0) {
        const GameValues now = game_values();
        if (now.ok && std::isfinite(now.treasury) && std::isfinite(now.treasury + delta)
            && game_write_treasury(now.treasury + delta) == Wrote::Done)
            did = true;
        else
            delta = 0.0;
    }
    if (g_mult.research != 100 && d.step > 0.0f && game_mult_off(MULT_PART_SPEED).empty()
        && game_research_push(static_cast<float>((g_mult.research / 100.0 - 1.0) * d.step * passed), &nodes) == Wrote::Done)
        did = did || nodes > 0;
    if (!did)
        return;
    g_mult_stats.days += passed;
    if (!g_mult_told) {
        g_mult_told = true;                     // 날마다 적지 않는다 — 설정이 바뀐 뒤의 첫 날만
        log_line("배율: 날마다의 일 (날 %u → +%u일, 국고 %+.0f — 세금 %d%% · 국방비 %d%% · 사회 지출 %d%%, 연구 %d%% — 노드 %d개, 하루치 %.4g일)", d.day,
                 passed, delta, g_mult.tax, g_mult.defense, g_mult.social, g_mult.research, nodes, d.step);
    }
}

const char *const SOCIAL_NAMES[SOCIAL_COUNT] = {"보건", "교육", "인프라", "환경", "가족 수당", "치안", "문화 지원", "사회 보장"};

bool grade_sane(float v)
{
    return std::isfinite(v) && v >= 0.0f && v < SOCIAL_GRADE_SANE;
}

const char *const FLOOR_NAMES[FLOOR_COUNT] = {"국민 지지", "군부 지지", "여론"};

// 지지율 · 여론의 최소 유지. g_lock 을 쥔 채로 부른다.
void floor_tick(unsigned long long now_ms)
{
    if (!(g_floor_on[0] || g_floor_on[1] || g_floor_on[2]) || runner_calling() || runner_faulted() || now_ms - g_floor_at < KEEP_EVERY_MS
        || !game_writes())
        return;
    g_floor_at = now_ms;
    for (int i = 0; i < FLOOR_COUNT; i++) {
        if (!g_floor_on[i])
            continue;
        const Floor which = static_cast<Floor>(i);
        const float floor = static_cast<float>(g_floor_pct[i]) / 100.0f;
        float now = 0;
        if (!game_floor_value(which, &now) || !std::isfinite(now) || now < 0.0f || now > 1.0f || now >= floor)
            continue;
        if (game_write_floor_value(which, floor) == Wrote::Done && !g_floor_told[i]) {
            g_floor_told[i] = true;
            log_line("유지: %s %.1f %% -> %d %%", FLOOR_NAMES[i], now * 100.0f, g_floor_pct[i]);
        }
    }
}

// 점령지 충성의 유지. g_lock 을 쥔 채로 부른다(game_loyalty 는 게임의 함수를 부르지 않는다 — 쓰기만 한다).
void loyalty_tick(unsigned long long now_ms)
{
    if (!g_loyalty_keep || runner_calling() || runner_faulted() || !game_writes())
        return;
    if (!g_loyalty_due && now_ms - g_loyalty_at < LOYALTY_EVERY_MS)
        return;
    g_loyalty_due = false;
    g_loyalty_at = now_ms;
    int hexes = 0, target = 0, number = 0;
    if (game_loyalty(&hexes, &target, &number) == Wrote::Done && hexes > 0)
        log_line("유지: 점령지 충성 %d칸 → %s", hexes, region_label(number).c_str());
}

// 예비 인원의 최소 유지. g_lock 을 쥔 채로 부른다.
void reserve_tick(unsigned long long now_ms)
{
    if (!g_reserve_keep || runner_calling() || runner_faulted() || now_ms - g_reserve_at < KEEP_EVERY_MS || !game_writes())
        return;
    g_reserve_at = now_ms;
    float now = 0;
    const float floor = static_cast<float>(g_reserve_floor);
    if (!game_reserve(&now) || !std::isfinite(now) || now < 0.0f || now >= floor)
        return;
    if (game_write_reserve(floor) == Wrote::Done && !g_reserve_told) {
        g_reserve_told = true;
        log_line("유지: 예비 %s -> %s", short_amount(now).c_str(), short_amount(floor).c_str());
    }
}

// 사회의 틱. g_lock 을 쥔 채로 부른다. 게임의 함수는 부르지 않는다.
void social_tick(unsigned long long now_ms)
{
    if (runner_calling() || runner_faulted())
        return;
    if (!g_social_due && now_ms - g_social_at < KEEP_EVERY_MS)
        return;
    g_social_due = false;
    g_social_at = now_ms;
    bool rates = false, keeping = false;
    for (int i = 0; i < SOCIAL_COUNT; i++) {
        rates = rates || (g_social.on && (g_social.up[i] != 100 || g_social.down[i] != 100));
        keeping = keeping || g_social.keep[i];
    }
    if ((!rates && !keeping) || !game_writes()) {
        g_social_have = false;
        return;
    }
    const Grades now = game_social_grades();
    if (!now.ok) {
        g_social_have = false;
        return;
    }
    float grade[SOCIAL_COUNT];
    memcpy(grade, now.grade, sizeof(grade));
    const uint32_t passed = now.day - g_social_day;   // 뒤로 간 날(불러오기)은 큰 수가 된다
    const bool new_day = rates && g_social_have && now.index == g_social_index && passed >= 1 && passed <= MULT_DAYS_MAX;
    if (!new_day)
        g_social_waiting = false;
    else if (!g_social_waiting) {
        g_social_waiting = true;                // 게임은 자정의 셈 도중에도 메시지를 돌린다 — 날만 오르고 등급은 아직 옛 값인 틱이 온다
        g_social_wait_from = now_ms;            // (2026-10-21 게임에서 봤다). 등급이 바뀐 것을 볼 때까지 자정 전의 값을 쥐고 기다린다
    }
    if (g_social_waiting) {
        bool changed = false;
        for (int i = 0; i < SOCIAL_COUNT; i++)
            changed = changed || grade[i] != g_social_seen[i];
        if (!changed && now_ms - g_social_wait_from < SOCIAL_SETTLE_MS)
            return;                             // 아직 게임이 등급을 셈하지 않았다 — 유지도 쉰다(자정 전의 값을 덮지 않게)
    }
    if (g_social_waiting) {
        g_social_waiting = false;
        int moved = 0;
        for (int i = 0; i < SOCIAL_COUNT; i++) {
            if (!grade_sane(grade[i]) || !grade_sane(g_social_seen[i]))
                continue;
            const float d = grade[i] - std::min(g_social_seen[i], SOCIAL_GRADE_TOP);
            const int percent = d > 0.0f ? g_social.up[i] : d < 0.0f ? g_social.down[i] : 100;
            if (percent == 100)
                continue;
            const float want = std::min(SOCIAL_GRADE_TOP, std::max(0.0f, grade[i] + (percent / 100.0f - 1.0f) * d));
            if (want == grade[i])
                continue;
            const Wrote wrote = game_write_social_grade(i, want);
            if (wrote == Wrote::Failed) {
                g_social_have = false;
                return;
            }
            if (wrote == Wrote::Done) {
                grade[i] = want;
                moved++;
            }
        }
        if (!g_social_told) {
            g_social_told = true;                  // 날마다 적지 않는다 — 설정이 바뀐 뒤의 첫 날만
            log_line("사회: 날마다의 일 (날 %u → +%u일, 품질 등급 %d칸을 더 움직였다)", now.day, passed, moved);
        }
    }
    for (int i = 0; i < SOCIAL_COUNT; i++) {
        const float floor = g_social.keep_value[i] / 100.0f;
        if (!g_social.keep[i] || !grade_sane(grade[i]) || grade[i] >= floor)
            continue;
        const Wrote wrote = game_write_social_grade(i, floor);
        if (wrote == Wrote::Failed) {
            g_social_have = false;
            return;
        }
        if (wrote != Wrote::Done)
            continue;
        if (!g_social_kept[i]) {
            g_social_kept[i] = true;
            log_line("사회 유지: %s 품질 등급 %.1f%% -> %.1f%%", SOCIAL_NAMES[i], grade[i] * 100.0f, floor * 100.0f);
        }
        grade[i] = floor;
    }
    memcpy(g_social_seen, grade, sizeof(grade));
    g_social_index = now.index;
    g_social_day = now.day;
    g_social_have = true;
}

// 배율의 틱. g_lock 을 쥔 채로 부른다. 게임의 함수는 부르지 않는다.
void mult_tick(unsigned long long now_ms)
{
    if (runner_calling() || runner_faulted())
        return;
    if (!g_mult_due && now_ms - g_mult_at < KEEP_EVERY_MS)
        return;
    g_mult_due = false;
    g_mult_at = now_ms;
    if (!g_mult.on && g_cells_index == 0 && !g_rush) {   // 꺼져 있고 되돌릴 칸도, 완료 대기도 없다
        g_day_known = false;
        return;
    }
    if (!game_writes())
        return;
    if (!mult_cells())
        return;                                 // 쓰기가 실패했다 — 값 쓰기 전체가 꺼졌다
    if (!g_mult.on) {
        g_day_known = false;
        if (g_mult_stats.held == 0 && !g_rush)
            g_cells_index = 0;                  // 다 되돌렸다 — 켤 때까지 쉰다
        return;
    }
    mult_daily();
}

}  // namespace

bool operator==(const Mult &a, const Mult &b)
{
    if (a.on != b.on || a.tax != b.tax || a.defense != b.defense || a.social != b.social || a.research != b.research
        || a.unitbuild != b.unitbuild || a.facbuild != b.facbuild)
        return false;
    for (int i = 0; i < MULT_GOODS; i++)
        if (a.out[i] != b.out[i] || a.use[i] != b.use[i] || a.raw[i] != b.raw[i])
            return false;
    return true;
}

bool operator==(const Social &a, const Social &b)
{
    if (a.on != b.on)
        return false;
    for (int i = 0; i < SOCIAL_COUNT; i++)
        if (a.up[i] != b.up[i] || a.down[i] != b.down[i] || a.keep[i] != b.keep[i] || a.keep_value[i] != b.keep_value[i])
            return false;
    return true;
}

void keeper_set_social(const Social &social)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_social = social;
    g_social_due = true;
    g_social_told = false;
}

Social keeper_social()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_social;
}

int social_count(const Social &s)
{
    int count = 0;
    for (int i = 0; i < SOCIAL_COUNT; i++)
        count += (s.up[i] != 100) + (s.down[i] != 100) + (s.keep[i] ? 1 : 0);
    return count;
}

void keeper_set_loyalty_keep(bool on)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_loyalty_keep = on;
    g_loyalty_due = on;
}

void keeper_set_floor_keep(Floor which, bool on, int percent)
{
    const int i = static_cast<int>(which);
    if (i < 0 || i >= FLOOR_COUNT)
        return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_floor_on[i] = on;
    g_floor_pct[i] = std::min(FLOOR_PERCENT_MAX, std::max(0, percent));
    g_floor_told[i] = false;
    g_floor_at = 0;
    g_loyalty_keep = g_loyalty_due = false;
    g_loyalty_at = 0;
}

void keeper_set_reserve_keep(bool on, long long value)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_reserve_keep = on;
    g_reserve_floor = std::min(RESERVE_KEEP_MAX, std::max(0LL, value));
    g_reserve_at = 0;
}

void keeper_set_mult(const Mult &mult)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_mult = mult;
    g_mult_due = true;
    g_mult_told = false;
}

Mult keeper_mult()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_mult;
}

std::string keeper_rush()
{
    if (!game_writes())
        return "값 쓰기가 꺼져 있습니다.";
    const std::string cells = game_mult_off(MULT_PART_CELLS);
    if (!cells.empty())
        return cells;
    const Daily d = game_daily();
    if (!d.ok)
        return "게임을 진행 중일 때만 됩니다.";
    std::lock_guard<std::mutex> lock(g_lock);
    g_rush = true;
    g_rush_day = d.day;
    g_rush_index = d.index;
    g_mult_due = true;
    log_line("배율: 다음 자정에 완료 — 대기 (날 %u)", d.day);
    return "";
}

void keeper_mult_put_back()
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_rush)
        log_line("배율: 다음 자정에 완료 — 버렸다 (플레이하는 나라를 바꾼다)");
    g_rush = false;
    float row[MUL_CELLS];
    int index = 0;
    if (game_writes() && game_mult_row(row, &index) && index == g_cells_index)
        for (int cell = 1; cell < MUL_CELLS; cell++)
            if (g_cells[cell].have && row[cell] == g_cells[cell].wrote)
                game_write_mult(cell, g_cells[cell].base);
    forget_cells();
}

bool keeper_rush_pending()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_rush;
}

int mult_count(const Mult &m)
{
    int count = (m.tax != 100) + (m.defense != 100) + (m.social != 100) + (m.research != 100) + (m.unitbuild != 100) + (m.facbuild != 100);
    for (int i = 0; i < MULT_GOODS; i++)
        count += (m.out[i] != 100) + (m.use[i] != 100) + (m.raw[i] != 100);
    return count;
}

MultStats keeper_mult_stats()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_mult_stats;
}

void keeper_tick(unsigned long long now_ms)
{
    Job job;
    bool write = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        write = tick_locked(now_ms, &job);
        mult_tick(now_ms);                     // 단추의 요청과 최소 유지 뒤에 — 방금 쓴 국고에 그날의 세금 · 국방비를 얹는다
        social_tick(now_ms);
        reserve_tick(now_ms);
        floor_tick(now_ms);
        loyalty_tick(now_ms);
    }
    if (write && job.act) {
        run_act(job.what);
    } else if (write) {
        run_research(job.research);
        std::lock_guard<std::mutex> lock(g_lock);
        g_watch_dirty = true;                  // 쓴 것이 목록에 바로 보이게
    }
    watch_tick(now_ms);
}

std::string keeper_last()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_last;
}

std::string keeper_notice()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_notice;
}

void keeper_set_keep(const Keep &keep)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (keep.treasury != g_keep.treasury || keep.treasury_floor != g_keep.treasury_floor)
        g_kept[0] = false;                     // 바뀐 항목은 다음에 올릴 때 다시 로그에 적는다
    for (int slot = 0; slot < STOCK_SLOTS; slot++)
        if (keep.stock[slot] != g_keep.stock[slot] || keep.stock_floor[slot] != g_keep.stock_floor[slot])
            g_kept[1 + slot] = false;
    g_keep = keep;
    g_keep_due = true;
}

Keep keeper_keep()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_keep;
}

int keep_count(const Keep &keep)
{
    int count = keep.treasury ? 1 : 0;
    for (int slot = 0; slot < STOCK_SLOTS; slot++)
        count += keep.stock[slot] ? 1 : 0;
    return count;
}

std::string keep_active_text(const Keep &keep, const bool *used)
{
    std::string first, names;
    int count = 0;
    for (int slot = TREASURY; slot < STOCK_SLOTS; slot++) {
        if (slot == TREASURY ? !keep.treasury : !(keep.stock[slot] && used[slot]))
            continue;
        const std::string name = target_name(slot);
        if (count++ == 0)
            first = name;
        else
            names += ", ";
        names += name;
    }
    if (count == 0)
        return std::string();
    return " · 유지 중: " + (count > 4 ? first + " 외 " + std::to_string(count - 1) + "개" : names);
}

std::string keep_resting_text(const Keep &keep, const char *why)
{
    const int count = keep_count(keep);
    return count == 0 ? std::string() : " · 유지 " + std::to_string(count) + "개 켜짐(" + why + ")";
}

void keeper_reset_for_test()
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_queue.clear();
    g_research.clear();
    g_acts.clear();
    g_last.clear();
    g_notice.clear();
    g_keep = Keep();
    g_keep_due = false;
    g_keep_at = 0;
    g_mult = Mult();
    g_mult_due = g_day_known = g_mult_told = g_rush = false;
    g_mult_at = 0;
    g_mult_stats = MultStats();
    forget_cells();
    g_social = Social();
    g_reserve_keep = g_reserve_told = false;
    for (int i = 0; i < FLOOR_COUNT; i++)
        g_floor_on[i] = g_floor_told[i] = false, g_floor_pct[i] = 0;
    g_floor_at = 0;
    g_reserve_floor = 0;
    g_reserve_at = 0;
    g_social_due = g_social_have = g_social_told = g_social_waiting = false;
    g_social_at = 0;
    memset(g_social_kept, 0, sizeof(g_social_kept));
    memset(g_kept, 0, sizeof(g_kept));
    g_shot.reset();
    g_watch_seen = g_watch_dirty = g_shot_timed = false;
    g_watch_picked = 0;
    g_shot_at = 0;
}
