#include "ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "features.h"
#include "game.h"
#include "imgui.h"
#include "keeper.h"
#include "log.h"
#include "overlay.h"
#include "products.h"
#include "regions.h"
#include "runner_win.h"
#include "settings.h"
#include "techs.h"

namespace {

typedef std::lock_guard<std::recursive_mutex> Lock;

const char *const DIPLOMACY_TAB = "외교·영토";   // features.cpp 의 탭 이름과 같아야 한다
const char *const RESEARCH_TAB = "연구";         // 〃
const char *const LIST_UNLOCK_ALL = "list:all_unlock";   // g_confirm: "보이는 것 전부 연구 가능으로"가 둘째 누름을 기다린다
// "연구 가능으로" 단추 둘의 풍선말(docs/superpowers/specs/2026-10-15-toybox-research-unlock-design.md · 2026-10-17-toybox-region-unlock-design.md)
const char *const UNLOCK_TIP = "항목 자체는 보유가 되지 않는다 — 모자란 선행 기술만 보유로 준다. 연구는 게임에서 한다. "
                               "부대 설계에 업그레이드의 앞 설계가 있으면 그 하나를 등록한다 — 그러면 그 앞 설계를 가리키는 더 옛 설계는 게임의 연구 목록에서 빠진다(이미 개량됨). "
                               "다른 지역 전용이면 그 지역 코드 하나를 내 나라에 더한다 — 같은 코드의 다른 나라 설계도 함께 열리고, 되돌리지 않는다. "
                               "게임의 연구 장관이 그것을 연구할 수 있다";
const char *const LIST_UNDO_ALL = "list:all_undo";   // g_confirm: "보이는 것 전부 미완료"가 둘째 누름을 기다린다
const char *const LIST_UNDO_EVERY = "list:every_undo";   // 〃 부대 탭의 "모든 설계도 해제"
const char *const UNITS_TAB = "부대";            // features.cpp 의 탭 이름과 같아야 한다

Settings g_settings;
bool g_visible, g_capturing;
float g_rect[4];              // 창의 왼쪽 · 위 · 오른쪽 · 아래 (지난 프레임)
std::string g_confirm;        // 한 번 더 누르기를 기다리는 기능의 id
std::string g_notice;
int g_picked;                 // "외교·영토" 탭에서 고른 나라의 번호(없으면 0)
char g_filter[64];            // 나라 검색란
std::vector<int> g_regions;   // 이번 게임에 있는 지역
double g_regions_at = -10.0;  // 그것을 읽은 때(ImGui 의 시계, 초)
bool g_reporting;             // 테스트가 그린 것의 목록을 청했다(ui_report). 그 전에는 모으지 않는다
std::string g_report, g_drawing;   // 지난 프레임의 목록 / 지금 모으는 것
float g_footer;               // 바닥 줄들(알림 · 안내)의 높이 — 지난 프레임에 잰 것
Keep g_keep_sent;             // keeper 에 마지막으로 넘긴 최소 유지(저장한 설정과 같다)
Mult g_mult_sent;             // keeper 에 마지막으로 넘긴 배율(〃)
// 기술 · 부대 설계의 목록 하나의 상태. 연구 탭의 목록과 부대 탭의 설계도 목록이 저마다 하나씩 갖는다(같은 코드로 그린다 — item_list).
struct ListState {
    int each = 1;                 // 부대 탭: "고른 것을 수량만큼 생성"의 수량(설계마다)
    bool stat_on[STAT_COUNT] = {};                 // 〃 만든 부대의 경험 · 사기 · 효율을 정한다(꺼져 있으면 게임이 준 값 그대로)
    int stat_value[STAT_COUNT] = {100, 100, 100};  // 그 퍼센트(0 … 100)
    ListFilter list;              // 거르개
    char find[64] = "";           // 찾기란
    char owner_find[64] = "";     // 보유국 고르기 안의 검색란
    int sort_column = -1;         // 놓은 열과 방향 — 줄을 다시 만들면 이것으로 다시 놓는다
    bool sort_down = false;
    std::set<int> chosen;         // 고른 번호(지금 목록 — 기술이나 부대 설계 — 의 것)
    int chosen_player = 0;        // 그것을 고를 때의 플레이어. 나라가 바뀌면 고른 것을 푼다
    std::vector<ListRow> rows;    // 거르개를 지난 줄 — 스냅숏이나 거르개가 바뀌었을 때만 다시 만든다
    unsigned long long rows_serial = 0;
    ListFilter rows_filter;
};
ListState g_research_list, g_units_list;

// 방금 그린 항목을 적는다: "이름\t가운데 x\t가운데 y\t보이는가(0/1)\t글". 테스트가 단추의 자리와 글을 여기서 읽는다.
void note(const std::string &name, const std::string &text)
{
    if (!g_reporting)
        return;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 middle((a.x + b.x) / 2, (a.y + b.y) / 2);
    const bool seen = ImGui::IsRectVisible(ImVec2(middle.x - 1, middle.y - 1), ImVec2(middle.x + 1, middle.y + 1));   // 가운데가 가려지지 않았다
    g_drawing += name + '\t' + std::to_string(static_cast<int>(middle.x)) + '\t' + std::to_string(static_cast<int>(middle.y)) + '\t'
        + (seen ? "1" : "0") + '\t' + text + '\n';
}

// 긴 안내 글(흐린 글씨). 창이 좁으면 줄을 바꾼다 — 그냥 두면 창 밖으로 잘린다(이미 써 본 사용자의 창은 저장된 크기 그대로다).
// name 으로 그 글의 오른쪽 끝(x)을 적는다 — 창 안에 드는지 테스트가 본다.
void help(const char *name, const char *text)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
    note(name, std::to_string(static_cast<int>(ImGui::GetItemRectMax().x)));
}

// 되돌릴 수 없는 단추가 둘째 누름을 기다릴 때의 글: 무엇을 어느 나라에 하는지를 그 단추에 적는다.
std::string asking_label(const Feature &f, int region)
{
    std::string what = f.label;
    if (f.target != Target::None && region > 0)
        what += ": " + region_label(region) + " (" + std::to_string(region) + ")";
    return what + " — 한 번 더 누르면 실행합니다";
}

// 직접 쓰는 줄(3단계 2 · 3): 내장 치트를 거치지 않는다 — 누르면 요청을 대기열에 넣고, 쓰는 것은 게임 창의 타이머에서다(keeper.h).
// 쓸 수 없으면 단추 자리에 까닭 한 줄만 그린다(그 줄에만. 같은 탭의 다른 줄은 그대로다). 내장 치트로 되돌아가지 않는다.
// 단추를 그렸으면(쓸 수 있으면) true.
// compact: 설명 글을 줄로 그리지 않고 단추의 풍선말로 보인다(연구 탭 — 단추 셋이 한 줄에 놓인다).
int g_best_domain = -1;   // 최강 부대 묶음의 영역(design_domain 의 번호. -1 이면 모든 영역)

// "지도에서 고른 칸"의 글(부대 생성의 줄과 설계도 목록이 같이 쓴다)
std::string hex_label(bool on_hex, int x, int y)
{
    return on_hex ? "지도에서 고른 칸: (" + std::to_string(x) + ", " + std::to_string(y) + ")"
                  : std::string("지도에서 고른 칸: 없음 — 게임의 지도에서 칸을 고르십시오");
}

