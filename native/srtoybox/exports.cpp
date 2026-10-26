// 테스트(tests/test_toybox.py)와 불러오는 쪽(srhook)이 쓰는 C 인터페이스. 글은 UTF-8, 버퍼가 작거나 대상이 없으면 -1.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "features.h"
#include "game.h"
#include "input.h"
#include "keeper.h"
#include "locate.h"
#include "log.h"
#include "overlay.h"
#include "products.h"
#include "prologue.h"
#include "regions.h"
#include "research.h"
#include "techs.h"
#include "runner_win.h"
#include "settings.h"
#include "sigs.h"
#include "ui.h"
#include "values.h"

#define EXPORT extern "C" __declspec(dllexport)

namespace {

int put(const std::string &text, char *out, int size)
{
    if (out == nullptr || size <= 0 || text.size() + 1 > static_cast<size_t>(size))
        return -1;
    memcpy(out, text.c_str(), text.size() + 1);
    return static_cast<int>(text.size());
}

// 서명마다의 결과를 글로: 한 줄에 "<찾을 것>\t<서명 글>\t<맞은 횟수>\t<처음 맞은 자리>\t<읽어 낸 값>\t<둘째 값>"(뒤의 넷은 16진수).
std::string rows_text(const SigRow *rows, int n)
{
    std::string text;
    char numbers[80];
    for (int i = 0; i < n; i++) {
        snprintf(numbers, sizeof(numbers), "\t%x\t%x\t%x\t%x\n", static_cast<unsigned>(rows[i].count), rows[i].at, rows[i].value,
                 rows[i].value2);
        text += std::string(rows[i].name) + '\t' + rows[i].text + numbers;
    }
    return text;
}

// 테스트가 글로 주는 연구의 표. 한 줄에 하나:
//   slots <기술 자리 수> <설계 자리 수>
//   t <번호> <분류> <수준> <선행 0> <선행 1> <보유> <묶음이 있다>
//   d <번호> <연구 대상> <게임이 건너뛰는 설계> <보유> <묶음이 있다> <선행 0> <선행 1> <선행 2> <선행 3>
//   q <종류> <번호> <깃발 0(16진수)> <깃발 1(16진수)>
//   region <플레이어 지역의 마스크> / tmask <기술 번호> <마스크> / dmask <설계 번호> <마스크> <특수> / dup <설계 번호> <개량되어 되는 설계> [<둘째>] / dold <설계 번호> <단종 연도 − 1900> <남는가> / year <지금 해 − 1900> (앞 줄의 행에 붙인다)
// t 와 d 의 줄 끝에는 목록의 것을 더 줄 수 있다(없으면 0 · 빈 글): t 는 <다른 나라의 수> <고른 나라가 보유>,
// d 는 <다른 나라의 수> <고른 나라가 보유> <병과> <연도> <이름(빈칸 없이)>.
ResearchTables tables_from(const std::string &text)
{
    ResearchTables t;
    std::istringstream lines(text);
    std::string line, word;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        int mine = 0, housed = 0, open = 0, held = 0, picked = 0;
        in >> word;
        if (word == "slots") {
            in >> t.tech_slots >> t.design_slots;
        } else if (word == "t") {
            TechRow row;
            in >> row.id >> row.kind >> row.level >> row.needs[0] >> row.needs[1] >> mine >> housed;
            if (in >> row.others >> picked)
                row.picked = picked != 0;
            row.mine = mine != 0;
            row.housed = housed != 0;
            t.techs.push_back(row);
        } else if (word == "d") {
            DesignRow row;
            in >> row.id >> open >> held >> mine >> housed >> row.needs[0] >> row.needs[1] >> row.needs[2] >> row.needs[3];
            if (in >> row.others >> picked >> row.cls >> row.year) {
                row.picked = picked != 0;
                in >> row.name;
            }
            row.open = open != 0;
            row.held = held != 0;
            row.mine = mine != 0;
            row.housed = housed != 0;
            t.designs.push_back(row);
        } else if (word == "region") {
            in >> t.region_mask;
        } else if (word == "year") {
            in >> t.year;
        } else if (word == "dup" || word == "dold") {
            int id = 0, a = 0, b = 0;
            in >> id >> a >> b;
            for (auto row = t.designs.rbegin(); row != t.designs.rend(); ++row)
                if (row->id == id) {
                    if (word == "dup") {
                        row->upgrade_to = a;
                        row->upgrade_to_2 = b;
                    } else {
                        row->retired = a;
                        row->kept = b != 0;
                    }
                    break;
                }
        } else if (word == "tmask" || word == "dmask") {
            int id = 0, special = 0;
            uint64_t mask = 0;
            in >> id >> mask >> special;
            if (word == "tmask") {
                for (auto row = t.techs.rbegin(); row != t.techs.rend(); ++row)
                    if (row->id == id) {
                        row->mask = mask;
                        break;
                    }
            } else {
                for (auto row = t.designs.rbegin(); row != t.designs.rend(); ++row)
                    if (row->id == id) {
                        row->mask = mask;
                        row->special = special != 0;
                        break;
                    }
            }
        } else if (word == "q") {
            QueueRow row;
            in >> row.kind >> row.id >> std::hex >> row.flags[0] >> row.flags[1];
            t.queue.push_back(row);
        }
    }
    research_mark_queued(&t);
    return t;
}

// 테스트가 글로 주는 "무엇을": "items t1 t2 d5" · "level 120" · "queue".
ResearchWhat what_from(const std::string &text)
{
    ResearchWhat what;
    std::istringstream in(text);
    std::string word;
    in >> word;
    if (word == "level") {
        what.kind = ResearchWhat::Level;
        in >> what.level;
    } else if (word == "queue") {
        what.kind = ResearchWhat::Queue;
    } else {
        while (in >> word)
            if (word.size() > 1)
                (word[0] == 't' ? what.techs : what.designs).push_back(atoi(word.c_str() + 1));
    }
    return what;
}

std::string numbers(const char *name, const std::vector<int> &values)
{
    std::string text = name;
    for (int value : values)
        text += ' ' + std::to_string(value);
    return text + '\n';
}

}  // namespace

EXPORT int srtoybox_feature_count(void)
{
    return FEATURE_COUNT;
}

EXPORT int srtoybox_cheat_feature_count(void)
{
    return cheat_feature_count();
}

