// 창에서 바꾸는 설정: 여닫는 단축키, 돈 탭의 입력란, 국고와 물자의 최소 유지, 기능마다 마지막에 넣은 값.
// 글 형식은 한 줄에 "키=값". 모르는 줄은 버리고 틀린 값은 기본값으로 돌린다.
#pragma once

#include <map>
#include <string>

#include "locate.h"

const int HOTKEY_CTRL = 1, HOTKEY_SHIFT = 2, HOTKEY_ALT = 4;
const long long MONEY_AMOUNT_MIN = 1, MONEY_AMOUNT_MAX = 1000000, MONEY_AMOUNT_DEFAULT = 10000;   // 돈 탭의 입력란(백만 달러)
const long long KEEP_TREASURY_MAX = 1000000;       // 유지할 국고(백만 달러): 0 ~ 1,000,000
const long long KEEP_STOCK_MAX = 1000000000;       // 유지할 물자의 수량: 0 ~ 1,000,000,000
const long long RESERVE_LIMIT = 100000000;         // 예비 인원의 유지 값 · 입력란의 끝(명). keeper.h 의 RESERVE_KEEP_MAX 와 같다
const int SETTINGS_SOCIAL_RATE_MAX = 10000, SETTINGS_SOCIAL_KEEP_MAX = 999;   // keeper.h 의 SOCIAL_RATE_MAX · SOCIAL_KEEP_MAX 와 같다
const int SETTINGS_MULT_FAST_MAX = 10000, SETTINGS_MULT_BUILD_MAX = 2000, SETTINGS_MULT_GOODS_MAX = 1000;   // 배율의 끝(퍼센트). keeper.h 의 MULT_*_MAX 와 같다
const int SETTINGS_MULT_CELL_MIN = 1, SETTINGS_MULT_RESEARCH_MIN = 10;   // 〃 MULT_CELL_MIN · MULT_RESEARCH_MIN

struct Settings {
    int hotkey_vk = 0x54;                            // T
    int hotkey_mods = HOTKEY_CTRL | HOTKEY_SHIFT;
    long long money_amount = MONEY_AMOUNT_DEFAULT;   // 돈 탭의 입력란. 파일의 줄은 "money.amount"
    bool keep_treasury = false;                      // 국고의 최소 유지. "keep.treasury"(0 / 1)
    long long keep_treasury_value = 0;               // 그 금액(백만 달러). "keep.treasury.value"
    bool keep_stock[STOCK_SLOTS] = {};               // 물자마다의 최소 유지. "keep.stock.<칸>"(0 / 1)
    long long keep_stock_value[STOCK_SLOTS] = {};    // 그 수량. "keep.stock.<칸>.value"
    std::map<std::string, long long> values;         // 값이 있는 기능의 id → 값
    // 배율(keeper.h 의 Mult — 퍼센트, 100 이 "그대로"). "mult.on"(0 / 1), "mult.tax" · "mult.defense" · "mult.social" · "mult.research" · "mult.unitbuild" · "mult.facbuild",
    // 물자마다 "mult.out.<칸>" · "mult.use.<칸>" · "mult.raw.<칸>"
    bool mult_on = false;
    int mult_tax = 100, mult_defense = 100, mult_social = 100, mult_research = 100;
    int mult_unitbuild = 100, mult_facbuild = 100;   // 유닛 생산 · 시설 건설 속도(곱 보정 칸 15 · 16)
    int mult_out[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    int mult_use[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    int mult_raw[MULT_GOODS] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    // 사회(keeper.h 의 Social). "social.on"(0 / 1), 항목마다 "social.up.<i>" · "social.down.<i>"(퍼센트 0 … 10000),
    // "social.keep.<i>"(0 / 1) · "social.keep.<i>.value"(퍼센트 0 … 999)
    // 예비 인원(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md): "keep.reserve"(0 / 1) · "keep.reserve.value"(명, 0 … 1억), 단추의 입력란 "reserve.add"(명, 1 … 1억)
    bool keep_reserve = false;
    long long keep_reserve_value = 0;
    long long reserve_amount = 1000000;
    // 지지율 · 여론의 최소 유지(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md): "keep.civapproval" · "keep.milapproval" · "keep.opinion"(0 / 1)과
    // 그 ".value"(퍼센트 0 … 100). 차례는 Floor(game.h) 와 같다
    bool keep_floor[3] = {};
    bool keep_loyalty = false;          // 점령지 충성의 유지: "keep.loyalty"(0 / 1)
    int keep_floor_value[3] = {};
    bool social_on = false;
    int social_up[SOCIAL_COUNT] = {100, 100, 100, 100, 100, 100, 100, 100};
    int social_down[SOCIAL_COUNT] = {100, 100, 100, 100, 100, 100, 100, 100};
    bool social_keep[SOCIAL_COUNT] = {};
    int social_keep_value[SOCIAL_COUNT] = {};
};

Settings default_settings();
Settings parse_settings(const std::string &ini);
std::string format_settings(const Settings &s);
bool valid_hotkey(int vk, int mods);                 // 수정키만으로는 안 되고, ESC 만 누른 것은 취소다
std::string hotkey_name(int vk, int mods);           // "Ctrl+Shift+T"

// 파일: %APPDATA%\SR2030ToyBox\ (환경 변수 SRTOYBOX_HOME 이 있으면 그 폴더). 게임 폴더와 문서 폴더에는 쓰지 않는다.
std::wstring settings_dir();                         // 없으면 만든다. 구하지 못하면 빈 글
std::string settings_dir_text();                     // 창에 보일 그 폴더(UTF-8, 역슬래시는 / — 한글 글꼴에서 \ 가 ₩ 로 보인다)
Settings load_settings();                            // 파일이 없거나 깨져 있으면 기본값
bool save_settings(const Settings &s);