bool direct_row(const Feature &f, const GameState &game, bool compact)
{
    // 되돌릴 수 없는 줄은 설명에도 한 번 더 누르기를 적는다 — 단추를 누르기 전에는 알 수 없었다(2026-10-19)
    const std::string help = std::string(f.help) + (f.confirm ? " — 한 번 더 눌러야 실행된다" : "");
    const bool research = f.direct == Direct::TechLevel || f.direct == Direct::QueueDone;   // 연구의 두 줄은 제 묶음(연구)을 본다
    const int group = f.direct == Direct::TechUp ? MORE_TECH : f.direct == Direct::OpinionBest ? MORE_OPINION
        : f.direct == Direct::PeopleAdd ? MORE_PEOPLE : f.direct == Direct::ApprovalBest ? MORE_APPROVAL : MORE_RELATIONS;
    const bool fight = f.direct == Direct::Fight;
    const bool become = f.direct == Direct::Become;
    const bool novichok = f.direct == Direct::Novichok;
    const bool treaty = f.direct == Direct::Treaty;
    const bool annex = f.direct == Direct::Annex;
    const bool supply = f.direct == Direct::Supply;
    const bool best = f.direct == Direct::Best;
    const bool victory = f.direct == Direct::Victory;
    const bool gui = f.direct == Direct::Gui;
    const bool act = f.direct == Direct::Colonize || fight || become || novichok || treaty || annex || supply || best || victory || gui;   // 게임의 함수를 부르는 줄
    const std::string off = !game.known ? std::string("게임 상태를 읽을 수 있을 때만 씁니다.")
        : research ? game_research_off() : act ? game_act_off(gui ? ACT_GUI : victory ? ACT_VICTORY : best ? ACT_BEST : supply ? ACT_SUPPLY : fight ? ACT_FIGHT : become ? ACT_BECOME : novichok ? ACT_ELECT : treaty ? ACT_TREATY : annex ? ACT_ANNEX : ACT_COLONIZE)
        : game_more_off(group);
    ImGui::PushID(f.id);
    if (!off.empty()) {
        const std::string line = std::string(f.label) + " — " + off;
        ImGui::TextWrapped("%s", line.c_str());
        note(std::string("off:") + f.id, line);
    } else {
        long long value = 0;
        if (f.has_value) {                                                   // 기술 수준. 쓸 수 없을 때는 입력란도 그리지 않는다
            long long &stored = g_settings.values[f.id];
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &stored)) {
                stored = stored < f.min ? f.min : stored > f.max ? f.max : stored;
                save_settings(g_settings);
            }
            note(std::string("value:") + f.id, std::to_string(stored));
            value = stored;
            ImGui::SameLine();
        }
        if (best) {                                                          // 최강 부대 묶음: 받을 영역(육 · 공 · 해. 처음은 모든 영역)
            static const char *const NAMES[] = {"모든 영역", "육군", "공군", "해군"};
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::BeginCombo("##domain", NAMES[g_best_domain + 1])) {
                for (int d = -1; d < 3; d++) {
                    if (ImGui::Selectable(NAMES[d + 1], d == g_best_domain)) {
                        g_best_domain = d;
                        g_confirm.clear();
                    }
                    note("best:domain:" + std::to_string(d), NAMES[d + 1]);
                }
                ImGui::EndCombo();
            }
            note("best:domain", NAMES[g_best_domain + 1]);
            ImGui::SameLine();
        }
        const auto named = [](int number) { return region_label(number) + " (" + std::to_string(number) + ")"; };
        const int on_map = fight ? game_map_pick() : 0;                       // 전쟁의 첫째 나라: 게임의 지도에서 고른 나라
        if (fight) {
            const std::string map = "지도에서 고른 나라: " + (on_map > 0 ? named(on_map) : std::string("없음 — 게임의 지도에서 나라를 고르십시오"));
            ImGui::TextUnformatted(map.c_str());
            note("fight:map", map);
        }
        int units = 0, mine = 0;                                             // 보급: 게임에서 고른 부대(내 부대에만 쓴다)
        const std::string picked = !supply ? std::string() : !game_picked_units(&units, &mine) ? std::string("고른 부대: 읽을 수 없음")
            : units == 0 ? std::string("고른 부대: 없음 — 게임에서 내 부대를 고르십시오")
            : "고른 부대: " + std::to_string(units) + "기" + (mine != units ? " (내 부대 " + std::to_string(mine) + "기)" : std::string());
        // 나라를 고르지 않았다(목록에서 · 전쟁은 지도에서도). 같은 나라끼리는 싸움 붙일 수 없다. 부대 생성은 칸을, 보급은 내 부대를 골라야 한다
        ImGui::BeginDisabled((f.target == Target::Picked && g_picked <= 0) || (fight && (on_map <= 0 || on_map == g_picked))
                             || (supply && mine <= 0));
        const bool asking = f.confirm && g_confirm == f.id;                   // 되돌릴 수 없는 줄은 한 번 더 눌러야 한다
        const std::string label = !asking ? std::string(f.label)
            : fight ? std::string(f.label) + ": " + named(on_map) + " -> " + named(g_picked) + " — 한 번 더 누르면 실행합니다"
            : asking_label(f, g_picked);
        bool pressed = ImGui::Button((label + "###run").c_str());
        note(std::string("run:") + f.id, label);
        if (pressed && f.confirm && !asking) {
            g_confirm = f.id;
            pressed = false;
        }
        if (compact && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", help.c_str());
        if (supply) {                                                        // 옆의 단추: 0 으로
            ImGui::SameLine();
            if (ImGui::Button("비우기###empty")) {
                g_confirm.clear();
                g_notice = keeper_enqueue_supply(false) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
            }
            note(std::string("empty:") + f.id, "비우기");
        }
        if (supply) {                                                        // 지금 고른 부대(게임에서 고른 것)
            ImGui::SameLine();
            ImGui::TextDisabled("%s", picked.c_str());
            note("supply:picked", picked);
        }
        if (pressed && act) {
            g_confirm.clear();
            g_notice = (gui ? keeper_enqueue_gui() : victory ? keeper_enqueue_victory() : best ? keeper_enqueue_best(g_best_domain, static_cast<int>(value)) : supply ? keeper_enqueue_supply(true) : fight ? keeper_enqueue_fight(on_map, g_picked) : become ? keeper_enqueue_become(g_picked)
                        : novichok ? keeper_enqueue_novichok(g_picked) : treaty ? keeper_enqueue_treaty(g_picked)
                        : annex ? keeper_enqueue_annex(g_picked) : keeper_enqueue_colonize(g_picked))
                ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
        } else if (pressed && research) {
            ResearchRequest request = {Research::Complete, ResearchWhat(), "대기열"};
            request.what.kind = ResearchWhat::Queue;
            if (f.direct == Direct::TechLevel) {
                request.what.kind = ResearchWhat::Level;
                request.what.level = static_cast<int>(value);
                request.label = "기술 수준 " + std::to_string(value) + " 이하";
            }
            g_confirm.clear();
            g_notice = keeper_enqueue_research(request) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
        } else if (pressed) {
            Request request = {TECH, Change::Set, 0.0, 0};
            if (f.direct == Direct::OpinionBest)
                request.slot = OPINION;
            else if (f.direct == Direct::PeopleAdd)
                request = {PEOPLE, Change::Add, 1e6, 0};
            else if (f.direct == Direct::ApprovalBest)
                request.slot = APPROVAL;
            else if (f.direct != Direct::TechUp)
                request = {RELATION, Change::Set, f.direct == Direct::RelationBest ? 1.0 : 0.0, g_picked};
            g_confirm.clear();
            g_notice = keeper_enqueue(request) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
        }
        ImGui::EndDisabled();
    }
    if (!compact) {
        ImGui::PushTextWrapPos(0.0f);   // 긴 설명이 창 오른쪽에서 잘렸다(2026-10-19 — 최강 부대 묶음) — 창 폭에서 줄을 바꾼다
        ImGui::TextDisabled("%s", help.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    ImGui::PopID();
    return off.empty();
}


// 이번 게임에 있는 나라의 목록과 지금 고른 나라. "고른 나라" 줄을 그린다 — 탭의 구르는 내용 밖에.
// none: 고른 나라가 없을 때의 글(탭마다 다르다 — 고르는 목록은 "외교·영토" 탭에만 있다).
RegionView choose(const GameState &game, const char *none)
{
    if (ImGui::GetTime() - g_regions_at > 1.0) {   // 지역 수백 개를 읽는다 — 프레임마다 하지 않는다
        g_regions = game_regions();
        g_regions_at = ImGui::GetTime();
    }
    const RegionView view = region_view(g_regions, game.player, g_picked, g_filter);
    if (view.picked != g_picked) {                 // 다른 판을 불러와 그 나라가 없어졌거나, 그 나라로 플레이하게 됐다
        g_picked = view.picked;
        g_confirm.clear();
    }
    const std::string chosen = g_picked != 0 ? "고른 나라: " + region_label(g_picked) + " (" + std::to_string(g_picked) + ")"
                                             : std::string("고른 나라: 없음 — ") + none;
    if (g_picked != 0)
        ImGui::TextUnformatted(chosen.c_str());
    else
        ImGui::TextDisabled("%s", chosen.c_str());
    note("picked", chosen);
    return view;
}

// 나라 고르기: 이번 게임에 있는 나라를 이름순으로. 검색은 이름(한글 · 영문)이나 번호의 일부.
void picker(const RegionView &view)
{
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##search", "검색 (이름 · 번호)", g_filter, sizeof(g_filter));
    note("search", g_filter);
    if (ImGui::BeginListBox("##regions", ImVec2(-FLT_MIN, 6.5f * ImGui::GetTextLineHeightWithSpacing()))) {
        for (int number : view.rows) {
            const std::string label = region_label(number) + " (" + std::to_string(number) + ")";
            const bool chose = ImGui::Selectable(label.c_str(), number == g_picked);
            note("row:" + std::to_string(number), label);
            if (chose) {
                g_picked = number;
                g_confirm.clear();   // 한 번 더 누르기를 기다리던 것은 다른 나라에 대한 것이었다
            }
        }
        ImGui::EndListBox();
    }
    ImGui::Spacing();
}

// 설정의 최소 유지를 keeper 가 쓰는 꼴로: 국고는 백만 달러 → 달러.
Keep keep_of(const Settings &s)
{
    Keep keep;
    keep.treasury = s.keep_treasury;
    keep.treasury_floor = static_cast<double>(s.keep_treasury_value) * 1e6;
    for (int slot = 0; slot < STOCK_SLOTS; slot++) {
        keep.stock[slot] = s.keep_stock[slot];
        keep.stock_floor[slot] = static_cast<double>(s.keep_stock_value[slot]);
    }
    return keep;
}

// 창에서 고친 최소 유지를 keeper 에 넘긴다(save 면 설정 파일에도 적는다). 달라진 것이 없으면 아무것도 하지 않는다.
// 로그에는 켠 것 · 끈 것 · 켜진 채로 값이 바뀐 것을 적는다.
void send_keep(bool save)
{
    const Keep want = keep_of(g_settings);
    bool changed = want.treasury != g_keep_sent.treasury || want.treasury_floor != g_keep_sent.treasury_floor;
    if (want.treasury && (!g_keep_sent.treasury || want.treasury_floor != g_keep_sent.treasury_floor))
        log_line("유지 켬: 국고 %lld (백만 달러)", g_settings.keep_treasury_value);
    else if (!want.treasury && g_keep_sent.treasury)
        log_line("유지 끔: 국고");
    for (int slot = 0; slot < STOCK_SLOTS; slot++) {
        changed = changed || want.stock[slot] != g_keep_sent.stock[slot] || want.stock_floor[slot] != g_keep_sent.stock_floor[slot];
        if (want.stock[slot] && (!g_keep_sent.stock[slot] || want.stock_floor[slot] != g_keep_sent.stock_floor[slot]))
            log_line("유지 켬: %s %lld", product_label(slot).c_str(), g_settings.keep_stock_value[slot]);
        else if (!want.stock[slot] && g_keep_sent.stock[slot])
            log_line("유지 끔: %s", product_label(slot).c_str());
    }
    if (!changed)
        return;
    if (save)
        save_settings(g_settings);
    keeper_set_keep(want);
    g_keep_sent = want;
}

// 설정 파일이 받아들이는 범위와 창 · keeper 의 범위는 같아야 한다(두 헤더가 따로 적는다)
static_assert(SETTINGS_MULT_FAST_MAX == MULT_FAST_MAX && SETTINGS_MULT_BUILD_MAX == MULT_BUILD_MAX && SETTINGS_MULT_GOODS_MAX == MULT_GOODS_MAX
                  && SETTINGS_MULT_CELL_MIN == MULT_CELL_MIN && SETTINGS_MULT_RESEARCH_MIN == MULT_RESEARCH_MIN,
              "settings.h 와 keeper.h 의 배율 범위가 다르다");

Mult mult_of(const Settings &s)
{
    Mult m;
    m.on = s.mult_on;
    m.tax = s.mult_tax;
    m.defense = s.mult_defense;
    m.social = s.mult_social;
    m.research = s.mult_research;
    m.unitbuild = s.mult_unitbuild;
    m.facbuild = s.mult_facbuild;
    for (int i = 0; i < MULT_GOODS; i++) {
        m.out[i] = s.mult_out[i];
        m.use[i] = s.mult_use[i];
        m.raw[i] = s.mult_raw[i];
    }
    return m;
}

// 창에서 고친 배율을 keeper 에 넘긴다(save 면 설정 파일에도 적는다). 달라진 것이 없으면 아무것도 하지 않는다.
void send_mult(bool save)
{
    const Mult want = mult_of(g_settings);
    if (want == g_mult_sent)
        return;
    // 켜고 끌 때만 적는다 — 입력란을 누를 때마다 줄이 쌓였다(2026-10-19). 켜 둔 동안 바꾼 값은 keeper 의 "날마다의 일" 줄에 남는다
    if (want.on != g_mult_sent.on)
        log_line("배율 %s: 세금 %d%% · 국방비 %d%% · 사회 지출 %d%% · 연구 속도 %d%% · 유닛 생산 %d%% · 시설 건설 %d%% · 물자의 배율 %d개",
                 want.on ? "켬" : "끔", want.tax, want.defense, want.social, want.research, want.unitbuild, want.facbuild,
                 mult_count(want) - (want.tax != 100) - (want.defense != 100) - (want.social != 100) - (want.research != 100)
                     - (want.unitbuild != 100) - (want.facbuild != 100));
    if (save)
        save_settings(g_settings);
    keeper_set_mult(want);
    g_mult_sent = want;
}

// 배율의 퍼센트 입력란(low … high). 입력을 마쳤을 때(Enter, 다른 곳을 누름, + · − 단추)에만 값이 넘어온다.
// + · − 는 10 씩(Ctrl 을 누르면 100 씩). 끝이 MULT_BUILD_MAX 를 넘는 줄(국고 · 연구 — 끝 10000)은 100 씩(Ctrl 1000).
void mult_input(const char *id, const std::string &name, int *value, int low, int high)
{
    const int step = high > MULT_BUILD_MAX ? 100 : 10;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputInt(id, value, step, step * 10)) {
        *value = std::min(high, std::max(low, *value));
        send_mult(true);
    }
    note(name, std::to_string(*value));
}

// 상태 줄의 뒤에 붙는 글: 배율이 켜져 있고 100 이 아닌 것이 있으면 " · 배율 N개".
std::string mult_text()
{
    const int count = mult_count(g_mult_sent);
    return g_mult_sent.on && count > 0 ? " · 배율 " + std::to_string(count) + "개" : std::string();
}

// "다음 자정에 완료" 단추의 풍선말. 시설만이다 — 부대(칸 15)는 게임에서 1000배로는 끝나지 않았다(설계서의 "게임에서 본 것", 2026-10-15)
const char *const RUSH_HELP = "내 나라의 시설 건설을 다음 날의 셈 하루 동안 1000배 빠르게 한다 — 게임이 스스로 끝낸다. 저장하지 않는다.\n"
                              "그날 남은 건설비가 한꺼번에 국고에서 빠질 수 있다. 부대 생산에는 듣지 않는다 — 부대는 유닛 생산 속도로.";

// 유닛 생산 속도 줄의 풍선말(docs/superpowers/specs/2026-10-19-toybox-mult-unit-caps-design.md — 게임에서 ×5 · ×20 이 들었다)
const char *const UNIT_BUILD_HELP = "생산 중인 부대의 하루 진행에 곱한다(곱 보정 칸 15, 끝 2000 %). 생산비도 진행에 비례해 국고에서 빠진다. "
                                   "마지막 하루 이틀은 배율과 상관없이 걸릴 수 있다.";