// 한 줄: id, 탭, 이름, 명령, 값 있음(0/1), 기본값, 최소, 최대, 확인(0/1), 설명, 대상(none/player/picked),
// 하는 길(cheat = 내장 치트 / tech_up · opinion_best · relation_best · relation_neutral · tech_level · queue_done = 직접 쓴다)
// — 탭 문자로 나눈다
EXPORT int srtoybox_feature_info(int index, char *out, int size)
{
    if (index < 0 || index >= FEATURE_COUNT)
        return -1;
    const Feature &f = FEATURES[index];
    static const char *const targets[] = {"none", "player", "picked"};
    static const char *const how[] = {"cheat", "tech_up", "opinion_best", "relation_best", "relation_neutral", "tech_level", "queue_done",
                                      "people_add", "approval_best", "colonize", "fight", "become", "novichok", "annex", "treaty", "supply", "best", "gui", "victory"};
    const std::string line = std::string(f.id) + '\t' + f.tab + '\t' + f.label + '\t' + f.command + '\t' + (f.has_value ? "1" : "0")
        + '\t' + std::to_string(f.def) + '\t' + std::to_string(f.min) + '\t' + std::to_string(f.max) + '\t'
        + (f.confirm ? "1" : "0") + '\t' + f.help + '\t' + targets[static_cast<int>(f.target)] + '\t'
        + how[static_cast<int>(f.direct)];
    return put(line, out, size);
}

EXPORT int srtoybox_settings_normalize(const char *ini, char *out, int size)
{
    return put(format_settings(parse_settings(ini == nullptr ? "" : ini)), out, size);
}

EXPORT int srtoybox_settings_store(const char *ini)
{
    return save_settings(parse_settings(ini == nullptr ? "" : ini)) ? 1 : 0;
}

EXPORT int srtoybox_settings_file(char *out, int size)
{
    return put(format_settings(load_settings()), out, size);
}

EXPORT int srtoybox_prologue_length(const unsigned char *code, int size, int want)
{
    return prologue_length(code, size, want);
}

// 테스트: 서명 글들(줄바꿈으로 나눈다)을 image 의 [begin, end) 에서 맞춰 본다.
// 서명마다 한 줄 "<맞은 횟수> <처음 맞은 자리> <값0> <값1>"(16진수. 글이 틀리면 "bad"),
// 끝 줄은 투표 "vote <찾았는가 0/1> <한 번만 맞은 수> <값0> <값1>".
EXPORT int srtoybox_sig_find(const unsigned char *image, unsigned long long size, unsigned begin, unsigned end, const char *sigs,
                             int need, char *out, int out_size)
{
    std::vector<Sig> parsed;
    std::vector<int> where;      // 줄 → parsed 의 칸. 틀린 글이면 -1
    const std::string all = sigs == nullptr ? "" : sigs;
    for (size_t pos = 0; pos <= all.size(); ) {
        size_t stop = all.find('\n', pos);
        if (stop == std::string::npos)
            stop = all.size();
        Sig sig = {};
        if (sig_parse(all.substr(pos, stop - pos).c_str(), &sig)) {
            where.push_back(static_cast<int>(parsed.size()));
            parsed.push_back(sig);
        } else {
            where.push_back(-1);
        }
        pos = stop + 1;
    }
    std::vector<SigHit> hits(parsed.size());
    const SigRange range = {begin, end};
    if (image != nullptr && !parsed.empty())
        sig_scan(image, static_cast<size_t>(size), &range, 1, parsed.data(), static_cast<int>(parsed.size()), hits.data());
    std::string text;
    char line[96];
    for (int at : where) {
        if (at < 0) {
            text += "bad\n";
            continue;
        }
        const SigHit &hit = hits[static_cast<size_t>(at)];
        snprintf(line, sizeof(line), "%x %x %llx %llx\n", static_cast<unsigned>(hit.count), hit.at, hit.value[0], hit.value[1]);
        text += line;
    }
    uint64_t value[SIG_CAPTURES] = {};
    int matched = 0;
    const bool found = !parsed.empty()
        && sig_vote(parsed.data(), hits.data(), static_cast<int>(parsed.size()), need, value, &matched);
    snprintf(line, sizeof(line), "vote %d %d %llx %llx", found ? 1 : 0, matched, value[0], value[1]);
    return put(text + line, out, out_size);
}

// 옛 찾기(전환 기간에만): 명령 처리 함수 · this · 옵션 묶음을 치트 닻으로. 0 이면 out 의 그 셋을 채웠다. -1 이면 error 에 까닭.
EXPORT int srtoybox_locate_legacy(const unsigned char *image, unsigned long long size, GameAddresses *out, char *error, int error_size)
{
    GameAddresses found = {};
    const char *why = locate_legacy(image, static_cast<size_t>(size), &found);
    if (why != nullptr) {
        put(why, error, error_size);
        return -1;
    }
    if (out != nullptr)
        *out = found;
    return 0;
}

// 새 찾기(상태 묶음, 서명으로). 0 이면 out 의 일곱 필드를 채웠다. -1 이면 error 에 까닭.
// rows 에는 서명마다 한 줄(rows_text). 필요 없으면 nullptr.
EXPORT int srtoybox_locate_state(const unsigned char *image, unsigned long long size, GameAddresses *out, char *error, int error_size,
                                 char *rows, int rows_size)
{
    GameAddresses found = {};
    SigRow table[STATE_WANTED * STATE_SIGS];
    char why[160] = "";
    const bool ok = locate_state(image, static_cast<size_t>(size), &found, table, why, sizeof(why));
    if (rows != nullptr)
        put(rows_text(table, STATE_WANTED * STATE_SIGS), rows, rows_size);
    if (!ok) {
        put(why, error, error_size);
        return -1;
    }
    if (out != nullptr)
        *out = found;
    return 0;
}

// 새 찾기(값 묶음, 서명으로). 0 이면 out 을 채웠다. -1 이면 error 에 까닭. rows 는 srtoybox_locate_state 와 같다.
EXPORT int srtoybox_locate_values(const unsigned char *image, unsigned long long size, ValueLayout *out, char *error, int error_size,
                                  char *rows, int rows_size)
{
    ValueLayout found = {};
    SigRow table[VALUE_WANTED * STATE_SIGS];
    char why[160] = "";
    const bool ok = locate_values(image, static_cast<size_t>(size), &found, table, why, sizeof(why));
    if (rows != nullptr)
        put(rows_text(table, VALUE_WANTED * STATE_SIGS), rows, rows_size);
    if (!ok) {
        put(why, error, error_size);
        return -1;
    }
    if (out != nullptr)
        *out = found;
    return 0;
}