// 줄을 바꾸는 풍선말 — SetTooltip 은 줄을 바꾸지 않아 1024 × 768 에서 화면 밖으로 잘린다(게임에서 봤다, 2026-10-15)
void wrapped_tooltip(const char *text)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// 배율 탭. 설정이다 — 게임 밖에서도 고칠 수 있다. 찾지 못한 묶음의 줄은 꺼져 있고 그 위에 까닭이 한 줄 있다.
void mult_tab(const GameState &game)
{
    if (ImGui::Checkbox("배율 적용", &g_settings.mult_on))
        send_mult(true);
    note("mult:on", g_settings.mult_on ? "1" : "0");
    ImGui::SameLine();
    ImGui::TextDisabled("퍼센트 — 100 이 그대로다. 플레이하는 나라에만 듣는다 (?)");
    // 줄을 아낀다 — 1024 × 768 에서 물자 표의 마지막 줄까지 보이게(건설 · 생산 줄이 한 줄 늘었다). 설명은 풍선말로
    if (ImGui::IsItemHovered())
        wrapped_tooltip("켜 두면 창을 닫아도, 새 판에서도 계속된다. 세금 · 국방비 · 사회 지출은 날이 바뀔 때 국고에 차이를 더하거나 뺀다(재무 패널의 숫자는 그대로다). "
                      "연구 속도는 날이 바뀔 때 진행 중인 연구의 남은 기간을 더 줄인다. 생산 · 소비와 유닛 생산 · 시설 건설 속도는 게임의 보정 값에 곱한다. "
                      "끝: 세금 · 국방비 · 사회 지출 · 연구 10000, 유닛 생산 · 시설 건설 2000, 물자 1000");
    if (!game_writes() && game.known) {
        ImGui::TextWrapped("값 쓰기가 꺼져 있어 배율이 쉽니다.");
        note("mult:off", "값 쓰기가 꺼져 있어 배율이 쉽니다.");
    }
    const std::string money = game_mult_off(MULT_PART_MONEY), speed = game_mult_off(MULT_PART_SPEED), cells = game_mult_off(MULT_PART_CELLS);
    const std::string social = game_mult_off(MULT_PART_SOCIAL);
    const auto why = [](const char *name, const char *what, const std::string &text) {
        if (text.empty())
            return;
        const std::string line = std::string(what) + "을(를) 쓸 수 없습니다: " + text;
        ImGui::TextWrapped("%s", line.c_str());
        note(name, line);
    };
    why("mult:money:off", "세금 · 국방비의 배율", money);
    why("mult:social:off", "사회 지출의 배율", social);
    why("mult:speed:off", "연구 속도의 배율", speed);
    ImGui::BeginDisabled(!money.empty());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("세금");
    ImGui::SameLine();
    mult_input("##tax", "mult:tax", &g_settings.mult_tax, 0, MULT_FAST_MAX);
    ImGui::SameLine();
    ImGui::TextUnformatted("국방비");
    ImGui::SameLine();
    mult_input("##defense", "mult:defense", &g_settings.mult_defense, 0, MULT_FAST_MAX);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!social.empty());
    ImGui::SameLine();
    ImGui::TextUnformatted("사회 지출");
    ImGui::SameLine();
    mult_input("##social", "mult:social", &g_settings.mult_social, 0, MULT_FAST_MAX);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!speed.empty());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("연구 속도");
    ImGui::SameLine();
    mult_input("##research", "mult:research", &g_settings.mult_research, MULT_RESEARCH_MIN, MULT_FAST_MAX);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!cells.empty());
    ImGui::SameLine();
    ImGui::TextUnformatted("유닛 생산 속도");
    if (ImGui::IsItemHovered())
        wrapped_tooltip(UNIT_BUILD_HELP);
    ImGui::SameLine();
    mult_input("##unitbuild", "mult:unitbuild", &g_settings.mult_unitbuild, MULT_CELL_MIN, MULT_BUILD_MAX);
    ImGui::EndDisabled();
    const MultStats stats = keeper_mult_stats();
    const std::string did = "이번 실행: 날마다의 일 " + std::to_string(stats.days) + "일 · 보정 칸 쓰기 " + std::to_string(stats.writes) + "번 · 지금 "
        + std::to_string(stats.held) + "칸";
    ImGui::SameLine();
    ImGui::TextDisabled("%llu일 · %d칸 (?)", stats.days, stats.held);   // 줄을 아낀다 — 1024 × 768 에서 유닛 생산 줄과 한 줄에. 자세한 것은 풍선말로
    if (ImGui::IsItemHovered())
        wrapped_tooltip(did.c_str());
    note("mult:stats", did);
    ImGui::BeginDisabled(!cells.empty());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("시설 건설 속도");
    ImGui::SameLine();
    mult_input("##facbuild", "mult:facbuild", &g_settings.mult_facbuild, MULT_CELL_MIN, MULT_BUILD_MAX);
    ImGui::SameLine();
    const bool pending = keeper_rush_pending();
    ImGui::BeginDisabled(pending);
    if (ImGui::Button(pending ? "다음 자정에 완료 — 대기 중" : "시설 건설을 다음 자정에 완료"))
        g_notice = keeper_rush();
    // 줄을 아낀다 — 1024 × 768 에서 물자 표의 마지막 줄까지 보이게. 설명은 단추의 풍선말로
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        wrapped_tooltip(RUSH_HELP);
    ImGui::EndDisabled();
    note("mult:rush", pending ? "1" : "0");
    ImGui::EndDisabled();
    why("mult:cells:off", "물자의 배율", cells);   // 줄을 아낀다 — 1024 × 768 에서 물자 열하나가 구르지 않고 보이게
    GameValues now;
    if (game.known && game.in_game && !game.multiplayer && game_values_off().empty())
        now = game_values();
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("mult", 4, flags))
        return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("물자");
    ImGui::TableSetupColumn("생산 %");
    ImGui::TableSetupColumn("인구 소비 %");
    ImGui::TableSetupColumn("산업 소비 %");
    ImGui::TableHeadersRow();
    ImGui::BeginDisabled(!cells.empty());
    for (int slot = 0; slot < MULT_GOODS; slot++) {
        const bool unused = now.ok && !now.used[slot];
        const std::string name = "mult:" + std::to_string(slot), label = product_label(slot);
        ImGui::PushID(slot);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (unused)
            ImGui::TextDisabled("%s", label.c_str());   // 이번 판에서 쓰지 않는 물자 — 설정은 고칠 수 있다
        else
            ImGui::TextUnformatted(label.c_str());
        note(name, label);
        ImGui::TableNextColumn();
        mult_input("##out", name + ":out", &g_settings.mult_out[slot], MULT_CELL_MIN, MULT_GOODS_MAX);
        ImGui::TableNextColumn();
        mult_input("##use", name + ":use", &g_settings.mult_use[slot], MULT_CELL_MIN, MULT_GOODS_MAX);
        ImGui::TableNextColumn();
        mult_input("##raw", name + ":raw", &g_settings.mult_raw[slot], MULT_CELL_MIN, MULT_GOODS_MAX);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
}

// ---- 예비 인원(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md) ----

static_assert(RESERVE_LIMIT == RESERVE_KEEP_MAX && static_cast<float>(RESERVE_LIMIT) == RESERVE_MAX, "예비 인원의 끝이 세 곳에서 다르다");

const char *const PEOPLE_TAB = "인구·여론";       // features.cpp 의 탭 이름과 같아야 한다
bool g_reserve_sent_on = false;
long long g_reserve_sent_value = -1;

// 창의 예비 최소 유지를 keeper 에 넘긴다(save 면 설정 파일에도). 달라진 것이 없으면 아무것도 하지 않는다.
void send_reserve(bool save)
{
    if (g_settings.keep_reserve == g_reserve_sent_on && g_settings.keep_reserve_value == g_reserve_sent_value)
        return;
    if (g_settings.keep_reserve != g_reserve_sent_on && (g_settings.keep_reserve || g_reserve_sent_value >= 0))
        log_line("유지 %s: 예비 %lld (명)", g_settings.keep_reserve ? "켬" : "끔", g_settings.keep_reserve_value);
    if (save)
        save_settings(g_settings);
    keeper_set_reserve_keep(g_settings.keep_reserve, g_settings.keep_reserve_value);
    g_reserve_sent_on = g_settings.keep_reserve;
    g_reserve_sent_value = g_settings.keep_reserve_value;
}

// 상태 줄의 뒤에 붙는 글: 예비 최소 유지가 켜져 있으면 " · 유지 중: 예비".
std::string reserve_text()
{
    return g_reserve_sent_on ? std::string(" · 유지 중: 예비") : std::string();
}

void reserve_button(const char *label, const char *name, double amount)
{
    const bool pressed = ImGui::Button(label);
    note(name, label);
    if (pressed)
        g_notice = keeper_enqueue({RESERVE, Change::Add, amount}) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
}

const char *const RESERVE_HELP = "지휘 패널의 \"예비 인원\" — 유닛 생산과 부대의 병력 보충이 여기서 사람을 가져간다. \"인구 +100만\"은 이 숫자를 늘리지 않는다. "
                                 "더한 값은 큰 나라에서는 남지만, 인구에 비해 너무 크면 게임이 자정에 깎고 그 뒤로 하루 1 % 씩 줄인다(작은 나라에서 봤다). "
                                 "최소 유지는 0.5초마다, 예비가 정한 값보다 적으면 그 값으로 다시 올린다(저장되는 설정)";

// 인구·여론 탭의 아래: 예비 인원의 단추와 최소 유지.
void reserve_rows(const GameState &game)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("예비 인원");
    ImGui::SameLine();
    float now = 0;
    const std::string off = game.known ? game_mult_off(MULT_PART_RESERVE) : std::string("게임 상태를 읽을 수 있을 때만 씁니다.");
    if (off.empty() && game.in_game && !game.multiplayer && game_reserve(&now) && std::isfinite(now))
        ImGui::TextDisabled("지금 %s (?)", short_amount(now).c_str());
    else
        ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        wrapped_tooltip(RESERVE_HELP);
    if (!off.empty()) {
        const std::string line = "예비 인원을 쓸 수 없습니다: " + off;
        ImGui::TextWrapped("%s", line.c_str());
        note("reserve:off", line);
        return;
    }
    const bool playing = game.known && game.in_game && !game.multiplayer;
    ImGui::BeginDisabled(!playing);
    reserve_button("+10만", "reserve:+100K", 1e5);
    ImGui::SameLine();
    reserve_button("+100만", "reserve:+1M", 1e6);
    ImGui::SameLine();
    reserve_button("+1000만", "reserve:+10M", 1e7);
    ImGui::SameLine();
    const long long step = 100000, fast = 1000000;
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::InputScalar("##reserveadd", ImGuiDataType_S64, &g_settings.reserve_amount, &step, &fast)) {
        g_settings.reserve_amount = std::min(RESERVE_LIMIT, std::max(1LL, g_settings.reserve_amount));
        save_settings(g_settings);
    }
    note("reserve:amount", std::to_string(g_settings.reserve_amount));
    ImGui::SameLine();
    reserve_button("더하기", "reserve:add", static_cast<double>(g_settings.reserve_amount));
    ImGui::EndDisabled();
    // 최소 유지는 설정이다 — 게임 밖에서도 고칠 수 있다
    if (ImGui::Checkbox("최소 유지##reserve", &g_settings.keep_reserve))
        send_reserve(true);
    note("reserve:keep", g_settings.keep_reserve ? "1" : "0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::InputScalar("##reservekeep", ImGuiDataType_S64, &g_settings.keep_reserve_value, &step, &fast)) {
        g_settings.keep_reserve_value = std::min(RESERVE_LIMIT, std::max(0LL, g_settings.keep_reserve_value));
        send_reserve(true);
    }
    note("reserve:keep:value", std::to_string(g_settings.keep_reserve_value));
    ImGui::SameLine();
    ImGui::TextDisabled("명");
}

// ---- 충성 · 지지(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md) ----