// 새 찾기(더 쓰는 값 — 묶음 셋). 돌려주는 값은 찾은 묶음의 비트(1 지식, 2 여론, 4 관계). values 는 값 묶음(찾았을 때)이거나 nullptr.
// error 에는 묶음마다 한 줄(지식 · 여론 · 관계 순. 찾은 묶음은 빈 줄). rows 는 srtoybox_locate_state 와 같다.
EXPORT int srtoybox_locate_more(const unsigned char *image, unsigned long long size, const ValueLayout *values, MoreLayout *out,
                                char *error, int error_size, char *rows, int rows_size)
{
    MoreLayout found = {};
    SigRow table[MORE_WANTED * STATE_SIGS];
    char why[MORE_GROUPS][MORE_WHY] = {};
    const int groups = locate_more(image, static_cast<size_t>(size), values, &found, table, why);
    if (rows != nullptr)
        put(rows_text(table, MORE_WANTED * STATE_SIGS), rows, rows_size);
    std::string all;
    for (int g = 0; g < MORE_GROUPS; g++)
        all += std::string(why[g]) + '\n';
    put(all, error, error_size);
    if (out != nullptr)
        *out = found;
    return groups;
}

// 새 찾기(연구, 서명으로). state 는 이미 찾은 상태 묶음이다(지역 표를 견준다. nullptr 이면 못 찾은 것으로 친다).
// 0 이면 out 을 채웠다. -1 이면 error 에 까닭. rows 는 srtoybox_locate_state 와 같다.
EXPORT int srtoybox_locate_research(const unsigned char *image, unsigned long long size, const GameAddresses *state,
                                    ResearchLayout *out, char *error, int error_size, char *rows, int rows_size)
{
    ResearchLayout found = {};
    SigRow table[RESEARCH_WANTED * STATE_SIGS];
    char why[160] = "";
    const bool ok = locate_research(image, static_cast<size_t>(size), state != nullptr ? *state : GameAddresses(), &found, table, why,
                                    sizeof(why));
    if (rows != nullptr)
        put(rows_text(table, RESEARCH_WANTED * STATE_SIGS), rows, rows_size);
    if (!ok) {
        put(why, error, error_size);
        return -1;
    }
    if (out != nullptr)
        *out = found;
    return 0;
}

// 연구 묶음과 값 묶음의 대조(locate_research_fits). 0 이면 맞는다. -1 이면 error 에 까닭.
EXPORT int srtoybox_locate_research_fits(const ResearchLayout *research, const ValueLayout *values, char *error, int error_size)
{
    char why[160] = "";
    if (research == nullptr || values == nullptr)
        return -1;
    if (locate_research_fits(*research, *values, why, sizeof(why)))
        return 0;
    put(why, error, error_size);
    return -1;
}

// 테스트: 연구 목록의 줄(research_rows)을 글로 준 표(tables_from)에 돌린다. show 는 Show 의 번호, kind 는 분류 · 병과(-1 이면 전체).
// 한 줄에 "번호|이름|분류 · 병과의 이름|수준 · 연도|상태|다른 나라의 수|고른 나라가 보유(0/1)|선행 기술(needs_label)".
// domain 은 부대 설계의 영역(-1 이면 전체).
EXPORT int srtoybox_research_rows(const char *tables, int designs, int show, int kind, int domain, const char *find, char *out, int size)
{
    ListFilter filter;
    filter.domain = domain;
    filter.designs = designs != 0;
    filter.show = static_cast<Show>(show);
    filter.kind = kind;
    filter.find = find != nullptr ? find : "";
    std::string text;
    for (const ListRow &row : research_rows(tables_from(tables != nullptr ? tables : ""), filter))
        text += std::to_string(row.id) + '|' + row.name + '|' + (filter.designs ? design_class_label(row.kind) : tech_kind_label(row.kind)) + '|'
            + std::to_string(row.level) + '|' + state_label(row, false) + '|' + std::to_string(row.others) + '|' + (row.picked ? "1" : "0") + '|' + needs_label(row) + '\n';
    return put(text, out, size);
}

// 테스트: 그 기술(design 0) · 부대 설계(1)를 보유한 다른 나라의 이름(목록의 풍선말).
EXPORT int srtoybox_owner_names(int design, int id, char *out, int size)
{
    return put(game_owner_names(design != 0, id), out, size);
}

// 테스트: 병과 → 영역의 번호와 이름("0\t육군").
EXPORT int srtoybox_design_domain(int cls, char *out, int size)
{
    const int domain = design_domain(cls);
    return put(std::to_string(domain) + '\t' + (domain < 0 ? std::string() : domain_label(domain)), out, size);
}

// 테스트: 이름표. "기술의 이름\n분류의 이름\n병과의 이름\nraw(게임 메모리의 글)를 UTF-8 로 바꾼 글".
EXPORT int srtoybox_tech_names(int tech, int kind, int cls, const char *raw, char *out, int size)
{
    const std::string text = raw != nullptr ? raw : "";
    return put(tech_label(tech) + '\n' + tech_kind_label(kind) + '\n' + design_class_label(cls) + '\n' + game_text_to_utf8(text.data(), text.size()),
               out, size);
}

// 새 찾기(부르는 함수)를 그 이미지에 돌린다. 돌려주는 값은 찾은 함수의 비트. error 에는 함수마다 한 줄(찾았으면 빈 줄), rows 는 서명마다의 결과.
EXPORT int srtoybox_locate_acts(const unsigned char *image, unsigned long long size, ActLayout *out, char *error, int error_size,
                                char *rows, int rows_size)
{
    ActLayout found = {};
    SigRow table[ACT_WANTED * STATE_SIGS];
    char why[ACT_WANTED][MORE_WHY] = {};
    const int acts = locate_acts(image, static_cast<size_t>(size), &found, table, why);
    if (rows != nullptr)
        put(rows_text(table, ACT_WANTED * STATE_SIGS), rows, rows_size);
    std::string lines;
    for (int w = 0; w < ACT_WANTED; w++)
        lines += std::string(why[w]) + '\n';
    if (error != nullptr)
        put(lines, error, error_size);
    if (out != nullptr)
        *out = found;
    return acts;
}

// 새 찾기(배율)를 그 이미지에 돌린다. 돌려주는 값은 찾은 것의 비트. error 에는 찾을 것마다 한 줄(찾았으면 빈 줄), rows 는 서명마다의 결과.
EXPORT int srtoybox_locate_mult(const unsigned char *image, unsigned long long size, MultLayout *out, char *error, int error_size,
                                char *rows, int rows_size)
{
    MultLayout found = {};
    SigRow table[MULT_WANTED * STATE_SIGS];
    char why[MULT_WANTED][MORE_WHY] = {};
    const int mult = locate_mult(image, static_cast<size_t>(size), &found, table, why);
    if (rows != nullptr)
        put(rows_text(table, MULT_WANTED * STATE_SIGS), rows, rows_size);
    std::string lines;
    for (int w = 0; w < MULT_WANTED; w++)
        lines += std::string(why[w]) + '\n';
    if (error != nullptr)
        put(lines, error, error_size);
    if (out != nullptr)
        *out = found;
    return mult;
}

// 테스트: 이 프로세스의 "게임"에 부르는 함수의 자리를 준다(srtoybox_test_game 다음에 부른다). colonize 는 그 자리에 둘 함수.
EXPORT void srtoybox_test_acts(const ActLayout *layout, void *colonize, void *fight, void *become, void *elect, void *news, void *treaty,
                               void *annex, void *spawn, void *deploy)
{
    game_set_acts_for_test(layout, colonize, fight, become, elect, news, treaty, annex, spawn, deploy);
}

// 점령지 충성(game_loyalty): Wrote 의 정수. 바꾼 칸의 수, 쓴 충성 대상의 인덱스와 지역 번호.
EXPORT int srtoybox_loyalty(int *hexes, int *target, int *number)
{
    return static_cast<int>(game_loyalty(hexes, target, number));
}

EXPORT int srtoybox_loyalty_off(char *out, int size)
{
    return put(game_loyalty_off(), out, size);
}

// 테스트: 점령지 충성의 스냅숏 뒤 · 쓰기 전에 부를 함수.
EXPORT void srtoybox_test_loyalty_hook(void (*hook)())
{
    game_set_loyalty_hook_for_test(hook);
}

// 테스트: 게임의 지도에서 고른 지역의 번호(없으면 0).
EXPORT int srtoybox_map_pick(void)
{
    return game_map_pick();
}

// 테스트: 고른 나라를 식민지로 삼는 요청. 받았으면 1.
EXPORT int srtoybox_keeper_colonize(int region)
{
    return keeper_enqueue_colonize(region) ? 1 : 0;
}

EXPORT int srtoybox_act_off(int act, char *out, int size)
{
    return put(game_act_off(act), out, size);
}

// 연구의 표와 목록의 꼴(locate.h 의 상수): 한 줄에 "이름\t값(16진수)". 테스트가 서명에 박힌 바이트와 댄다.
EXPORT int srtoybox_research_shape(char *out, int size)
{
    return put(locate_research_shape(), out, size);
}

// 테스트: 연구의 규칙(research_plan)을 글로 준 표(tables_from)와 "무엇을"(what_from)에 돌린다. action: 0 완료, 1 미완료.
// can_house: 묶음이 없는 항목에 새 묶음을 걸 수 있는가(0 이면 그런 항목을 건너뛴다).
// 나오는 글은 여덟 줄이다: "techs 1 2 3" / "designs 5" / "nodes 0 1" / "asked <고른 기술의 수> <고른 설계의 수>" /
// "skipped <건너뛴 항목의 수>" / "queued t3 d5"(표에서 대기열에 있는 것으로 읽힌 항목) / "unlock <연 것> <이미 열림> <다른 지역> <연구 대상 아님> <특수> <이미 개량됨> <단종> <더한 지역 비트의 수> <더한 비트(10진)>" / "text <알림의 글>".
EXPORT int srtoybox_research_plan(const char *tables, int action, const char *what, int can_house, char *out, int size)
{
    const ResearchTables t = tables_from(tables != nullptr ? tables : "");
    const Research how = action == 0 ? Research::Complete : action == 2 ? Research::Unlock : Research::Revoke;
    const ResearchPlan plan = research_plan(t, how, what_from(what != nullptr ? what : ""), can_house != 0);
    std::string queued = "queued";
    for (const TechRow &row : t.techs)
        if (row.queued)
            queued += " t" + std::to_string(row.id);
    for (const DesignRow &row : t.designs)
        if (row.queued)
            queued += " d" + std::to_string(row.id);
    return put(numbers("techs", plan.techs) + numbers("designs", plan.designs) + numbers("nodes", plan.nodes)
               + "asked " + std::to_string(plan.asked_techs) + ' ' + std::to_string(plan.asked_designs) + "\nskipped "
               + std::to_string(plan.skipped) + '\n' + queued + "\nunlock " + std::to_string(plan.opened) + ' ' + std::to_string(plan.already) + ' '
               + std::to_string(plan.blocked[BLOCK_REGION]) + ' ' + std::to_string(plan.blocked[BLOCK_CLOSED]) + ' '
               + std::to_string(plan.blocked[BLOCK_SPECIAL]) + ' ' + std::to_string(plan.blocked[BLOCK_SUPERSEDED]) + ' '
               + std::to_string(plan.blocked[BLOCK_RETIRED]) + ' ' + std::to_string(plan.regions) + ' ' + std::to_string(plan.region_add)
               + "\ntext " + research_summary(plan, how), out, size);
}

// 테스트: write_research 가 지역 코드 칸을 쓰는 CAS(swap_qword).
EXPORT int srtoybox_swap_qword(unsigned long long cell, unsigned long long was, unsigned long long now)
{
    return swap_qword(cell, was, now);
}

// 두 묶음의 대조(locate_fits). 0 이면 맞는다. -1 이면 error 에 까닭.
EXPORT int srtoybox_locate_fits(const GameAddresses *state, const ValueLayout *values, char *error, int error_size)
{
    char why[160] = "";
    if (state == nullptr || values == nullptr)
        return -1;
    if (locate_fits(*state, *values, why, sizeof(why)))
        return 0;
    put(why, error, error_size);
    return -1;
}

EXPORT int srtoybox_stock_slots(void)
{
    return STOCK_SLOTS;
}

EXPORT unsigned srtoybox_function_root(const unsigned char *image, unsigned long long size, unsigned rva)
{
    return locate_function_root(image, static_cast<size_t>(size), rva);
}

// 테스트: 가짜 메모리에서 상태를 읽는다. "known=1 in_game=1 multiplayer=0 cheats=0 player=1499 regions=1106,1499"
EXPORT int srtoybox_game_state(const unsigned char *base, const GameAddresses *at, char *out, int size)
{
    if (at == nullptr)
        return -1;
    const GameState s = read_game(base, *at);
    std::string regions;
    for (int number : read_regions(base, *at))
        regions += (regions.empty() ? "" : ",") + std::to_string(number);
    return put("known=" + std::to_string(s.known) + " in_game=" + std::to_string(s.in_game) + " multiplayer="
               + std::to_string(s.multiplayer) + " player=" + std::to_string(s.player)
               + " regions=" + regions, out, size);
}

// 테스트: 이 프로세스의 "게임"을 가짜 메모리로 바꾼다(주소 찾기의 결과를 덮어쓴다).
EXPORT void srtoybox_test_game(const unsigned char *base, const GameAddresses *at)
{
    game_set_for_test(base, at);
}