const char *const LOYALTY_ASK = "loyalty:run";      // g_confirm: "점령지 충성"이 둘째 누름을 기다린다
const char *const LOYALTY_HELP = "전투 · 병합으로 얻은 내 땅 가운데 아직 다른 나라에 충성하는 칸의 충성 대상을 내 나라(본토 칸에 가장 많은 충성 대상)로 바꾼다. "
                                 "남의 나라 땅과 이미 내 편인 땅은 그대로다. 되돌릴 수 없다";

// 인구·여론 탭의 아래: 충성 · 지지 묶음.
void loyalty_rows(const GameState &game)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("충성 · 지지");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        wrapped_tooltip(LOYALTY_HELP);
    const std::string off = game.known ? game_loyalty_off() : std::string("게임 상태를 읽을 수 있을 때만 씁니다.");
    if (!off.empty()) {
        const std::string line = "점령지 충성을 쓸 수 없습니다: " + off;
        ImGui::TextWrapped("%s", line.c_str());
        note("loyalty:off", line);
        return;
    }
    const bool playing = game.known && game.in_game && !game.multiplayer;
    ImGui::BeginDisabled(!playing);
    const bool asking = g_confirm == LOYALTY_ASK;
    const std::string text = asking ? std::string("점령지 충성 — 한 번 더 누르면 실행합니다") : std::string("점령지 충성");
    if (ImGui::Button((text + "###loyalty").c_str())) {
        if (asking) {
            g_confirm.clear();
            g_notice = keeper_enqueue({LOYALTY, Change::Add, 0}) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
        } else {
            g_confirm = LOYALTY_ASK;
        }
    }
    note("loyalty:run", text);
    ImGui::EndDisabled();
}

// ---- 사회: 품질 등급의 배율과 최소 유지(docs/superpowers/specs/2026-10-21-toybox-social-grades-design.md) ----

static_assert(SETTINGS_SOCIAL_RATE_MAX == SOCIAL_RATE_MAX && SETTINGS_SOCIAL_KEEP_MAX == SOCIAL_KEEP_MAX,
              "settings.h 와 keeper.h 의 사회 범위가 다르다");

const char *const SOCIAL_LABELS[SOCIAL_COUNT] = {"보건", "교육", "인프라", "환경", "가족 수당", "치안", "문화 지원", "사회 보장"};
Social g_social_sent;          // keeper 에 마지막으로 넘긴 사회 설정
int g_social_all_up = 100, g_social_all_down = 100, g_social_all_keep = 0;   // "모든 항목" 줄의 입력란(고치면 여덟 줄에 넣는다)

Social social_of(const Settings &s)
{
    Social m;
    m.on = s.social_on;
    for (int i = 0; i < SOCIAL_COUNT; i++) {
        m.up[i] = s.social_up[i];
        m.down[i] = s.social_down[i];
        m.keep[i] = s.social_keep[i];
        m.keep_value[i] = s.social_keep_value[i];
    }
    return m;
}

// 창에서 고친 사회 설정을 keeper 에 넘긴다(save 면 설정 파일에도). 달라진 것이 없으면 아무것도 하지 않는다. 로그는 스위치를 켜고 끌 때만.
void send_social(bool save)
{
    const Social want = social_of(g_settings);
    if (want == g_social_sent)
        return;
    if (want.on != g_social_sent.on)
        log_line("사회 배율 %s (100 %% 가 아닌 배율과 켜진 유지 %d개)", want.on ? "켬" : "끔", social_count(want));
    if (save)
        save_settings(g_settings);
    keeper_set_social(want);
    g_social_sent = want;
}

// 상태 줄의 뒤에 붙는 글: 켜 둔 사회의 배율 · 유지가 있으면 " · 사회 N개".
std::string social_text()
{
    int count = 0;
    for (int i = 0; i < SOCIAL_COUNT; i++)
        count += (g_social_sent.on ? (g_social_sent.up[i] != 100) + (g_social_sent.down[i] != 100) : 0) + (g_social_sent.keep[i] ? 1 : 0);
    return count > 0 ? " · 사회 " + std::to_string(count) + "개" : std::string();
}

// 사회 탭의 퍼센트 입력란(low … high). 입력을 마쳤을 때에만 값이 넘어온다. 고쳤으면 참.
bool social_input(const char *id, const std::string &name, int *value, int low, int high)
{
    const int step = high > MULT_BUILD_MAX ? 100 : 10;
    ImGui::SetNextItemWidth(110.0f);
    const bool changed = ImGui::InputInt(id, value, step, step * 10);
    if (changed)
        *value = std::min(high, std::max(low, *value));
    note(name, std::to_string(*value));
    return changed;
}

const char *const SOCIAL_HELP = "품질 등급은 게임의 사회 지출 화면에서 항목을 누르면 나오는 \"품질 등급\"이다. 게임은 매일 자정, 등급을 목표(지출 수준 × 기술) 쪽으로 "
                                "남은 거리의 60분의 1 씩 움직인다. 오름 · 내림 배율은 그 하루 움직임을 키우거나 줄인다(0 % 면 그쪽으로 움직이지 않는다). "
                                "게임의 끝은 200 % 다 — 배율은 그 위로 올리지 않는다. 최소 유지는 스위치와 상관없이 0.5초마다, 등급이 정한 값보다 낮으면 올린다. "
                                "200 % 를 넘긴 값은 게임이 자정마다 200 % 로 자르고 ToyBox 가 다시 올린다. 저장되는 설정이다";

// 사회 탭. 설정이다 — 게임 밖에서도 고칠 수 있다.
void social_tab(const GameState &game)
{
    if (ImGui::Checkbox("등급 배율 적용", &g_settings.social_on))
        send_social(true);
    note("social:on", g_settings.social_on ? "1" : "0");
    ImGui::SameLine();
    ImGui::TextDisabled("퍼센트 — 100 이 그대로다. 플레이하는 나라에만 듣는다 (?)");
    if (ImGui::IsItemHovered())
        wrapped_tooltip(SOCIAL_HELP);
    if (!game_writes() && game.known) {
        ImGui::TextWrapped("값 쓰기가 꺼져 있어 쉽니다.");
        note("social:off", "값 쓰기가 꺼져 있어 쉽니다.");
    }
    const std::string off = game_mult_off(MULT_PART_GRADE);
    if (!off.empty()) {
        const std::string line = "품질 등급을 쓸 수 없습니다: " + off;
        ImGui::TextWrapped("%s", line.c_str());
        note("social:grade:off", line);
    }
    Grades now;
    if (off.empty() && game.known && game.in_game && !game.multiplayer)
        now = game_social_grades();
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("social", 6, flags))
        return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("항목");
    ImGui::TableSetupColumn("지금 등급");
    ImGui::TableSetupColumn("오름 %");
    ImGui::TableSetupColumn("내림 %");
    ImGui::TableSetupColumn("유지");
    ImGui::TableSetupColumn("유지 값 %");
    ImGui::TableHeadersRow();
    ImGui::BeginDisabled(!off.empty());
    bool changed = false;
    {                                                        // 모든 항목: 고친 값을 여덟 줄에 함께 넣는다
        ImGui::PushID(-1);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("모든 항목");
        note("social:all", "모든 항목");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (social_input("##up", "social:all:up", &g_social_all_up, 0, SOCIAL_RATE_MAX)) {
            for (int i = 0; i < SOCIAL_COUNT; i++)
                g_settings.social_up[i] = g_social_all_up;
            changed = true;
        }
        ImGui::TableNextColumn();
        if (social_input("##down", "social:all:down", &g_social_all_down, 0, SOCIAL_RATE_MAX)) {
            for (int i = 0; i < SOCIAL_COUNT; i++)
                g_settings.social_down[i] = g_social_all_down;
            changed = true;
        }
        ImGui::TableNextColumn();
        bool all = true;
        for (int i = 0; i < SOCIAL_COUNT; i++)
            all = all && g_settings.social_keep[i];
        if (ImGui::Checkbox("##keep", &all)) {
            for (int i = 0; i < SOCIAL_COUNT; i++)
                g_settings.social_keep[i] = all;
            changed = true;
        }
        ImGui::TableNextColumn();
        if (social_input("##keepv", "social:all:keepv", &g_social_all_keep, 0, SOCIAL_KEEP_MAX)) {
            for (int i = 0; i < SOCIAL_COUNT; i++)
                g_settings.social_keep_value[i] = g_social_all_keep;
            changed = true;
        }
        ImGui::PopID();
    }
    for (int i = 0; i < SOCIAL_COUNT; i++) {
        const std::string name = "social:" + std::to_string(i);
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(SOCIAL_LABELS[i]);
        note(name, SOCIAL_LABELS[i]);
        ImGui::TableNextColumn();
        char grade[32] = "-";
        if (now.ok && std::isfinite(now.grade[i]))
            snprintf(grade, sizeof(grade), "%.1f %%", now.grade[i] * 100.0f);
        ImGui::TextUnformatted(grade);
        note(name + ":now", grade);
        ImGui::TableNextColumn();
        changed |= social_input("##up", name + ":up", &g_settings.social_up[i], 0, SOCIAL_RATE_MAX);
        ImGui::TableNextColumn();
        changed |= social_input("##down", name + ":down", &g_settings.social_down[i], 0, SOCIAL_RATE_MAX);
        ImGui::TableNextColumn();
        changed |= ImGui::Checkbox("##keep", &g_settings.social_keep[i]);
        note(name + ":keep", g_settings.social_keep[i] ? "1" : "0");
        ImGui::TableNextColumn();
        changed |= social_input("##keepv", name + ":keepv", &g_settings.social_keep_value[i], 0, SOCIAL_KEEP_MAX);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
    if (changed)
        send_social(true);
}

// 최소 유지의 수량 입력란. 치는 동안의 값(5, 50, 500 …)은 쓰이지 않는다 — ImGui 의 InputScalar 는 입력을 마쳤을 때
// (Enter, 다른 곳을 누름)에만 값을 넘겨준다. 치던 채로 창을 닫으면 친 것은 버려진다. 누르면 지금 값이 통째로 골라진다.
// + · − 단추의 걸음은 끝의 1000분의 1(국고 10억 달러 · 물자 100만), Ctrl 을 누르면 그 10배(2026-10-19 — 단추가 없어 숫자를 쳐야만 했다).
void keep_input(const char *id, long long *value, long long high, float width)
{
    const long long step = std::max(1LL, high / 1000), fast = step * 10;
    ImGui::SetNextItemWidth(width + 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x));
    if (ImGui::InputScalar(id, ImGuiDataType_S64, value, &step, &fast)) {
        *value = std::min(high, std::max(0LL, *value));
        send_keep(true);
    }
}

// 상태 줄의 뒤에 붙는 글: 최소 유지가 지금 무엇을 하고 있는가. 켜진 것이 없으면 빈 글.
// 까닭을 고르는 순서는 유지 검사(keeper_tick)가 쉬는 순서와 같다 — 값을 쓸 수 없으면 게임 밖에서도 "게임에 들어가면 적용"이라고 하지 않는다.
std::string keep_text(const GameState &game)
{
    if (keep_count(g_keep_sent) == 0)
        return std::string();
    if (!game.known)
        return keep_resting_text(g_keep_sent, "게임을 읽을 수 없어 쉽니다");
    if (!game_values_off().empty())
        return keep_resting_text(g_keep_sent, "값을 쓸 수 없어 쉽니다");
    if (game.multiplayer)
        return keep_resting_text(g_keep_sent, "멀티플레이에서는 쉽니다");
    if (!game.in_game)
        return keep_resting_text(g_keep_sent, "게임에 들어가면 적용");
    const GameValues now = game_values();
    return now.ok ? keep_active_text(g_keep_sent, now.used) : keep_resting_text(g_keep_sent, "값을 읽을 수 없어 쉽니다");
}

// 돈 탭의 단추 하나: 누르면 국고에 대한 요청을 대기열에 넣는다. 쓰는 것은 게임 창의 타이머에서다(keeper.h).
void money_button(const char *label, const char *name, Change change, double amount)
{
    const bool pressed = ImGui::Button(label);
    note(name, label);
    if (pressed)
        g_notice = keeper_enqueue({TREASURY, change, amount}) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
}