// 테스트: 가짜 메모리에서 값을 읽는다. "ok=1 treasury=14430000000 used=100100010000 stock=1000,0,0,2500,…"
EXPORT int srtoybox_values_read(const unsigned char *base, const GameAddresses *at, const ValueLayout *layout, char *out, int size)
{
    if (at == nullptr || layout == nullptr)
        return -1;
    const GameValues v = read_values(base, *at, *layout);
    char number[40];
    snprintf(number, sizeof(number), "%.17g", v.treasury);
    std::string used, stock;
    for (int i = 0; i < STOCK_SLOTS; i++) {
        used += v.used[i] ? '1' : '0';
        char one[32];
        snprintf(one, sizeof(one), "%s%.9g", i == 0 ? "" : ",", static_cast<double>(v.stock[i]));
        stock += one;
    }
    return put("ok=" + std::to_string(v.ok) + " treasury=" + number + " used=" + used + " stock=" + stock, out, size);
}

// 테스트: 가짜 메모리에 쓴다. slot 이 -1 이면 국고, 아니면 그 칸의 재고. 돌려주는 값은 Wrote
// (0 썼다, 1 게임 밖, 2 쓰지 않는 물자, 3 쓸 수 없는 값이나 칸, 4 실패).
EXPORT int srtoybox_values_write(const unsigned char *base, const GameAddresses *at, const ValueLayout *layout, int slot, double value)
{
    if (at == nullptr || layout == nullptr)
        return -1;
    return static_cast<int>(slot == -1 ? write_treasury(base, *at, *layout, value)
                                       : write_stock(base, *at, *layout, slot, static_cast<float>(value)));
}

// 테스트: 이 프로세스의 "게임"에 값의 자리를 준다(srtoybox_test_game 다음에 부른다). nullptr 이면 못 찾은 것으로.
EXPORT void srtoybox_test_values(const ValueLayout *layout)
{
    game_set_values_for_test(layout);
}

// 테스트: 가짜 메모리에서 더 쓰는 값을 읽는다. "ok=1 tech=130 opinion=0.5,0.25,0.75"
EXPORT int srtoybox_more_read(const unsigned char *base, const GameAddresses *at, const MoreLayout *more, char *out, int size)
{
    if (at == nullptr || more == nullptr)
        return -1;
    const GameMore v = read_more(base, *at, *more);
    char line[320];
    snprintf(line, sizeof(line), "ok=%d tech=%.9g opinion=%.9g,%.9g,%.9g people=%.9g,%.9g approval=%.9g", v.ok ? 1 : 0,
             static_cast<double>(v.tech), static_cast<double>(v.opinion[0]), static_cast<double>(v.opinion[1]),
             static_cast<double>(v.opinion[2]), static_cast<double>(v.people[0]), static_cast<double>(v.people[1]),
             static_cast<double>(v.approval));
    return put(line, out, size);
}

// 테스트: 가짜 메모리에서 그 번호의 나라와의 관계를 읽는다. "ok=1 mine=0.5,0.25,0 theirs=-1,0,1"(관계 표 둘, 전쟁 명분)
EXPORT int srtoybox_relation_read(const unsigned char *base, const GameAddresses *at, const MoreLayout *more, int number, char *out,
                                  int size)
{
    if (at == nullptr || more == nullptr)
        return -1;
    const Relation r = read_relation(base, *at, *more, number);
    char line[200];
    snprintf(line, sizeof(line), "ok=%d mine=%.9g,%.9g,%.9g theirs=%.9g,%.9g,%.9g", r.ok ? 1 : 0, static_cast<double>(r.mine[0]),
             static_cast<double>(r.mine[1]), static_cast<double>(r.mine[2]), static_cast<double>(r.theirs[0]),
             static_cast<double>(r.theirs[1]), static_cast<double>(r.theirs[2]));
    return put(line, out, size);
}

// 테스트: 가짜 메모리에 더 쓰는 값을 쓴다. what: 0 기술 수준(value 로), 1 세계 시장 여론(최고), 2 관계(number 의 나라와 value 로).
// 돌려주는 값은 Wrote(…, 5 꺼져 있다 · 자리를 모른다, 6 그 나라가 없다). done 에 쓴 칸의 수.
EXPORT int srtoybox_more_write(const unsigned char *base, const GameAddresses *at, const MoreLayout *more, int what, int number,
                               double value, int *done)
{
    int cells = 0;
    Wrote wrote = Wrote::BadValue;
    if (at == nullptr || more == nullptr || what < 0 || what > 4)
        return -1;
    if (what == 0) {
        wrote = write_tech(base, *at, *more, static_cast<float>(value));
        cells = wrote == Wrote::Done ? 1 : 0;
    } else if (what == 1) {
        wrote = write_opinion(base, *at, *more, &cells);
    } else if (what == 3) {                    // 인구 칸과 풀 칸에 value 를 더한다
        wrote = write_people(base, *at, *more, static_cast<float>(value), &cells);
    } else if (what == 4) {                    // 지지율 100%
        wrote = write_approval(base, *at, *more);
        cells = wrote == Wrote::Done ? 1 : 0;
    } else {
        wrote = write_relation(base, *at, *more, number, static_cast<float>(value), &cells);
    }
    if (done != nullptr)
        *done = cells;
    return static_cast<int>(wrote);
}

// 테스트: 이 프로세스의 "게임"에 더 쓰는 값의 자리를 준다(srtoybox_test_game 다음에 부른다). 자리가 0 인 묶음 · nullptr 은 못 찾은 것으로.
EXPORT void srtoybox_test_more(const MoreLayout *layout)
{
    game_set_more_for_test(layout);
}

// 더 쓰는 값의 묶음(1 지식, 2 여론, 4 관계)을 쓸 수 없는 까닭. 쓸 수 있으면 빈 글.
EXPORT int srtoybox_more_off(int group, char *out, int size)
{
    return put(game_more_off(group), out, size);
}

// 테스트: 가짜 메모리에서 연구의 표를 읽는다(read_research). 읽었으면 1 과 표의 글, 못 읽었으면 0 과 까닭.
// 표의 글은 한 줄에 하나: "slots <기술 자리 수> <설계 자리 수>" /
//   "t <번호> <분류> <수준> <선행 0> <선행 1> <보유> <고른 나라가 보유> <다른 나라의 수> <대기열에 있다> <묶음이 있다>" /
//   "d <번호> <병과> <연도> <연구 대상> <게임이 건너뛰는 설계> <보유> <고른 나라가 보유> <다른 나라의 수> <대기열에 있다> <묶음이 있다> <선행 넷>" /
//   "q <종류> <번호> <깃발 0(16진수)> <깃발 1(16진수)>"
EXPORT int srtoybox_research_read(const unsigned char *base, const GameAddresses *at, const ResearchLayout *layout, int picked, char *out,
                                  int size)
{
    if (at == nullptr || layout == nullptr)
        return -1;
    ResearchTables t;
    std::string why;
    if (!read_research(base, *at, *layout, picked, &t, &why))
        return put(why, out, size) < 0 ? -1 : 0;
    std::ostringstream text;
    text << "slots " << t.tech_slots << ' ' << t.design_slots << '\n';
    for (const TechRow &row : t.techs)
        text << "t " << row.id << ' ' << row.kind << ' ' << row.level << ' ' << row.needs[0] << ' ' << row.needs[1] << ' ' << row.mine
             << ' ' << row.picked << ' ' << row.others << ' ' << row.queued << ' ' << row.housed << '\n';
    for (const DesignRow &row : t.designs)
        text << "d " << row.id << ' ' << row.cls << ' ' << row.year << ' ' << row.open << ' ' << row.held << ' ' << row.mine << ' '
             << row.picked << ' ' << row.others << ' ' << row.queued << ' ' << row.housed << ' ' << row.needs[0] << ' ' << row.needs[1]
             << ' ' << row.needs[2] << ' ' << row.needs[3] << '\n';
    for (const QueueRow &row : t.queue)
        text << "q " << row.kind << ' ' << row.id << ' ' << std::hex << row.flags[0] << ' ' << row.flags[1] << std::dec << '\n';
    return put(text.str(), out, size) < 0 ? -1 : 1;
}

// 테스트: 가짜 메모리의 연구를 완료(action 0) · 미완료(1)로 바꾼다(write_research). what 은 what_from 의 글,
// recompute 는 "다시 셈" 자리에 둘 함수(없으면 nullptr). 돌려주는 값은 Wrote. out 에는 한 줄에 하나:
// "techs …" / "designs …" / "nodes …" / "asked <기술> <설계>" / "skipped N" / "housed N" / "cells N" / "written N" /
// "recomputed 0|1" / "code <예외 코드(16진수)>" / "why <까닭>" / "text <알림의 글>"
EXPORT int srtoybox_research_write(const unsigned char *base, const GameAddresses *at, const ResearchLayout *layout, void *recompute,
                                   int action, const char *what, char *out, int size)
{
    if (at == nullptr || layout == nullptr)
        return -1;
    const Research how = action == 0 ? Research::Complete : action == 2 ? Research::Unlock : Research::Revoke;
    ResearchDone done;
    const Wrote wrote = write_research(base, *at, *layout, reinterpret_cast<Recompute>(recompute), how,
                                       what_from(what != nullptr ? what : ""), &done);
    std::ostringstream text;
    text << numbers("techs", done.plan.techs) << numbers("designs", done.plan.designs) << numbers("nodes", done.plan.nodes) << "asked "
         << done.plan.asked_techs << ' ' << done.plan.asked_designs << "\nskipped " << done.skipped << "\nhoused " << done.housed
         << "\ncells " << done.cells << "\nwritten " << done.written << "\nrecomputed " << done.recomputed << "\ncode " << std::hex
         << done.code << "\nwhy " << done.why << "\ntext " << research_summary(done.plan, how);
    put(text.str(), out, size);
    return static_cast<int>(wrote);
}

// 테스트: 포인터 칸에 새 묶음을 건다(hang_owners). 0 걸었다 / 1 이미 걸려 있었다 / -1 쓸 수 없다. *owners 는 그 칸에 걸려 있는 묶음.
EXPORT int srtoybox_research_hang(void *cell, unsigned long long block, unsigned long long *owners)
{
    uint64_t found = owners != nullptr ? *owners : 0;
    const int taken = hang_owners(reinterpret_cast<uint64_t>(cell), block, &found);
    if (owners != nullptr)
        *owners = found;
    return taken;
}

// 테스트: 이 프로세스의 "게임"에 연구의 자리와 "다시 셈" 자리에 둘 함수를 준다(srtoybox_test_game 다음에 부른다). nullptr 이면 못 찾은 것으로.
EXPORT void srtoybox_test_research(const ResearchLayout *layout, void *recompute)
{
    game_set_research_for_test(layout, recompute);
}

// 연구를 쓸 수 없는 까닭. 쓸 수 있으면 빈 글.
EXPORT int srtoybox_research_off(char *out, int size)
{
    return put(game_research_off(), out, size);
}

// 테스트: 게임이 뜰 때의 찾기(game_init_from)를 그 이미지에 돌린다. 이미지는 srtoybox_test_game(nullptr, …) 로 비울 때까지 살아 있어야 한다.
EXPORT void srtoybox_test_init(const unsigned char *image, unsigned long long size)
{
    game_init_from(image, static_cast<size_t>(size));
}

// 테스트: 이 프로세스의 "게임"에 대해 아는 것. 비트 1 = 상태를 읽는다, 4 = 값(국고 · 재고)을 쓸 수 있다,
// 8 = 기술 수준을, 16 = 세계 시장 여론을, 32 = 관계를, 64 = 연구를 쓸 수 있다.
EXPORT int srtoybox_game_flags(void)
{
    return (game_reads() ? 1 : 0) | (game_values_off().empty() ? 4 : 0)
        | (game_more_off(MORE_TECH).empty() ? 8 : 0) | (game_more_off(MORE_OPINION).empty() ? 16 : 0)
        | (game_more_off(MORE_RELATIONS).empty() ? 32 : 0) | (game_research_off().empty() ? 64 : 0)
        | (game_more_off(MORE_PEOPLE).empty() ? 128 : 0) | (game_more_off(MORE_APPROVAL).empty() ? 256 : 0)    // 인구 · 지지율
        | (game_act_off(ACT_COLONIZE).empty() ? 512 : 0);                                                      // 식민지화(게임의 함수)
}

EXPORT int srtoybox_values_off(char *out, int size)
{
    return put(game_values_off(), out, size);
}

// 테스트: 값 쓰기 요청(keeper.h). slot 이 -1 이면 국고, -2 면 쓰는 물자 모두. change: 0 더하기, 1 이 값으로, 2 바닥. 받았으면 1.
EXPORT int srtoybox_keeper_request(int slot, int change, double amount)
{
    if (change < 0 || change > 2)
        return -1;
    return keeper_enqueue({slot, static_cast<Change>(change), amount}) ? 1 : 0;
}

// 테스트: 더 쓰는 값의 요청. slot: -3 기술 수준 +1, -4 세계 시장 여론 최고, -5 region 의 나라와의 관계를 amount(1 최고, 0 중립)로.
// 받았으면 1. 그런 요청이 없으면 -1.
EXPORT int srtoybox_keeper_more(int slot, int region, double amount)
{
    if (slot != TECH && slot != OPINION && slot != RELATION && slot != PEOPLE && slot != APPROVAL)
        return -1;
    return keeper_enqueue({slot, Change::Set, amount, region}) ? 1 : 0;
}