// 값을 쓰는 탭(돈 · 물자)의 머리. 쓸 수 없으면 까닭 한 줄만 그리고(name 으로 적는다) false — 내장 치트로 되돌아가지 않는다.
// 쓸 수 있으면 true 와 *now(탭이 보이는 동안 프레임마다 읽는다. 게임 밖이면 ok 가 거짓인 빈 값).
bool values_ready(const GameState &game, const char *name, GameValues *now)
{
    std::string off = game.known ? game_values_off() : std::string("게임 상태를 읽을 수 있을 때만 씁니다.");
    if (off.empty()) {
        *now = game.in_game ? game_values() : GameValues();
        if (game.in_game && !now->ok)   // 게임 안인데 값을 믿을 수 없다(재고 칸이 수가 아니다, 세계 자료를 읽지 못했다) — 까닭 없이 꺼 두지 않는다
            off = "게임의 값을 읽을 수 없어 쓸 수 없습니다.";
    }
    if (off.empty())
        return true;
    ImGui::TextWrapped("%s", off.c_str());
    note(name, off);
    return false;
}

// 돈 탭: 내장 치트를 거치지 않고 플레이어의 국고를 직접 고친다(기능 표가 아니라 여기서 그린다). 쓸 수 있으면 true.
bool money_tab(const GameState &game, bool blocked)
{
    GameValues now;
    if (!values_ready(game, "money:off", &now))
        return false;
    const std::string have = now.ok ? "국고: $ " + short_number(now.treasury) : std::string("국고: -");
    ImGui::TextUnformatted(have.c_str());
    note("money:now", have);
    ImGui::Spacing();

    ImGui::BeginDisabled(blocked || !now.ok);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputScalar("##amount", ImGuiDataType_S64, &g_settings.money_amount)) {
        g_settings.money_amount = std::min(MONEY_AMOUNT_MAX, std::max(MONEY_AMOUNT_MIN, g_settings.money_amount));
        save_settings(g_settings);
    }
    note("money:amount", std::to_string(g_settings.money_amount));
    const double amount = static_cast<double>(g_settings.money_amount) * 1e6;
    ImGui::SameLine();
    ImGui::TextUnformatted("백만 달러");
    ImGui::SameLine();
    money_button("더하기", "money:add", Change::Add, amount);
    ImGui::SameLine();
    money_button("빼기", "money:sub", Change::Add, -amount);
    ImGui::SameLine();
    money_button("이 값으로", "money:set", Change::Set, amount);
    ImGui::TextDisabled("입력한 금액만큼 국고를 더하거나 빼거나, 국고를 그 금액으로 맞춘다");
    ImGui::Spacing();

    money_button("-$100 B", "money:-100b", Change::Add, -100e9);
    ImGui::SameLine();
    money_button("-$10 B", "money:-10b", Change::Add, -10e9);
    ImGui::SameLine();
    money_button("$0", "money:zero", Change::Set, 0.0);
    ImGui::SameLine();
    money_button("+$10 B", "money:+10b", Change::Add, 10e9);
    ImGui::SameLine();
    money_button("+$100 B", "money:+100b", Change::Add, 100e9);
    ImGui::TextDisabled("국고는 음수가 될 수 있다. 다른 나라의 국고는 건드리지 않는다");
    ImGui::EndDisabled();
    ImGui::Spacing();

    // 최소 유지는 설정이다 — 게임 밖에서도 켜고 끌 수 있다(쓰는 것은 게임 안에서만이다)
    if (ImGui::Checkbox("최소 유지", &g_settings.keep_treasury))
        send_keep(true);
    note("money:keep", g_settings.keep_treasury ? "1" : "0");
    ImGui::SameLine();
    keep_input("##keepvalue", &g_settings.keep_treasury_value, KEEP_TREASURY_MAX, 150.0f);
    note("money:keepvalue", std::to_string(g_settings.keep_treasury_value));
    ImGui::SameLine();
    ImGui::TextUnformatted("백만 달러");
    help("help:money", "켜 두면 국고가 이 금액보다 적어질 때 이 금액으로 올린다(0.5초마다). 창을 닫아도, 새 판에서도 계속된다");
    return true;
}

// 물자 탭의 한 줄에 놓이는 단추 다섯: 그 칸(또는 ALL_STOCK)의 재고에서 빼고, 0 으로 만들고, 더한다.
void stock_buttons(const std::string &name, int slot)
{
    static const struct {
        const char *label, *id;
        Change change;
        double amount;
    } BUTTONS[] = {
        {"-1억", ":-100m", Change::Add, -1e8}, {"-100만", ":-1m", Change::Add, -1e6}, {"0", ":zero", Change::Set, 0.0},
        {"+100만", ":+1m", Change::Add, 1e6}, {"+1억", ":+100m", Change::Add, 1e8},
    };
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); i++) {
        if (i > 0)
            ImGui::SameLine();
        const bool pressed = ImGui::Button(BUTTONS[i].label);
        note(name + BUTTONS[i].id, BUTTONS[i].label);
        if (pressed)
            g_notice = keeper_enqueue({slot, BUTTONS[i].change, BUTTONS[i].amount}) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
    }
}

// 물자 탭의 한 줄. slot 이 ALL_STOCK 이면 "모든 물자" 줄이다(재고 표시와 최소 유지가 없다).
// 이번 판에서 쓰지 않는 물자의 줄(최소 유지가 켜져 있어 남은 것)은 단추가 꺼져 있다.
void stock_row(int slot, const GameValues &now, bool blocked)
{
    const bool all = slot == ALL_STOCK;
    const bool unused = !all && now.ok && !now.used[slot];
    const std::string name = all ? std::string("stock:all") : "stock:" + std::to_string(slot);
    const std::string label = all ? std::string("모든 물자") : product_label(slot);
    const std::string have = all || !now.ok ? std::string("-") : unused ? std::string("쓰지 않음") : short_amount(now.stock[slot]);
    ImGui::PushID(slot);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    note(name, label);
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(have.c_str());
    note(name + ":now", have);
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(blocked || !now.ok || unused);
    stock_buttons(name, slot);
    ImGui::EndDisabled();
    ImGui::TableNextColumn();
    if (!all) {                                  // 최소 유지는 설정이다 — 단추가 꺼져 있을 때도 고칠 수 있다
        if (ImGui::Checkbox("##keep", &g_settings.keep_stock[slot]))
            send_keep(true);
        note(name + ":keep", g_settings.keep_stock[slot] ? "1" : "0");
        ImGui::SameLine();
        keep_input("##keepvalue", &g_settings.keep_stock_value[slot], KEEP_STOCK_MAX, 120.0f);
        note(name + ":keepvalue", std::to_string(g_settings.keep_stock_value[slot]));
    }
    ImGui::PopID();
}

// 물자 탭: 내장 치트를 거치지 않고 플레이어의 물자 재고를 직접 고친다. 물자마다 한 줄 —
// 게임 안에서는 이번 판에서 쓰는 물자만, 게임 밖에서는 이름표에 있는 물자가 나온다(단추는 꺼져 있다).
// 최소 유지가 켜진 물자는 어느 쪽에서든 줄이 남는다(끌 수 있게). 쓸 수 있으면 true.
bool stock_tab(const GameState &game, bool blocked)
{
    GameValues now;
    if (!values_ready(game, "stock:off", &now))
        return false;
    ImGui::TextDisabled("재고는 0 아래로 내려가지 않는다. 다른 나라의 재고는 건드리지 않는다");
    help("help:stock", "최소 유지를 켜 두면 재고가 그 수량보다 적어질 때 그 수량으로 올린다(0.5초마다). 창을 닫아도, 새 판에서도 계속된다");
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;   // 창이 좁으면 표가 가로로 구른다
    if (!ImGui::BeginTable("stock", 4, flags))
        return true;
    ImGui::TableSetupScrollFreeze(1, 1);                       // 이름 칸과 머리 줄은 굴려도 남는다
    ImGui::TableSetupColumn("물자");
    ImGui::TableSetupColumn("재고");
    ImGui::TableSetupColumn("빼기 / 더하기");
    ImGui::TableSetupColumn("최소 유지");
    ImGui::TableHeadersRow();
    stock_row(ALL_STOCK, now, blocked);
    for (int slot = 0; slot < STOCK_SLOTS; slot++)
        if ((now.ok ? now.used[slot] : find_product(slot) != nullptr) || g_settings.keep_stock[slot])
            stock_row(slot, now, blocked);
    ImGui::EndTable();
    return true;
}

// 연구 목록의 단추 하나: 그 번호들을 완료 · 미완료로 바꾸는 요청을 넣는다. 쓰는 것은 게임 창의 타이머에서다(keeper.h).
void list_request(const ListState &L, Research action, const std::vector<int> &ids, const char *label)
{
    ResearchRequest request = {action, ResearchWhat(), label};
    (L.list.designs ? request.what.designs : request.what.techs) = ids;
    g_confirm.clear();
    g_notice = keeper_enqueue_research(request) ? "" : "대기 중인 요청이 많아 받지 못했습니다.";
}

bool same(const ListFilter &a, const ListFilter &b)
{
    return a.designs == b.designs && a.show == b.show && a.kind == b.kind && a.find == b.find && a.by_picked == b.by_picked
        && a.domain == b.domain;
}

// 목록의 "보유국" 칸: 보유한 다른 나라의 이름. 둘까지 적고 더 있으면 "폴란드, 덴마크 외 3". 없으면 빈 글.
std::string owners_label(const ListRow &row, const std::vector<int> &regions)
{
    std::string out;
    int named = 0;
    for (int index : row.owners)
        if (index > 0 && named < row.others) {
            const int number = static_cast<size_t>(index) < regions.size() ? regions[static_cast<size_t>(index)] : 0;
            out += (named++ > 0 ? ", " : "") + (number > 0 ? region_label(number) : "#" + std::to_string(index));
        }
    return row.others > named ? out + " 외 " + std::to_string(row.others - named) : out;
}

// 쉼표로 이은 이름들에서 앞의 limit 개만 남긴다("… 외 272") — 보유국이 수백이면 줄을 바꿔도 풍선말이 창보다 길다
std::string first_names(const std::string &names, int limit)
{
    size_t cut = std::string::npos;
    int count = names.empty() ? 0 : 1;
    for (size_t i = 0; i < names.size(); i++)
        if (names[i] == ',' && count++ == limit)
            cut = i;
    return cut == std::string::npos ? names : names.substr(0, cut) + " … 외 " + std::to_string(count - limit);
}