// 테스트: 연구 요청(keeper.h). action: 0 완료, 1 미완료, 2 연구 가능으로. what: "items t1 d5" · "level 120" · "queue"(what_from).
// label: 알림과 로그에 적을 이름. 받았으면 1, 받지 못했으면 0(가득 찼다 · 연구를 쓸 수 없다). 그런 요청이 없으면 -1.
EXPORT int srtoybox_keeper_research(int action, const char *what, const char *label)
{
    if (action < 0 || action > 2 || what == nullptr || label == nullptr)
        return -1;
    return keeper_enqueue_research({action == 0 ? Research::Complete : action == 2 ? Research::Unlock : Research::Revoke, what_from(what), label}) ? 1 : 0;
}

EXPORT void srtoybox_keeper_tick(void)
{
    keeper_tick(GetTickCount64());
}

// 테스트: 가짜 시계로 틱(최소 유지는 KEEP_EVERY_MS 마다 본다).
EXPORT void srtoybox_keeper_tick_at(unsigned long long now_ms)
{
    keeper_tick(now_ms);
}

// 테스트: 최소 유지의 한 항목을 켜고 끈다. slot 이 -1 이면 국고(floor 는 달러), 0 … 11 이면 그 칸의 물자(수량). 없는 칸이면 -1.
EXPORT int srtoybox_keeper_keep(int slot, int on, double floor)
{
    Keep keep = keeper_keep();
    if (slot == TREASURY) {
        keep.treasury = on != 0;
        keep.treasury_floor = floor;
    } else if (slot >= 0 && slot < STOCK_SLOTS) {
        keep.stock[slot] = on != 0;
        keep.stock_floor[slot] = floor;
    } else {
        return -1;
    }
    keeper_set_keep(keep);
    return 0;
}

// 테스트: 상태 줄에 붙는 글. used 가 '0' · '1' 열두 글자면 게임 안의 글(그 물자들을 쓰는 판), nullptr 이면 쉬는 동안의 글(까닭 why).
EXPORT int srtoybox_keep_text(const char *used, const char *why, char *out, int size)
{
    const Keep keep = keeper_keep();
    if (used == nullptr)
        return put(keep_resting_text(keep, why == nullptr ? "" : why), out, size);
    bool flags[STOCK_SLOTS] = {};
    for (int slot = 0; slot < STOCK_SLOTS && used[slot] != '\0'; slot++)
        flags[slot] = used[slot] == '1';
    return put(keep_active_text(keep, flags), out, size);
}

// 테스트: "<마지막으로 쓴 것>\t<알림>".
EXPORT int srtoybox_keeper_text(char *out, int size)
{
    return put(keeper_last() + '\t' + keeper_notice(), out, size);
}

// 테스트: keeper 와, 그것이 함께 쓰는 실행기의 오류 가드 · "게임의 함수 안" 깃발을 지운다.
// 테스트: 이 프로세스의 "게임"에 배율의 자리를 준다(srtoybox_test_game 다음에 부른다).
EXPORT void srtoybox_test_mult(const MultLayout *layout)
{
    game_set_mult_for_test(layout);
}

// 배율의 그 부분(MULT_PART_*)을 쓸 수 없는 까닭. 쓸 수 있으면 빈 글.
EXPORT int srtoybox_mult_off(int part, char *out, int size)
{
    return put(game_mult_off(part), out, size);
}

// 테스트: 배율의 설정을 고친다. spec 은 빈칸으로 나눈 "키=값"들 — on · tax · defense · social · research · unitbuild · facbuild · out.<칸> · use.<칸> · raw.<칸>.
// 적지 않은 것은 그대로다. 모르는 키가 있으면 -1(아무것도 바꾸지 않는다).
EXPORT int srtoybox_keeper_mult(const char *spec)
{
    Mult m = keeper_mult();
    std::string text = spec == nullptr ? "" : spec;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(' ', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string pair = text.substr(pos, end - pos);
        pos = end + 1;
        if (pair.empty())
            continue;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos)
            return -1;
        const std::string key = pair.substr(0, eq);
        const int n = atoi(pair.c_str() + eq + 1);
        if (key == "on")
            m.on = n != 0;
        else if (key == "tax")
            m.tax = n;
        else if (key == "defense")
            m.defense = n;
        else if (key == "social")
            m.social = n;
        else if (key == "research")
            m.research = n;
        else if (key == "unitbuild")
            m.unitbuild = n;
        else if (key == "facbuild")
            m.facbuild = n;
        else if (key.size() > 4 && key[3] == '.' && atoi(key.c_str() + 4) >= 0 && atoi(key.c_str() + 4) < MULT_GOODS
                 && (key.compare(0, 3, "out") == 0 || key.compare(0, 3, "use") == 0 || key.compare(0, 3, "raw") == 0))
            (key[0] == 'o' ? m.out : key[0] == 'u' ? m.use : m.raw)[atoi(key.c_str() + 4)] = n;
        else
            return -1;
    }
    keeper_set_mult(m);
    return 0;
}

// 테스트: 예비 인원의 최소 유지를 켜고 끈다.
// 점령지 충성의 유지(켜면 바로 한 번, 그 뒤 5초마다).
EXPORT void srtoybox_keeper_loyalty(int on)
{
    keeper_set_loyalty_keep(on != 0);
}

// 지지율 · 여론의 최소 유지: which 는 Floor(0 국민 지지율 · 1 군부 지지율 · 2 세계 시장 여론), percent 는 0 … 100.
EXPORT void srtoybox_keeper_floor(int which, int on, int percent)
{
    if (which >= 0 && which < FLOOR_COUNT)
        keeper_set_floor_keep(static_cast<Floor>(which), on != 0, percent);
}

EXPORT int srtoybox_floor_off(int which, char *out, int size)
{
    return put(which >= 0 && which < FLOOR_COUNT ? game_floor_off(static_cast<Floor>(which)) : std::string("없는 칸입니다"), out, size);
}

EXPORT void srtoybox_keeper_reserve(int on, double value)
{
    keeper_set_reserve_keep(on != 0, static_cast<long long>(value));
}

// 테스트: 사회의 설정을 고친다. spec 은 빈칸으로 나눈 "키=값"들 — on · up.<i> · down.<i> · keep.<i> · keepv.<i>.
// 적지 않은 것은 그대로다. 모르는 키가 있으면 -1(아무것도 바꾸지 않는다).
EXPORT int srtoybox_keeper_social(const char *spec)
{
    Social s = keeper_social();
    std::string text = spec == nullptr ? "" : spec;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(' ', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string pair = text.substr(pos, end - pos);
        pos = end + 1;
        if (pair.empty())
            continue;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos)
            return -1;
        const std::string key = pair.substr(0, eq);
        const int n = atoi(pair.c_str() + eq + 1);
        const size_t dot = key.find('.');
        const int i = dot == std::string::npos ? -1 : atoi(key.c_str() + dot + 1);
        const std::string what = key.substr(0, dot);
        if (key == "on")
            s.on = n != 0;
        else if (i < 0 || i >= SOCIAL_COUNT)
            return -1;
        else if (what == "up")
            s.up[i] = n;
        else if (what == "down")
            s.down[i] = n;
        else if (what == "keep")
            s.keep[i] = n != 0;
        else if (what == "keepv")
            s.keep_value[i] = n;
        else
            return -1;
    }
    keeper_set_social(s);
    return 0;
}

// 테스트: "<날마다의 일을 한 날수>\t<곱 보정 칸을 쓴 횟수>\t<지금 ToyBox 의 값이 든 칸의 수>\t<100 이 아닌 배율의 수>".
// 테스트: 다음 자정에 완료를 누른다. 까닭의 글(되면 빈 글).
EXPORT int srtoybox_keeper_rush(char *out, int size)
{
    return put(keeper_rush(), out, size);
}

// 테스트: "이 나라로 플레이" 직전의 되돌리기.
EXPORT void srtoybox_mult_put_back(void)
{
    keeper_mult_put_back();
}

EXPORT int srtoybox_rush_pending(void)
{
    return keeper_rush_pending() ? 1 : 0;
}

EXPORT int srtoybox_mult_stats(char *out, int size)
{
    const MultStats s = keeper_mult_stats();
    return put(std::to_string(s.days) + "\t" + std::to_string(s.writes) + "\t" + std::to_string(s.held) + "\t"
               + std::to_string(mult_count(keeper_mult())), out, size);
}

EXPORT void srtoybox_keeper_reset(void)
{
    keeper_reset_for_test();
    runner_reset_for_test();
}

// 테스트: "<오류 가드가 걸렸는가 0/1>\t<게임의 함수 안인가 0/1>\t<실행기의 알림>".
EXPORT int srtoybox_runner_text(char *out, int size)
{
    return put(std::string(runner_faulted() ? "1" : "0") + '\t' + (runner_calling() ? "1" : "0") + '\t' + runner_notice(), out, size);
}

EXPORT int srtoybox_product_label(int slot, char *out, int size)
{
    return put(product_label(slot), out, size);
}

// 물자 이름표의 한 줄: "<한글 이름>\t<영문 이름>". 표에 없는 칸이면 -1.
EXPORT int srtoybox_product_names(int slot, char *out, int size)
{
    const ProductName *name = find_product(slot);
    return name == nullptr ? -1 : put(std::string(name->ko) + '\t' + name->en, out, size);
}

EXPORT int srtoybox_region_label(int number, char *out, int size)
{
    return put(region_label(number), out, size);
}

// 테스트: 게임이 지금 판에서 쓰는 지역 이름을 준다. spec 은 줄마다 "<번호>\t<이름>"(UTF-8). 빈 글이면 지운다(이름표만 쓴다).
EXPORT void srtoybox_test_region_names(const char *spec)
{
    std::vector<std::pair<int, std::string>> names;
    std::string text = spec == nullptr ? "" : spec;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos)
            names.push_back({atoi(line.c_str()), line.substr(tab + 1)});
    }
    regions_set_game_names(names);
}

// 이번 판의 나라마다 게임이 쓰는 이름(game_region_names): 줄마다 "<번호>\t<이름>", 번호순.
EXPORT int srtoybox_region_names(char *out, int size)
{
    std::vector<std::pair<int, std::string>> names = game_region_names();
    std::sort(names.begin(), names.end());
    std::string text;
    for (const auto &row : names)
        text += std::to_string(row.first) + '\t' + row.second + '\n';
    return put(text, out, size);
}

// 테스트: 창의 나라 목록. 첫 줄 "picked=<번호>", 이어서 한 줄에 "<번호>\t<이름>".
EXPORT int srtoybox_region_view(const int *numbers, int count, int player, int picked, const char *filter, char *out, int size)
{
    const std::vector<int> all(numbers, numbers + (numbers == nullptr || count < 0 ? 0 : count));
    const RegionView view = region_view(all, player, picked, filter);
    std::string text = "picked=" + std::to_string(view.picked) + '\n';
    for (int number : view.rows)
        text += std::to_string(number) + '\t' + region_label(number) + '\n';
    return put(text, out, size);
}

EXPORT unsigned srtoybox_dbcs(unsigned char lead, unsigned char trail, unsigned codepage)
{
    return dbcs_combine(lead, trail, codepage);
}

// 테스트: 설정 창이 지난 프레임에 그린 것들(ui.h 의 ui_report).
EXPORT int srtoybox_ui_report(char *out, int size)
{
    return put(ui_report(), out, size);
}

EXPORT int srtoybox_hotkey_name(int vk, int mods, char *out, int size)
{
    return put(hotkey_name(vk, mods), out, size);
}

// 테스트: 값 계산(values.h). stock 이 0 이면 국고, 아니면 재고. change: 0 더하기, 1 이 값으로, 2 바닥.
// 돌려주는 값은 Verdict: 0 쓴다(*out 에 쓸 값), 1 바꿀 것이 없다, 2 쓰지 않는다. 인자가 틀리면 -1.
EXPORT int srtoybox_value(int stock, double now, int change, double amount, double *out)
{
    if (change < 0 || change > 2 || out == nullptr)
        return -1;
    double next = 0;
    float next_stock = 0;       // (small 은 windows.h 가 매크로로 쓴다)
    const Verdict verdict = stock != 0 ? stock_value(static_cast<float>(now), static_cast<Change>(change), amount, &next_stock)
                                       : treasury_value(now, static_cast<Change>(change), amount, &next);
    if (verdict == Verdict::Write)
        *out = stock != 0 ? static_cast<double>(next_stock) : next;
    return static_cast<int>(verdict);
}

EXPORT int srtoybox_short_number(double value, char *out, int size)
{
    return put(short_number(value), out, size);
}

EXPORT int srtoybox_short_amount(double value, char *out, int size)
{
    return put(short_amount(value), out, size);
}


// srhook 이 이 DLL 을 불러온 뒤 한 번 부른다(불러온 스레드에서). 게임의 창은 기다리지 않는다 — 첫 Present 에서 얻는다.
EXPORT void WINAPI srtoybox_start(void)
{
    log_line("시작 (프로세스 %lu)", GetCurrentProcessId());
    ui_init();
    game_init();          // 화면에 끼어들기 전에 끝낸다 — 창이 뜰 때는 게임을 읽을 수 있는지가 이미 정해져 있다
    overlay_install();
}