// 긴 글의 풍선말 — 창 밖으로 나가지 않게 줄을 바꾼다(보유국이 수백이다)
void wrapped_tip(const std::string &text)
{
    if (!ImGui::BeginTooltip())
        return;
    ImGui::PushTextWrapPos(32.0f * ImGui::GetFontSize());
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

int g_stat_which = STAT_EXPERIENCE, g_stat_percent = 100;   // 부대의 상태: 무엇을 · 몇 퍼센트로

// 부대 탭의 "내 부대" 줄 둘: 내 부대 전부의 보급, 그리고 부대의 상태(경험 · 사기 · 효율)를 고른 부대에 / 내 부대 전부에.
// 내 부대 = 세계의 부대 목록에서 주인이 플레이어이고 시설이 아닌 것(예비 부대도 든다). 쓸 수 없으면 그 자리에 까닭을 적는다.
void army_rows()
{
    static const char *const STATS[STAT_COUNT] = {"경험", "사기", "효율"};
    const std::string busy = "대기 중인 요청이 많아 받지 못했습니다.";
    const std::string supply_off = game_act_off(ACT_SUPPLY_ALL), all_off = game_act_off(ACT_UNITS), picked_off = game_act_off(ACT_PICKED_UNITS);
    ImGui::PushID("army");
    if (!supply_off.empty()) {
        const std::string line = "내 부대 전부 보급 채우기 — " + supply_off;
        ImGui::TextWrapped("%s", line.c_str());
        note("off:units:fill_all", line);
    } else {
        if (ImGui::Button("내 부대 전부 보급 채우기")) {
            g_confirm.clear();
            g_notice = keeper_enqueue_supply(true, true) ? "" : busy;
        }
        note("units:fill_all", "내 부대 전부 보급 채우기");
        ImGui::SameLine();
        ImGui::TextDisabled("배치 · 예비의 내 부대 모두(시설은 뺀다)");
    }
    if (!all_off.empty() && !picked_off.empty()) {
        const std::string line = "부대의 상태 — " + picked_off;
        ImGui::TextWrapped("%s", line.c_str());
        note("off:units:stat", line);
    } else {
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::BeginCombo("##stat", STATS[g_stat_which])) {
            for (int i = 0; i < STAT_COUNT; i++) {
                if (ImGui::Selectable(STATS[i], i == g_stat_which))
                    g_stat_which = i;
                note("units:stat:" + std::to_string(i), STATS[i]);
            }
            ImGui::EndCombo();
        }
        note("units:stat", STATS[g_stat_which]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        // 빠른 걸음(Ctrl 을 누른 채의 + · −)을 10 으로: ImGui 의 기본은 100 이라 한 번에 0 ↔ 100 을 오간다(게임에서 봤다 — 단축키의 Ctrl 이 남아 있을 때)
        if (ImGui::InputInt("%##percent", &g_stat_percent, 1, 10))
            g_stat_percent = g_stat_percent < 0 ? 0 : g_stat_percent > 100 ? 100 : g_stat_percent;
        note("units:percent", std::to_string(g_stat_percent));
        ImGui::SameLine();
        ImGui::BeginDisabled(!picked_off.empty());
        if (ImGui::Button("고른 부대에")) {
            g_confirm.clear();
            g_notice = keeper_enqueue_unit_stat(g_stat_which, g_stat_percent, false) ? "" : busy;
        }
        note("units:stat_picked", "고른 부대에");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!all_off.empty());
        if (ImGui::Button("내 부대 전부에")) {
            g_confirm.clear();
            g_notice = keeper_enqueue_unit_stat(g_stat_which, g_stat_percent, true) ? "" : busy;
        }
        note("units:stat_all", "내 부대 전부에");
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

// 목록을 머리 줄에서 고른 열로 놓는다. 같은 값끼리는 앞의 순서(수준 · 번호)가 남는다.
void sort_rows(ListState &L, int column, bool down)
{
    const auto less = [column](const ListRow &a, const ListRow &b) {
        switch (column) {
        case 1: return a.name < b.name;
        case 2: return a.kind < b.kind;
        case 4: return a.state < b.state;
        case 5: return a.needs - a.needs_mine < b.needs - b.needs_mine;   // 선행 기술: 모자란 것이 적은 순서
        case 6: return a.others < b.others;
        case 7: return a.picked < b.picked;
        default: return a.level < b.level;
        }
    };
    std::stable_sort(L.rows.begin(), L.rows.end(), [&](const ListRow &a, const ListRow &b) { return down ? less(b, a) : less(a, b); });
}

// 연구 탭의 목록: 기술이나 부대 설계를 거르개로 걸러 보이고, 고른 것 · 보이는 것 전부를 완료 · 미완료로 바꾼다.
// 표는 keeper 가 창 스레드에서 뜬 스냅숏이다(탭이 보이는 동안 0.5초마다). 게임 밖 · 멀티플레이에서는 그리지 않는다(상태 줄이 말한다).
// units: 부대 탭의 설계도 목록이다 — 부대 설계만 보이고, 영역(육 · 공 · 해 · 미사일)으로 거르고, 단추의 이름이 "등록 · 해제"이며
// "모든 설계도"의 단추 둘이 더 있다. 하는 일(완료 · 미완료의 규칙)은 연구 탭의 목록과 같다 — 등록하면 선행 기술도 함께 보유가 된다(사용자가 정했다).
void item_list(ListState &L, bool units, const GameState &game, bool blocked)
{
    static const char *const SHOWS[] = {"전체", "자국 보유", "자국 미보유", "타국만 보유", "고른 나라의 보유", "고른 나라에서 가져올 것", "연구 가능", "선행 부족"};
    if (!game.known || blocked) {
        L.chosen.clear();                        // 게임에서 나가면 고른 것은 풀린다
        return;
    }
    ImGui::Separator();
    keeper_watch_research(g_picked);
    const std::shared_ptr<const ResearchShot> shot = keeper_research_shot();
    if (shot == nullptr || !shot->ok) {
        const std::string line = shot == nullptr ? std::string("연구의 표를 읽는 중입니다.") : "연구의 표를 읽을 수 없습니다 (" + shot->why + ")";
        ImGui::TextWrapped("%s", line.c_str());
        note("list:off", line);
        return;
    }
    if (shot->player != L.chosen_player) {       // 다른 나라로 플레이하게 됐다
        L.chosen.clear();
        L.chosen_player = shot->player;
    }

    bool designs = units || L.list.designs;
    if (units) {
        // 영역: 육군 · 공군 · 해군 · 미사일 · 시설. 바꾸면 병과의 거르개는 풀린다(그 영역의 병과만 고를 수 있다)
        const auto domain_name = [](int domain) { return domain < 0 ? std::string("전체") : domain_label(domain); };
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::BeginCombo("##domain", domain_name(L.list.domain).c_str())) {
            for (int domain = -1; domain < DOMAINS; domain++) {
                if (ImGui::Selectable(domain_name(domain).c_str(), domain == L.list.domain)) {
                    L.list.domain = domain;
                    L.list.kind = -1;
                    g_confirm.clear();
                }
                note("domain:" + std::to_string(domain), domain_name(domain));
            }
            ImGui::EndCombo();
        }
        note("list:domain", domain_name(L.list.domain));
    } else {
        if (ImGui::RadioButton("기술", !designs))
            designs = false;
        note("list:techs", designs ? "0" : "1");
        ImGui::SameLine();
        if (ImGui::RadioButton("부대 설계", designs))
            designs = true;
        note("list:designs", designs ? "1" : "0");
    }
    if (designs != L.list.designs) {             // 목록을 바꾸면 분류와 고른 것은 풀린다(번호가 다른 표의 것이다)
        L.list.designs = designs;
        L.list.kind = -1;
        L.chosen.clear();
        g_confirm.clear();
    }
    if (g_picked == 0 && (L.list.show == Show::Picked || L.list.show == Show::FromPicked))
        L.list.show = Show::NotMine;             // 고른 나라가 없어졌다
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::BeginCombo("##show", SHOWS[static_cast<int>(L.list.show)])) {
        for (int i = 0; i < static_cast<int>(sizeof(SHOWS) / sizeof(SHOWS[0])); i++) {
            const bool needs_country = i == static_cast<int>(Show::Picked) || i == static_cast<int>(Show::FromPicked);   // 이 둘은 나라를 골라야 한다
            ImGui::BeginDisabled(needs_country && g_picked == 0);
            if (ImGui::Selectable(SHOWS[i], i == static_cast<int>(L.list.show))) {
                L.list.show = static_cast<Show>(i);
                g_confirm.clear();
            }
            note("show:" + std::to_string(i), SHOWS[i]);
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    note("list:show", SHOWS[static_cast<int>(L.list.show)]);
    const auto kind_label = [&](int kind) {
        return kind < 0 ? std::string("전체") : designs ? design_class_label(kind) : tech_kind_label(kind);
    };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("##kind", kind_label(L.list.kind).c_str())) {
        for (int kind = -1; kind < (designs ? DESIGN_CLASSES : TECH_KINDS + 1); kind++) {
            if (!designs && kind == 0)
                continue;                        // 기술의 분류는 1 부터다
            if (units && kind >= 0 && L.list.domain >= 0 && design_domain(kind) != L.list.domain)
                continue;                        // 고른 영역의 병과만
            if (ImGui::Selectable(kind_label(kind).c_str(), kind == L.list.kind)) {
                L.list.kind = kind;
                g_confirm.clear();
            }
            note("kind:" + std::to_string(kind), kind_label(kind));
        }
        ImGui::EndCombo();
    }
    note("list:kind", kind_label(L.list.kind));
    // 둘째 줄: 보유국(그 나라가 보유한 것만 — 보기와 함께 걸린다)과 찾기. 고르면 "외교·영토" 탭의 고른 나라도 그 나라가 된다
    if (g_picked == 0)
        L.list.by_picked = false;
    const std::string owner = L.list.by_picked ? region_label(g_picked) + " (" + std::to_string(g_picked) + ")" : std::string("전체");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("보유국");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##owner", owner.c_str())) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ownerfind", "검색 (이름 · 번호)", L.owner_find, sizeof(L.owner_find));
        if (ImGui::Selectable("전체", !L.list.by_picked)) {
            L.list.by_picked = false;
            g_confirm.clear();
        }
        note("owner:0", "전체");
        for (int number : region_view(g_regions, game.player, g_picked, L.owner_find).rows) {
            const std::string label = region_label(number) + " (" + std::to_string(number) + ")";
            if (ImGui::Selectable(label.c_str(), L.list.by_picked && number == g_picked)) {
                g_picked = number;
                L.list.by_picked = true;
                g_confirm.clear();
            }
            note("owner:" + std::to_string(number), label);
        }
        ImGui::EndCombo();
    }
    note("list:owner", owner);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##find", "찾기 (이름 · 번호)", L.find, sizeof(L.find)))
        g_confirm.clear();
    note("list:find", L.find);
    L.list.find = L.find;

    bool rebuilt = false;
    if (shot->serial != L.rows_serial || !same(L.list, L.rows_filter)) {
        L.rows = research_rows(shot->tables, L.list);
        L.rows_serial = shot->serial;
        L.rows_filter = L.list;
        rebuilt = true;
    }
    const bool with_picked = shot->picked != 0 && shot->picked == g_picked;   // 고른 나라의 열 — 그 나라로 뜬 스냅숏일 때만
    const std::string why = game_research_off();
    const bool off = !why.empty();
    // 되돌리기 어려운 단추(전부 · 모든 것을 미완료 · 해제, 보이는 것 전부 연구 가능으로)는 한 번 더 눌러야 한다
    const auto risky = [&](const char *id, const std::string &label, const std::vector<int> &ids, const char *what,
                           Research action = Research::Revoke) {
        const bool asking = g_confirm == id;
        const std::string text = asking ? label + " — 한 번 더 누르면 실행합니다" : label;
        if (ImGui::Button((text + "###" + id).c_str())) {
            if (asking)
                list_request(L, action, ids, what);
            else
                g_confirm = id;
        }
        note(id, text);
    };
    if (units) {
        // 거르개와 상관없이 모든 부대 설계(자국이 처음부터 가진 것도 해제된다 — 사용자가 정했다).
        // 표 위에 둔다 — 아래에 두면 탭을 끝까지 내려야 보였다
        std::vector<int> every;
        every.reserve(shot->tables.designs.size());
        for (const DesignRow &d : shot->tables.designs)
            every.push_back(d.id);
        ImGui::BeginDisabled(off || every.empty());
        if (ImGui::Button("모든 설계도 등록"))
            list_request(L, Research::Complete, every, "모든 설계도");
        note("list:every_done", "모든 설계도 등록");
        ImGui::SameLine();
        risky(LIST_UNDO_EVERY, "모든 설계도 해제", every, "모든 설계도");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%d종 · 등록하면 선행 기술도 함께 보유가 된다", static_cast<int>(every.size()));
        // 고른 설계의 부대를 게임의 지도에서 고른 칸에 만든다 — 설계마다 수량만큼(보유하지 않은 설계도 된다. 내장 치트와 같다)
        const std::string spawn_off = game_act_off(ACT_SPAWN);
        int hex_x = 0, hex_y = 0;
        const bool on_hex = spawn_off.empty() && game_map_hex(&hex_x, &hex_y);
        const bool many = static_cast<long long>(L.chosen.size()) * L.each > SPAWN_LIMIT;
        const std::string where = !spawn_off.empty() ? "부대 생성 — " + spawn_off
            : many ? "한 번에 " + std::to_string(SPAWN_LIMIT) + "기까지입니다" : hex_label(on_hex, hex_x, hex_y);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("##each", &L.each, 1, 10))                        // 빠른 걸음은 10(기본 100 은 한 번에 한도까지 간다)
            L.each = L.each < 1 ? 1 : L.each > SPAWN_LIMIT ? SPAWN_LIMIT : L.each;
        note("list:each", std::to_string(L.each));
        ImGui::SameLine();
        ImGui::BeginDisabled(!on_hex || many || L.chosen.empty());
        if (ImGui::Button("고른 것을 수량만큼 생성")) {
            g_confirm.clear();
            std::vector<int> stats;                                           // 경험 · 사기 · 효율의 퍼센트. 정하지 않은 것은 -1
            for (int s = 0; s < STAT_COUNT; s++)
                stats.push_back(L.stat_on[s] ? L.stat_value[s] : -1);
            g_notice = keeper_enqueue_spawn(std::vector<int>(L.chosen.begin(), L.chosen.end()), L.each, stats) ? ""
                : "대기 중인 요청이 많아 받지 못했습니다.";
        }
        note("list:spawn", "고른 것을 수량만큼 생성");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", where.c_str());
        note("list:hex", where);
        // 만든 부대의 상태: 켠 것만 그 퍼센트로 쓴다(내 부대 전부의 목록을 읽을 수 있어야 한다 — 새로 생긴 부대를 거기서 찾는다)
        static const char *const STATS[STAT_COUNT] = {"경험", "사기", "효율"};
        const std::string stat_off = game_act_off(ACT_UNITS);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("만든 부대의");
        ImGui::BeginDisabled(!stat_off.empty());
        for (int s = 0; s < STAT_COUNT; s++) {
            ImGui::PushID(s);
            ImGui::SameLine();
            ImGui::Checkbox(STATS[s], &L.stat_on[s]);
            note("list:stat:" + std::to_string(s), L.stat_on[s] ? "1" : "0");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##stat", &L.stat_value[s], 1, 10))
                L.stat_value[s] = L.stat_value[s] < 0 ? 0 : L.stat_value[s] > 100 ? 100 : L.stat_value[s];
            note("list:stat:" + std::to_string(s) + ":value", std::to_string(L.stat_value[s]));
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // 줄을 아낀다 — 720 폭의 창에서 오른쪽이 잘렸다(2026-10-19). 자세한 것은 풍선말로
        ImGui::TextDisabled("%s", stat_off.empty() ? "% — 켠 것만 (?)" : stat_off.c_str());
        if (stat_off.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("켠 것만 정한다. 사기 · 효율은 게임이 자정마다 다시 셈한다");
    }
    std::vector<int> visible;
    size_t chosen_visible = 0;
    visible.reserve(L.rows.size());
    for (const ListRow &r : L.rows) {
        visible.push_back(r.id);
        chosen_visible += L.chosen.count(r.id);
    }
    std::sort(visible.begin(), visible.end());
    const size_t hidden = L.chosen.size() - std::min(L.chosen.size(), chosen_visible);
    const std::string count = "보이는 것 " + std::to_string(L.rows.size()) + "개 · 고른 것 " + std::to_string(L.chosen.size()) + "개"
        + (hidden > 0 ? "(보이지 않는 것 " + std::to_string(hidden) + "개)" : std::string());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(count.c_str());
    note("list:count", count);
    ImGui::SameLine();
    ImGui::BeginDisabled(L.chosen.empty());
    if (ImGui::Button("고르기 풀기")) {
        L.chosen.clear();
        g_confirm.clear();
    }
    note("list:clear", "고르기 풀기");
    ImGui::EndDisabled();

    // 단추 여섯(완료 · 미완료 · 연구 가능으로를 고른 것 / 보이는 것 전부에). "고른 것"은 지금 보이지 않는 줄이라도 고른 것 모두에 한다. 쓸 수 없는 까닭은 위의 단추 자리에 적혀 있다
    // (부대 탭에는 연구의 단추가 없다 — 까닭을 여기에 적는다)
    const char *const yes = units ? "등록" : "완료", *const no = units ? "해제" : "미완료";
    if (units && off) {
        ImGui::TextWrapped("%s", why.c_str());
        note("list:why", why);
    }
    const std::vector<int> chosen(L.chosen.begin(), L.chosen.end());
    ImGui::BeginDisabled(off || chosen.empty());
    if (ImGui::Button((std::string("고른 것 ") + yes).c_str()))
        list_request(L, Research::Complete, chosen, "고른 것");
    note("list:done", std::string("고른 것 ") + yes);
    ImGui::SameLine();
    if (ImGui::Button((std::string("고른 것 ") + no).c_str()))
        list_request(L, Research::Revoke, chosen, "고른 것");
    note("list:undo", std::string("고른 것 ") + no);
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginDisabled(off || visible.empty());
    if (ImGui::Button((std::string("보이는 것 전부 ") + yes).c_str()))
        list_request(L, Research::Complete, visible, "보이는 것");
    note("list:all_done", std::string("보이는 것 전부 ") + yes);
    ImGui::SameLine();
    risky(LIST_UNDO_ALL, std::string("보이는 것 전부 ") + no, visible, "보이는 것");
    ImGui::EndDisabled();
    // 연구 가능으로: 항목은 보유가 되지 않는다 — 모자란 선행 기술만 보유로(docs/superpowers/specs/2026-10-15-toybox-research-unlock-design.md)
    ImGui::BeginDisabled(off || chosen.empty());
    if (ImGui::Button("고른 것 연구 가능으로"))
        list_request(L, Research::Unlock, chosen, "고른 것");
    note("list:unlock", "고른 것 연구 가능으로");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        wrapped_tooltip(UNLOCK_TIP);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(off || visible.empty());
    risky(LIST_UNLOCK_ALL, "보이는 것 전부 연구 가능으로", visible, "보이는 것", Research::Unlock);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        wrapped_tooltip(UNLOCK_TIP);
    ImGui::EndDisabled();
    // 수와 단추의 줄들은 표 위에 둔다 — 아래에 두면 창이 낮을 때 탭을 내려야 보였다(게임에서 봤다). 표가 남은 높이를 다 쓴다
    const float height = std::max(6.0f * ImGui::GetFrameHeightWithSpacing(), ImGui::GetContentRegionAvail().y);
    // 열의 너비는 머리 줄의 경계를 끌어 바꾸고, 머리 줄을 누르면 그 열로 놓는다(한 번 더 누르면 거꾸로)
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;
    // 표는 하나다 — 고른 나라의 열은 켜고 끌 뿐이다(열의 수가 다른 표로 바꾸면 놓은 순서와 고친 너비가 처음으로 돌아간다)
    const int columns = 8;
    // 상태 열의 너비는 가장 긴 상태의 글("이미 개량됨" · "다른 지역 전용" …)로 잰다 — 보이는 줄로 재면 처음에 없던 긴 글이 잘린다(게임에서 봤다, 2026-10-16).
    // 표의 이름에 9 를 붙였다: 옛 표("…8")의 저장된 열 너비가 이 너비를 덮지 않게(열 너비 · 놓은 순서는 한 번 처음으로 돌아간다)
    float state_width = ImGui::CalcTextSize(units ? "등록" : "상태").x;
    for (const char *label : {"보유", "등록", "연구 중", "연구 가능", "선행 부족"})
        state_width = std::max(state_width, ImGui::CalcTextSize(label).x);
    for (int block = BLOCK_NONE + 1; block < BLOCKS; block++)
        state_width = std::max(state_width, ImGui::CalcTextSize(block_label(block).c_str()).x);
    if (ImGui::BeginTable(units ? "units9" : "list9", columns, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        // 처음 너비는 보이는 줄에서 잰다 — 긴 병과("전략 폭격기")가 처음에 보이지 않으면 잘린다. 부대 설계는 넉넉히 준다
        ImGui::TableSetupColumn(designs ? "병과" : "분류", designs ? ImGuiTableColumnFlags_WidthFixed : 0, designs ? 7.0f * ImGui::GetFontSize() : 0.0f);
        ImGui::TableSetupColumn(designs ? "연도" : "수준", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn(units ? "등록" : "상태", ImGuiTableColumnFlags_WidthFixed, state_width);   // 부대 탭: 등록 · 연구 중 · 연구 가능 · 선행 부족 · 열 수 없음(state_label)
        ImGui::TableSetupColumn("선행");                              // 선행 기술을 플레이어가 보유했는가: "보유" · "2/3" · "-"(선행이 없다)
        ImGui::TableSetupColumn("보유국", ImGuiTableColumnFlags_WidthStretch, 1.0f);   // 이름이 여럿이다 — 남는 너비를 이름 열과 나눠 쓴다
        ImGui::TableSetupColumn(with_picked ? region_label(shot->picked).c_str() : "고른 나라", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetColumnEnabled(7, with_picked);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);         // TableHeadersRow 가 하는 일 — 머리 칸의 자리를 적으려고 풀어 썼다
        for (int column = 0; column < columns; column++) {
            if (!ImGui::TableSetColumnIndex(column))
                continue;                                         // 꺼진 열(고른 나라가 없다)
            ImGui::TableHeader(ImGui::TableGetColumnName(column));
            note("head:" + std::to_string(column), ImGui::TableGetColumnName(column));
        }
        if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                L.sort_column = specs->Specs[0].ColumnIndex;
                L.sort_down = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                rebuilt = true;
            }
        }
        if (rebuilt && L.sort_column >= 0 && !(L.sort_column == 3 && !L.sort_down))   // 줄은 수준의 오름차순으로 만들어진다
            sort_rows(L, L.sort_column, L.sort_down);
        ImGuiListClipper clipper;                // 줄이 많다(부대 설계 수천) — 보이는 줄만 그린다
        clipper.Begin(static_cast<int>(L.rows.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const ListRow &r = L.rows[static_cast<size_t>(i)];
                const std::string cells[6] = {r.name, kind_label(r.kind), std::to_string(r.level),
                                              state_label(r, units),
                                              needs_label(r),
                                              owners_label(r, shot->tables.regions)};
                ImGui::PushID(r.id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool on = L.chosen.count(r.id) != 0;
                if (ImGui::Checkbox("##on", &on)) {
                    if (on)
                        L.chosen.insert(r.id);
                    else
                        L.chosen.erase(r.id);
                    g_confirm.clear();
                }
                std::string line = on ? "1" : "0";
                for (const std::string &cell : cells)
                    line += "|" + cell;
                if (with_picked)
                    line += r.picked ? "|보유" : "|";
                note("item:" + std::to_string(r.id), line);   // 고르는 칸의 자리와, 그 줄의 글
                for (int c = 0; c < 6; c++) {
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(cells[c].c_str());
                    if (c == 4 && r.needs > 0 && ImGui::IsItemHovered())          // 선행 기술마다 보유했는가
                        wrapped_tip(needs_detail(r, shot->tables));
                    else if (c == 5 && r.others > 2 && ImGui::IsItemHovered())    // 칸에는 둘까지만 적는다 — 풍선말에 모두
                        wrapped_tip(first_names(game_owner_names(designs, r.id), 80));
                }
                if (with_picked && ImGui::TableSetColumnIndex(7)) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(r.picked ? "보유" : "");
                }
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
}

void settings_tab()
{
    ImGui::Text("창 여닫기: %s", hotkey_name(g_settings.hotkey_vk, g_settings.hotkey_mods).c_str());
    if (g_capturing)
        ImGui::TextUnformatted("새 조합을 누르십시오. ESC 는 취소입니다.");
    else if (ImGui::Button("단축키 바꾸기"))
        g_capturing = true;
    ImGui::Spacing();
    // 실제 폴더를 보인다 — SRTOYBOX_HOME 으로 띄우면 %APPDATA% 가 아니다(2026-10-19). 환경 변수는 실행 중에 바뀌지 않으니 한 번만 읽는다
    static const std::string where = settings_dir_text();
    if (where.empty())
        ImGui::TextDisabled("설정을 저장할 폴더를 찾지 못했습니다.");
    else {
        ImGui::PushTextWrapPos(0.0f);   // 경로가 길면 창 폭에서 줄을 바꾼다
        ImGui::TextDisabled("설정은 %s 에 저장됩니다.", where.c_str());
        ImGui::PopTextWrapPos();
    }
}

// 상태 줄: ToyBox 가 게임을 어떻게 보고 있는지 한 줄로.
void status_line(const GameState &game)
{
    const std::string state = !game.known ? "게임 상태를 읽을 수 없습니다 — 단추가 꺼져 있습니다"
        : game.multiplayer ? "멀티플레이에서는 동작하지 않습니다"
        : !game.in_game ? "게임을 진행 중이 아닙니다 — 단추가 꺼져 있습니다"
        : "플레이 중: " + region_label(game.player) + " (" + std::to_string(game.player) + ")";
    const std::string text = state + keep_text(game) + reserve_text() + mult_text() + social_text();   // 최소 유지 · 배율이 켜져 있으면 늘 보인다 — 켜 둔 것을 잊지 않게
    if (!game.known)
        ImGui::TextDisabled("%s", text.c_str());
    else
        ImGui::TextUnformatted(text.c_str());
    note("status", text);
}

}  // namespace

void ui_init()
{
    Lock lock(ui_mutex());
    g_settings = load_settings();
    send_keep(false);             // 저장해 둔 최소 유지를 keeper 에 넘긴다 — 창을 한 번도 열지 않아도 돈다
    send_mult(false);             // 배율도
    send_social(false);           // 사회도
    send_reserve(false);          // 예비 인원의 최소 유지도
    g_units_list.list.show = Show::All;   // 설계도 목록은 전체부터 — 모두 등록해도 목록이 비지 않는다(등록 열로 가른다)
}

bool ui_visible()
{
    Lock lock(ui_mutex());
    return g_visible;
}

void ui_toggle()
{
    Lock lock(ui_mutex());
    g_visible = !g_visible;
    // 닫혀 있는 동안의 글쇠는 ImGui 에 가지 않는다 — 단축키로 닫을 때 떼는 Ctrl · Shift 를 놓쳐, 다시 열면 ImGui 가 Ctrl 을 쥔 것으로 보고
    // + · − 가 큰 걸음으로 움직였다(게임에서 봤다, 2026-10-19). 열고 닫을 때 글쇠 상태를 비운다 — 다음 글쇠 메시지가 지금 상태를 다시 알린다
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::GetIO().ClearInputKeys();
    g_capturing = false;
    g_confirm.clear();
    g_report.clear();   // 닫힌 창의 목록을 남겨 두지 않는다
}

bool ui_is_hotkey(int vk, int mods)
{
    Lock lock(ui_mutex());
    return vk == g_settings.hotkey_vk && mods == g_settings.hotkey_mods;
}

bool ui_hit(int x, int y)
{
    Lock lock(ui_mutex());
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    return g_visible && fx >= g_rect[0] && fx < g_rect[2] && fy >= g_rect[1] && fy < g_rect[3];
}

bool ui_capturing_hotkey()
{
    Lock lock(ui_mutex());
    return g_visible && g_capturing;
}

void ui_capture_key(int vk, int mods)
{
    Lock lock(ui_mutex());
    if (vk == 0x10 || vk == 0x11 || vk == 0x12)   // Shift · Ctrl · Alt 만 눌린 동안은 더 기다린다
        return;
    g_capturing = false;
    if (valid_hotkey(vk, mods)) {                 // ESC 만 누른 것은 valid_hotkey 가 거른다 = 취소
        g_settings.hotkey_vk = vk;
        g_settings.hotkey_mods = mods;
        save_settings(g_settings);
    }
}

void ui_draw()
{
    Lock lock(ui_mutex());
    g_drawing.clear();
    ImGui::SetNextWindowPos(ImVec2(40.0f, 60.0f), ImGuiCond_FirstUseEver);
    // 물자 탭의 표(물자 열하나 + "모든 물자")가 굴리지 않아도 다 보이는 크기. 이미 써 본 사용자의 창은 저장된 크기 그대로다
    ImGui::SetNextWindowSize(ImVec2(720.0f, 600.0f), ImGuiCond_FirstUseEver);
    // 창은 구르지 않는다. 구르는 것은 탭의 내용(body)뿐이다 — 맨 위의 상태 줄 · 고른 나라와 바닥의 알림은 늘 보인다
    if (ImGui::Begin("SR2030 ToyBox", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const GameState game = game_state();
        static double names_at = -10.0;              // 게임이 지금 판에서 쓰는 지역 이름 — 1초마다(판 · 나라가 바뀌어도 곧 따라온다)
        if (ImGui::GetTime() - names_at > 1.0) {
            regions_set_game_names(game_region_names());
            names_at = ImGui::GetTime();
        }
        const bool faulted = runner_faulted();
        const bool blocked = faulted || (game.known && (!game.in_game || game.multiplayer));
        const std::string trouble = runner_notice();
        if (faulted) {   // 맨 위에, 빨갛게: 게임의 상태가 어긋났을 수 있다 — 저장하지 말라는 말이 가려지면 안 된다
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", trouble.c_str());
            ImGui::PopStyleColor();
            note("fault", trouble);
        } else {
            status_line(game);
        }
        if (g_footer <= 0.0f)
            g_footer = 3.0f * ImGui::GetTextLineHeightWithSpacing();   // 첫 프레임의 어림. 그 뒤로는 지난 프레임에 잰 값
        const ImVec2 body(0.0f, -g_footer);
        // 지금 보이는 탭이 내장 치트로 도는 줄이 없는 탭(돈 · 물자, 그리고 줄이 모두 직접 쓰는 줄인 연구)인가, 그 탭을 쓸 수 있는가
        bool values_tab = false, values_ok = false;
        if (ImGui::BeginTabBar("tabs")) {
            const bool money = ImGui::BeginTabItem("돈");   // 첫 탭. 기능 표에 없다 — 내장 치트 없이 직접 한다
            note("tab:돈", "돈");
            if (money) {
                values_tab = true;
                if (ImGui::BeginChild("body", body))
                    values_ok = money_tab(game, blocked);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            const bool stock = ImGui::BeginTabItem("물자");   // 둘째 탭. 이것도 기능 표에 없다
            note("tab:물자", "물자");
            if (stock) {
                values_tab = true;
                if (ImGui::BeginChild("body", body))
                    values_ok = stock_tab(game, blocked);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            const bool mult = ImGui::BeginTabItem("배율");   // 셋째 탭. 설정이다 — 단추가 없다
            note("tab:배율", "배율");
            if (mult) {
                if (ImGui::BeginChild("body", body))
                    mult_tab(game);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            const bool social = ImGui::BeginTabItem("사회");   // 넷째 탭. 설정이다 — 단추가 없다(2026-10-21)
            note("tab:사회", "사회");
            if (social) {
                if (ImGui::BeginChild("body", body))
                    social_tab(game);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            const char *tab = nullptr;
            for (int i = 0; i < FEATURE_COUNT; i++) {
                if (tab != nullptr && strcmp(tab, FEATURES[i].tab) == 0)
                    continue;   // 이 탭은 앞에서 그렸다(같은 탭의 기능은 표에서 이어져 있다)
                tab = FEATURES[i].tab;
                const bool open = ImGui::BeginTabItem(tab);
                note(std::string("tab:") + tab, tab);
                if (!open)
                    continue;
                const bool diplomacy = strcmp(tab, DIPLOMACY_TAB) == 0, research = strcmp(tab, RESEARCH_TAB) == 0;
                const bool usable = !diplomacy || game.known;   // 이번 게임의 나라 목록을 모르면 이 탭은 쓰지 못한다
                RegionView view;
                if (diplomacy && usable)
                    view = choose(game, "아래 목록에서 고르십시오");   // "고른 나라" 줄은 구르는 내용 밖에 둔다
                else if ((research || strcmp(tab, UNITS_TAB) == 0) && game.known && game.in_game)
                    choose(game, "외교·영토 탭에서 고릅니다");         // 목록의 "고른 나라" 열과 보기가 이 나라다
                if (ImGui::BeginChild("body", body)) {
                    if (!usable) {
                        ImGui::TextWrapped("게임 상태를 읽을 수 있을 때만 씁니다.");
                    } else {
                        ImGui::BeginDisabled(blocked);
                        if (diplomacy)
                            picker(view);
                        // 부대 탭은 줄이 많다 — 접을 수 있게 한다(접으면 아래의 설계도 목록이 넓어진다)
                        const bool units_tab = strcmp(tab, UNITS_TAB) == 0;
                        const bool rows = !units_tab || ImGui::CollapsingHeader("부대 기능 (접으면 아래 목록이 넓어집니다)", ImGuiTreeNodeFlags_DefaultOpen);
                        bool direct_ok = false;
                        for (int j = i; rows && j < FEATURE_COUNT && strcmp(FEATURES[j].tab, tab) == 0; j++) {
                            if (direct_row(FEATURES[j], game, research)) {
                                direct_ok = true;
                                if (research && j + 1 < FEATURE_COUNT && strcmp(FEATURES[j + 1].tab, tab) == 0)
                                    ImGui::SameLine();   // 연구 탭의 단추들은 한 줄에 — 아래는 목록의 자리다
                            }
                        }
                        if (strcmp(tab, PEOPLE_TAB) == 0) {
                            reserve_rows(game);
                            loyalty_rows(game);
                        }
                        ImGui::EndDisabled();
                        if (research)
                            item_list(g_research_list, false, game, blocked);
                        else if (strcmp(tab, UNITS_TAB) == 0) {
                            if (rows) {
                                ImGui::BeginDisabled(blocked);
                                army_rows();
                                ImGui::EndDisabled();
                            }
                            item_list(g_units_list, true, game, blocked);
                        }
                        values_tab = true;
                        values_ok = direct_ok;
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("설정")) {
                if (ImGui::BeginChild("body", body))
                    settings_tab();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        const float top = ImGui::GetCursorPosY();
        ImGui::Separator();
        const std::string wrote = keeper_last();
        if (!wrote.empty()) {
            ImGui::Text("마지막으로 쓴 값: %s", wrote.c_str());
            note("wrote", wrote);
        }
        const std::string unwritten = keeper_notice();
        if (!unwritten.empty()) {
            ImGui::TextWrapped("%s", unwritten.c_str());
            note("unwritten", unwritten);
        }
        if (!g_notice.empty()) {
            ImGui::TextWrapped("%s", g_notice.c_str());
            note("notice", g_notice);
        }
        if (!faulted && !trouble.empty()) {
            ImGui::TextWrapped("%s", trouble.c_str());
            note("trouble", trouble);
        }
        // 게임 화면에 무슨 일이 생기는지. 게임 밖에서는 단추가 꺼져 있으므로(상태 줄이 그렇게 말한다) 띄우지 않는다.
        // 열려 있던 게임의 패널은 스스로 다시 그려지지 않는다 — 일시 정지 중에도, 시간이 흐르는 중에도(같은 날 안에서) [확인: 게임]
        // 쓸 수 없는 탭에서는 까닭의 줄이 전부다.
        const char *const hint = (game.known && blocked) || !values_tab || !values_ok ? nullptr
            : "값은 바로 바뀝니다. 게임 화면의 숫자는 그 패널을 누르거나 다시 열 때 따라옵니다.";
        if (hint != nullptr) {
            ImGui::TextWrapped("%s", hint);
            note("hint", hint);
        }
        g_footer = ImGui::GetCursorPosY() - top;
    }
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    g_rect[0] = pos.x;
    g_rect[1] = pos.y;
    g_rect[2] = pos.x + size.x;
    g_rect[3] = pos.y + size.y;
    ImGui::End();
    g_report = g_drawing;
}

std::string ui_report()
{
    Lock lock(ui_mutex());
    g_reporting = true;
    return g_report;
}
