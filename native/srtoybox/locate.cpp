#include "locate.h"

#include <excpt.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "sigs.h"

namespace {

struct Section {
    uint32_t rva, size;
    bool code;        // 실행 구역
    bool constant;    // 읽기 전용 자료(문자열이 여기 있다. 쓸 수 있는 구역에는 사용자가 친 글이 남을 수 있어 닻을 찾지 않는다)
    bool writable;    // 쓸 수 있는 자료(전역 변수가 여기 있다)
};

struct Image {
    const uint8_t *p;
    size_t size;
    Section sections[32];
    int count;
    uint32_t pdata, pdata_size;

    bool has(uint64_t rva, uint64_t n) const { return rva + n <= size; }
    uint16_t u16(uint32_t rva) const { uint16_t v; memcpy(&v, p + rva, sizeof(v)); return v; }
    uint32_t u32(uint32_t rva) const { uint32_t v; memcpy(&v, p + rva, sizeof(v)); return v; }
    int32_t i32(uint32_t rva) const { int32_t v; memcpy(&v, p + rva, sizeof(v)); return v; }
};

bool parse(Image &im)
{
    if (!im.has(0, 0x40) || im.u16(0) != 0x5A4D)                        // "MZ"
        return false;
    const uint32_t nt = im.u32(0x3C);
    if (!im.has(nt, 24 + 112 + 16 * 8) || im.u32(nt) != 0x00004550)     // "PE\0\0"
        return false;
    const uint32_t sections = im.u16(nt + 6), optional_size = im.u16(nt + 20), optional = nt + 24;
    if (im.u16(optional) != 0x20B || optional_size < 112 + 4 * 8)       // PE32+
        return false;
    im.pdata = im.u32(optional + 112 + 3 * 8);                          // 예외 디렉터리 = 함수 표(.pdata)
    im.pdata_size = im.u32(optional + 112 + 3 * 8 + 4);
    if (im.pdata_size < 12 || !im.has(im.pdata, im.pdata_size))
        return false;
    im.count = 0;
    for (uint32_t i = 0; i < sections && im.count < 32; i++) {
        const uint64_t header = static_cast<uint64_t>(optional) + optional_size + 40ull * i;
        if (!im.has(header, 40))
            return false;
        const uint32_t h = static_cast<uint32_t>(header), flags = im.u32(h + 36);
        Section s;
        s.size = im.u32(h + 8);
        s.rva = im.u32(h + 12);
        s.code = (flags & 0x20000000) != 0;                             // IMAGE_SCN_MEM_EXECUTE
        s.constant = !s.code && (flags & 0x80000000) == 0;              // IMAGE_SCN_MEM_WRITE 가 아니다
        s.writable = !s.code && (flags & 0x80000000) != 0;
        if (s.size == 0)
            continue;
        if (!im.has(s.rva, s.size))
            return false;
        im.sections[im.count++] = s;
    }
    return im.count > 0;
}

// 서명: 16진수 바이트와 '?'(아무 바이트)를 빈칸으로 나눠 적는다. 예: "48 8D 0D ? ? ? ?"
struct Pattern {
    uint8_t bytes[40];
    bool any[40];
    uint32_t length;
};

Pattern pattern(const char *text)
{
    Pattern p = {};
    for (const char *c = text; *c != '\0' && p.length < 40; ) {
        if (*c == ' ') {
            c++;
        } else if (*c == '?') {
            p.any[p.length++] = true;
            c++;
        } else {
            unsigned value = 0;
            for (int i = 0; i < 2; i++, c++)
                value = value * 16 + static_cast<unsigned>(*c <= '9' ? *c - '0' : (*c | 0x20) - 'a' + 10);
            p.bytes[p.length++] = static_cast<uint8_t>(value);
        }
    }
    return p;
}

bool matches(const Image &im, uint64_t rva, const Pattern &pt)
{
    if (!im.has(rva, pt.length))
        return false;
    for (uint32_t i = 0; i < pt.length; i++)
        if (!pt.any[i] && im.p[rva + i] != pt.bytes[i])
            return false;
    return true;
}

// [from, to) 에서 처음 맞는 자리. 없으면 0.
uint32_t find(const Image &im, uint32_t from, uint32_t to, const Pattern &pt)
{
    for (uint64_t rva = from; rva < to; rva++)
        if (matches(im, rva, pt))
            return static_cast<uint32_t>(rva);
    return 0;
}

// RIP 상대 주소가 든 명령이 가리키는 곳. at: 명령의 시작, disp: 변위의 자리, length: 명령의 길이.
uint32_t target(const Image &im, uint32_t at, uint32_t disp, uint32_t length)
{
    return static_cast<uint32_t>(static_cast<int64_t>(at) + length + im.i32(at + disp));
}

// 읽기 전용 자료에서 NUL 로 끝나는 글의 수. first 에 가장 앞의 것.
int count_string(const Image &im, const char *text, uint32_t *first)
{
    const uint32_t length = static_cast<uint32_t>(strlen(text)) + 1;
    int n = 0;
    for (int s = 0; s < im.count; s++) {
        const Section &sec = im.sections[s];
        if (!sec.constant || sec.size < length)
            continue;
        for (uint32_t rva = sec.rva; rva <= sec.rva + sec.size - length; rva++)
            if (im.p[rva] == static_cast<uint8_t>(text[0]) && memcmp(im.p + rva, text, length) == 0 && n++ == 0)
                *first = rva;
    }
    return n;
}

// where 를 가리키는 lea r64,[rip+d] (48|4C 8D 05|0D|…|3D d32)의 수. first 에 가장 앞의 것.
int count_lea_to(const Image &im, uint32_t where, uint32_t *first)
{
    int n = 0;
    for (int s = 0; s < im.count; s++) {
        const Section &sec = im.sections[s];
        if (!sec.code || sec.size < 7)
            continue;
        for (uint32_t rva = sec.rva; rva <= sec.rva + sec.size - 7; rva++)
            if ((im.p[rva] == 0x48 || im.p[rva] == 0x4C) && im.p[rva + 1] == 0x8D && (im.p[rva + 2] & 0xC7) == 0x05
                && target(im, rva, 3, 7) == where && n++ == 0)
                *first = rva;
    }
    return n;
}

// where 를 부르는 call rel32 (E8 d32)의 수. first 에 가장 앞의 것.
int count_calls_to(const Image &im, uint32_t where, uint32_t *first)
{
    int n = 0;
    for (int s = 0; s < im.count; s++) {
        const Section &sec = im.sections[s];
        if (!sec.code || sec.size < 5)
            continue;
        for (uint32_t rva = sec.rva; rva <= sec.rva + sec.size - 5; rva++)
            if (im.p[rva] == 0xE8 && target(im, rva, 1, 5) == where && n++ == 0)
                *first = rva;
    }
    return n;
}

// rva 가 든 함수의 시작. 함수 표의 한 항목은 함수의 한 조각일 수 있다(큰 함수는 조각이 여럿이다) —
// chained unwind 를 뿌리까지 따라간다. 못 찾으면 0.
uint32_t function_root(const Image &im, uint32_t rva)
{
    uint32_t lo = 0, hi = im.pdata_size / 12, entry = 0;                // 항목은 시작 주소순이다
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2, e = im.pdata + 12 * mid;
        if (rva < im.u32(e)) {
            hi = mid;
        } else if (rva >= im.u32(e + 4)) {
            lo = mid + 1;
        } else {
            entry = e;
            break;
        }
    }
    if (entry == 0)
        return 0;
    uint32_t begin = im.u32(entry), unwind = im.u32(entry + 8);
    for (int depth = 0; depth < 32; depth++) {
        if (!im.has(unwind, 4))
            return 0;
        if (((im.p[unwind] >> 3) & 4) == 0)                             // UNW_FLAG_CHAININFO 가 없으면 뿌리다
            return begin;
        const uint32_t parent = unwind + 4 + 2 * ((im.p[unwind + 2] + 1u) & ~1u);   // 풀기 코드(짝수 개로 맞춘다) 뒤에 부모 항목
        if (!im.has(parent, 12))
            return 0;
        begin = im.u32(parent);
        unwind = im.u32(parent + 8);
    }
    return 0;
}

// 옛 찾기: 닻 문자열 → 그것을 쓰는 자리 → 그 자리가 든 함수(명령 처리 함수) → 머리의 옵션 묶음, 부르는 곳의 this.
// 상태 전역(멀티플레이 표시 · 플레이어 · 지역 표 …)은 여기서 읽지 않는다 — 새 찾기(search_state)의 일이다.
const char *search_legacy(const uint8_t *image, size_t size, GameAddresses *out)
{
    Image im = {};
    im.p = image;
    im.size = size;
    GameAddresses a = *out;
    uint32_t anchor = 0, use = 0, call = 0, at = 0;
    if (image == nullptr || !parse(im))
        return "실행 파일의 머리말을 읽을 수 없습니다";

    if (count_string(im, "cheat allowcheats", &anchor) != 1)
        return "닻 문자열(cheat allowcheats)이 하나가 아닙니다";
    if (count_lea_to(im, anchor, &use) != 1)
        return "닻 문자열을 쓰는 자리가 하나가 아닙니다";
    if ((a.handler = function_root(im, use)) == 0)
        return "명령 처리 함수의 시작을 찾지 못했습니다";

    // 함수 머리: or dword ptr [옵션],40h
    if ((at = find(im, a.handler, a.handler + 0x60, pattern("83 0D ? ? ? ? 40"))) == 0)
        return "함수 머리에 치트 허용 비트를 세우는 명령이 없습니다";
    a.options = target(im, at, 2, 7);

    // 부르는 곳은 하나이고, 바로 앞에서 lea rcx,[this]
    if (count_calls_to(im, a.handler, &call) != 1)
        return "명령 처리 함수를 부르는 곳이 하나가 아닙니다";
    if (call < 7 || !matches(im, call - 7, pattern("48 8D 0D ? ? ? ?")))
        return "부르는 곳 바로 앞에 this 를 채우는 명령이 없습니다";
    a.context = target(im, call - 7, 3, 7);

    const uint32_t all[] = {a.handler, a.context, a.options};
    for (uint32_t rva : all)
        if (rva == 0 || !im.has(rva, 8))
            return "찾은 주소가 실행 파일 밖입니다";
    *out = a;
    return nullptr;
}

// 게임 상태를 읽는 데 쓰는 전역 일곱. 서명은 모두 치트 코드 밖에서 뽑았다(uv run srkit sig-mine <RVA>):
// build 21347933 에서 저마다 실행 구역에 한 번만 맞고, 찾을 것마다 세 서명이 서로 다른 함수에 있다(docs/11-game-internals.md).
// 프로그램 상태와 모드 상태의 셋째 서명은 같은 자리(게임 진입의 mov [모드 상태],2 / mov [프로그램 상태],1)를 읽는다.
const struct Wanted {
    const char *name;                       // GameAddresses 의 필드 이름(srkit locate 와 테스트가 본다)
    const char *label;                      // 로그에 나오는 이름
    uint32_t GameAddresses::*field;
    uint32_t bytes;                         // 그 주소에서 이만큼은 쓸 수 있는 자료 구역 안이어야 한다
    const char *sigs[STATE_SIGS];
} STATE[STATE_WANTED] = {
    {"multiplayer", "멀티플레이 표시", &GameAddresses::multiplayer, 1,
     {"80 3D [rip+1] 00 74 18 8B 47 3C", "80 3D [rip+1] 00 0F B6 FA 74 47", "80 3D [rip+1] 00 74 13 8B 41 14"}},
    {"program_state", "프로그램 상태", &GameAddresses::program_state, 4,
     {"83 3D [rip+1] 01 75 08 48 8B CB", "83 3D [rip+1] 06 48 8B F2 4C 8B F9",
      "C7 05 ? ? ? ? 02 00 00 00 C7 05 [rip+4] 01 00 00 00"}},
    {"mode_state", "모드 상태", &GameAddresses::mode_state, 4,
     {"83 3D [rip+1] 02 48 8B D9 75 40", "83 3D [rip+1] 00 48 8B D9 75 21",
      "C7 05 [rip+4] 02 00 00 00 C7 05 ? ? ? ? 01 00 00 00"}},
    {"player_index", "플레이어 인덱스", &GameAddresses::player_index, 4,
     {"44 3B 05 [rip] 49 63 C8 74 16", "44 8B 35 [rip] 45 84 ED 74 05", "44 8B 0D [rip] 45 33 C0 33 D2"}},
    {"player_pointer", "플레이어 포인터", &GameAddresses::player_pointer, 8,
     {"48 8B 15 [rip] 8B 42 6C 85 C0", "48 8B 0D [rip] 48 85 C9 74 58", "4C 8B 35 [rip] FF C7 3B 7D 38"}},
    {"region_table", "지역 표", &GameAddresses::region_table, 8 * 1024,
     {"4C 8D 05 [rip] 49 8B 10 48 85 D2", "48 8D 0D [rip] 90 48 8B 31 48 85 F6", "48 8D 0D [rip] 48 8B 0C C1 48 85 C9"}},
    {"region_count", "지역 수", &GameAddresses::region_count, 4,
     {"44 8B 35 [rip] 44 8B DF 8B D6", "44 8B 0D [rip] 8B D7 45 85 C9", "44 8B 15 [rip] 43 8D 04 0C 99"}},
};

// 값을 읽고 쓰는 자리 넷. 같은 규칙으로 뽑았다(uv run srkit sig-mine <RVA> / --offset <간격> <첫 칸>). 둘을 읽는 서명은
// 간격이 먼저, 첫 칸이 나중이다. "쓰는 물자" 표의 셋째 서명은 가운데 명령(imul r,r,재고 간격)의 상수를 구멍으로 뒀다.
const struct ValueWanted {
    const char *name;                       // srkit locate 와 테스트가 본다
    const char *label;                      // 로그와 창에 나오는 이름
    const char *sigs[STATE_SIGS];
} VALUES[VALUE_WANTED] = {
    {"world_pointer", "세계 자료 포인터",
     {"4C 8B 2D [rip] 41 8B F9 B3 01", "48 8B 0D [rip] 99 45 0F BF 45 48", "4C 8B 0D [rip] 66 0F 6E E7 0F 5B E4"}},
    {"treasury", "국고 칸",
     {"F2 0F 10 87 [u32] 66 0F 2F C1 76 5B", "F2 0F 11 89 [u32] 33 C9 89 4C 24 60", "F2 0F 11 9B [u32] 76 18 0F 28 C1"}},
    {"stock", "재고 칸",
     {"48 69 C8 [u32] 42 0F 2F 84 21 [u32] 76 40", "48 69 C7 [u32] 48 03 C3 F3 0F 10 80 [u32]",
      "49 69 C0 [u32] 0F 2F 94 08 [u32] 76 0E"}},
    {"used", "쓰는 물자 표",
     {"49 69 CE [u32] F3 0F 10 44 01 [u8] 0F 2F C6", "48 69 D0 [u32] F3 42 0F 10 44 02 [u8] 41 0F 2F C5",
      "4C 69 C2 [u32] 48 69 CA ? ? ? ? 0F 28 C2 F3 41 0F 59 44 01 [u8]"}},
};

// 더 쓰는 값의 자리 일곱 — 기능마다 한 묶음이다(group: 0 지식, 1 여론, 2 관계). 같은 규칙으로 뽑았다
// (uv run srkit sig-mine --offset <자리>). 표의 서명은 모두 "지역 객체 + 인덱스 × 4 + 자리" 꼴의 명령에서 읽는다.
const struct MoreWanted {
    const char *name;                       // srkit locate 와 테스트가 본다(MoreLayout 의 필드 순서와 같다)
    const char *label;                      // 로그와 창에 나오는 이름
    int group;
    uint32_t bytes;                         // 그 자리부터 차지하는 크기: 칸은 4, 표는 4 × REGION_SLOTS
    const char *sigs[STATE_SIGS];
} MORE[MORE_WANTED] = {
    {"tech", "기술 수준 칸", 0, 4,
     {"F3 0F 2C 88 [u32] 41 3B C8 7E 4A", "F3 0F 2C 87 [u32] 83 E8 0F 3B C8", "48 05 [u32] F3 0F 2C 00 05 6C 07 00 00"}},
    {"opinion0", "여론 칸 1", 1, 4,
     {"F3 0F 58 B0 [u32] 48 85 D2 74 48", "F3 0F 10 89 [u32] 44 0F 2F D1 76 18", "F3 0F 59 8B [u32] F3 0F 58 C8 0F 2F F1"}},
    {"opinion1", "여론 칸 2", 1, 4,
     {"F3 0F 10 88 [u32] 0F 2F CB 76 5D", "F3 0F 10 8F [u32] 0F 2E CE 7A 14", "F3 0F 59 8B [u32] F3 0F 58 C8 0F 28 C2"}},
    {"opinion2", "여론 칸 3", 1, 4,
     {"F3 0F 10 B0 [u32] 48 85 D2 74 43", "F3 0F 10 8A [u32] 0F 2F F9 76 2E", "F3 0F 10 81 [u32] 48 8D 44 24 70 F3 0F 5C C1"}},
    {"relation0", "관계 표 1", 2, 4 * REGION_SLOTS,
     {"41 0F 2F 84 8D [u32] 76 0D 40 B6 01", "F3 42 0F 10 9C 82 [u32] 0F 2F FB 76 33", "F3 0F 10 84 81 [u32] 41 0F 2F C6 76 5F"}},
    {"relation1", "관계 표 2", 2, 4 * REGION_SLOTS,
     {"41 0F 2F 84 84 [u32] 76 7A 48 85 D2", "F3 0F 11 84 88 [u32] 45 85 DB 79 2A", "F3 0F 10 9C 81 [u32] 8B D0 0F 2F FB"}},
    {"casus", "전쟁 명분 표", 2, 4 * REGION_SLOTS,
     {"F3 0F 10 9C 8A [u32] 0F 2F FB 76 35", "F3 41 0F 5C 8C 82 [u32] 0F 2F C1 76 02", "F3 0F 11 84 88 [u32] 0F 28 C2 48 8B 07"}},
    // 인구와 지지율(3단계 4). 게임은 자정마다 인구 칸을 "민간 인구 + 풀 + 현역 인력"으로 다시 쓴다 — 풀은 날마다 다시 쌓이지 않는 칸이라
    // 거기에 더한 것이 남는다(docs/11). 내장 치트 populate 가 올리던 군 인력 칸과 셋째 칸에는 쓰지 않는다
    {"people0", "인구 칸", 3, 4,
     {"F3 0F 11 81 [u32] 45 84 ED 74 3C", "F3 0F 10 9A [u32] 0F 28 CB 0F 28 C4", "F3 0F 10 90 [u32] 44 0F 2F D2 76 2A"}},
    {"people1", "인구의 풀 칸", 3, 4,
     {"F3 0F 10 80 [u32] 0F 2F C3 76 2B", "F3 0F 10 90 [u32] 0F 28 CB 0F 2E D6", "F3 0F 10 98 [u32] 0F 28 C1 0F 2E DA"}},
    {"approval", "지지율 칸", 4, 4,
     {"F3 0F 10 89 [u32] 0F 2F C1 76 33", "F3 0F 10 83 [u32] F3 0F 59 C7 0F 5A D0", "F3 0F 11 83 [u32] 48 8B 47 10 0F B7 08"}},
};

// 부르는 게임의 함수. 그 함수를 부르는 자리의 서명이다(uv run srkit sig-mine <함수의 RVA> --back 2) — 인자를 싣는 명령까지 넣었다.
const uint32_t ACT_CONSTANT = 0xFFFF;
const uint32_t ACT_WORLD_CONSTANT = 0xFFFE;
const uint32_t ACT_TABLE = 0xFFFD;            // 읽기 전용 자료(가상 함수 표)의 RVA — 8바이트로 줄 맞춰져 있고 이미지 안이어야 한다   // 상수: 세계 객체(큰 전역 구조체) 안의 자리 — 지역 객체 안의 자리보다 훨씬 멀다
const struct ActWanted {
    const char *name;                       // srkit locate 와 테스트가 본다(ActLayout 의 필드 순서와 같다)
    const char *label;                      // 로그와 창에 나오는 이름
    uint32_t bytes;                         // 자료면 그 크기(4 · 8). 함수면 0, 상수(구조체 안의 자리)면 ACT_CONSTANT · ACT_WORLD_CONSTANT
    const char *sigs[STATE_SIGS];
} ACTS[ACT_WANTED] = {
    {"colonize", "식민지화 함수", 0,
     {"41 B0 01 49 8B CE E8 [rip] 33 D2", "49 8B C9 41 B0 01 E8 [rip] FF C7", "45 33 C0 8B D7 E8 [rip] 48 63 CF"}},
    {"fight", "전쟁 함수", 0,
     {"88 5C 24 20 49 8B CA E8 [rip]", "40 0F B6 D6 49 8B CD E8 [rip]", "B2 01 C6 44 24 20 00 E8 [rip] 8B 4E 38"}},
    {"become", "플레이 지역을 바꾸는 함수", 0,
     {"33 D2 48 8B CB E8 [rip] 0F B7 43 04", "49 8B 8C C4 A0 E8 34 00 33 D2 E8 [rip]", "49 8B F0 48 8B 0C D8 E8 [rip]"}},
    // 지도자 제거: 게임이 지도자가 죽은 날 스스로 하는 일(0x768A00 안)과 같은 두 함수
    {"elect", "새 지도자를 세우는 함수", 0,
     {"83 4C 01 28 02 48 8B CB E8 [rip]", "33 D2 48 8B CE E8 [rip] 0F B7 4E 04", "0F 97 C2 45 33 C0 E8 [rip] 49 8B 06"}},
    {"news", "알림 함수", 0,
     {"4C 89 74 24 30 C6 44 24 28 01 C7 44 24 20 5C 06 00 00 E8 [rip]", "44 8B CA 44 8B C2 E8 [rip] 0F B7 47 04",
      "44 8B C2 C7 44 24 20 51 01 00 00 E8 [rip]"}},
    // 자료: 그 포인터를 읽고 곧바로 가리키는 word(지역의 인덱스)를 읽는 자리들
    {"map_pick", "지도에서 고른 지역", 8,
     {"48 8B 0D [rip] 44 8B CF 0F B7 11", "48 8B 05 [rip] 0F B7 08 41 3B CB", "48 8B 05 [rip] 0F B7 08 0F 28 C1"}},
    {"player_index2", "플레이어 인덱스의 둘째 사본", 4,
     {"48 63 05 [rip] 44 3B C0 74 68", "48 63 05 [rip] 44 3B F8 74 60", "48 63 05 [rip] 44 3B C8 74 69"}},
    {"player_pointer2", "플레이어 포인터의 둘째 사본", 8,
     {"48 89 0D [rip] 83 39 02 74 48", "48 89 05 [rip] 8B 86 50 05 00 00 8B D0", "48 89 05 [rip] 75 5F 0F B7 05 ? ? ? ?"}},
    // 지도자 표: 레코드의 크기(C8)와 깃발의 자리(+28)가 박힌 자리들
    {"leader_table", "지도자 표", 8,
     {"48 8B 05 [rip] 48 69 C9 C8 00 00 00 83 4C 01 28 04", "48 8B 05 [rip] 48 69 D1 C8 00 00 00 83 4C 02 28 04",
      "48 8B 05 [rip] 48 69 CA C8 00 00 00 83 4C 01 28 04"}},
    // 상수: 지역 객체에서 지도자 번호를 읽어 곧바로 레코드의 크기를 곱하는 자리들
    {"leader_id", "지도자 번호의 자리", ACT_CONSTANT,
     {"0F B7 87 [u32] 48 69 E8 C8 00 00 00 49 8B 5C 2E 20", "0F B7 81 [u32] 4C 8D 0D ? ? ? ? 4C 69 D0 C8 00 00 00",
      "0F B7 81 [u32] 48 8D 2D ? ? ? ? 48 69 F0 C8 00 00 00"}},
    // 동맹 맺기: 조약 함수와, 지역 객체에서 조약마다의 비트 묶음 포인터가 놓인 표의 자리
    {"treaty", "조약 함수", 0,
     {"48 8B CE 89 7C 24 20 E8 [rip]", "48 8B CD 89 44 24 20 E8 [rip]", "49 8B CF 44 89 64 24 20 E8 [rip]"}},
    {"treaties", "조약 표의 자리", ACT_CONSTANT,
     {"48 8D 81 [u32] 49 63 E8 44 0F B6 F2", "48 8D 97 [u32] 48 8D 14 C2 48 85 D2", "48 05 [u32] 74 25 48 8B 10 48 85 D2"}},
    // 병합: 칸의 주인을 바꾸는 함수와 지도 객체(칸 수 +88 · 너비 +80 이 박힌 자리들)
    {"annex", "칸의 주인을 바꾸는 함수", 0,
     {"49 8B CC 88 44 24 20 E8 [rip]", "45 33 C9 40 88 74 24 20 E8 [rip]", "45 33 C0 8B D6 E8 [rip] 48 8B 44 24 58"}},
    {"map", "지도 객체", 8,
     {"48 8B 35 [rip] FF C7 3B BE 88 00 00 00", "4C 8B 05 [rip] 41 39 98 88 00 00 00 7E 58", "48 8B 05 [rip] 8B 98 80 00 00 00 3B F3"}},
    // 부대 생성: 부대를 만드는 함수와 "지도에서 고른 칸"(첫 서명에는 "둘 다 0 이면 고르지 않았다"는 게임의 검사가 박혀 있다)
    {"spawn", "부대를 만드는 함수", 0,
     {"C6 44 24 20 00 44 8B 48 48 E8 [rip]", "44 88 64 24 28 C6 44 24 20 00 E8 [rip]", "44 88 7C 24 28 C6 44 24 20 01 E8 [rip]"}},
    {"map_hex", "지도에서 고른 칸", 4,
     {"83 3D [rip+1] 00 0F B7 05 ? ? ? ? 75 05 66 85 C0 74 3C", "8B 0D [rip] 0F 1F 44 00 00 89 4C 24 70", "8B 05 [rip] 3B C8 74 08 3B DA"}},
    // 보급 채우기: 게임에서 고른 부대들의 목록과, 세계 객체 안의 개량 보정 표(깃발의 자리 — 서명마다 그 깃발의 비트와 짝인 값의 자리가 함께 박혀 있다:
    // 비트 13 ↔ 깃발 − 0x34, 비트 17 ↔ − 0x24, 비트 14 ↔ − 0x30 — "값 n 은 깃발 − 0x68 + 4n" 이라는 꼴)
    {"picked_units", "고른 부대의 목록", 8,
     {"48 8B 1D [rip] 33 ED 8B FD 48 85 DB", "48 8B 15 [rip] 48 89 54 24 78 48 85 D2", "48 8B 1D [rip] 45 32 FF 48 85 DB"}},
    {"upgrades", "개량 보정 표의 자리", ACT_WORLD_CONSTANT,
     {"41 F7 84 09 [u32] 00 20 00 00 F3 41 0F 10 84 09 F0 89 36 00 74 05 0F 28 D0",
      "42 F7 84 29 [u32] 00 00 02 00 F3 42 0F 10 84 29 00 8A 36 00 75 08 F3 0F 58 C3",
      "42 F7 84 39 [u32] 00 40 00 00 F3 42 0F 10 94 39 F4 89 36 00 74 05 0F 28 C2 EB 0F"}},
    // 즉시 승리: 판이 끝난 꼴과 이긴 지역들의 묶음
    {"game_over", "판이 끝난 꼴", 4,
     {"44 89 2D [rip] 48 85 DB 75 26", "8B 05 [rip] B9 5A 00 00 00 F7 F1", "83 3D [rip+1] 5A 73 42 4C 8B 0D ? ? ? ?"}},
    {"winners", "이긴 지역들의 묶음", 8,
     {"4C 8B 0D [rip] 4D 85 C9 74 31", "48 89 05 [rip] 8B D3 44 8B C3", "48 89 05 [rip] 83 E7 3F 49 8B D6"}},
    // GUI 숨기기/보이기: 화면의 깃발들, GUI 객체, 그 객체의 가상 함수 표(표를 거는 생성자들 — 객체의 깃발 자리 +2C 가 함께 박혀 있다)
    {"ui_flags", "화면의 깃발", 4,
     {"8B 35 [rip] C1 EE 0B 83 E6 01", "8B 05 [rip] C1 E8 1F 83 E0 01 85 C0", "8B 05 [rip] 41 83 E0 03 0F BA E0 0C"}},
    {"gui", "GUI 객체", 8,
     {"48 8B 05 [rip] 48 8D 0D ? ? ? ? 48 89 74 24 30", "48 8B 05 [rip] 48 85 C0 74 07 81 48 28 00 00 02 00 48 8B 05 ? ? ? ?",
      "48 8B 05 [rip] 45 33 D2 4C 8B C1"}},
    {"gui_vtable", "GUI 객체의 함수 표", ACT_TABLE,
     {"48 8D 05 [rip] 48 89 01 C7 41 2C 00 00 00 01", "48 8D 0D [rip] 48 89 08 C7 40 2C 00 00 00 01",
      "48 8D 05 [rip] 49 89 01 41 C7 41 2C 00 00 00 01"}},
    // 최강 부대 묶음: 그 지역이 그 유형의 부대를 내놓는 칸을 찾는 함수
    {"deploy_hex", "배치 칸을 찾는 함수", 0,
     {"41 B8 02 00 00 00 32 DB E8 [rip]", "45 8B C6 48 8D 94 24 38 01 00 00 E8 [rip]", "48 8D 55 A8 48 8B 8D 08 01 00 00 E8 [rip]"}},
    // 내 부대 전부에 쓰기: 세계의 부대 목록
    {"units", "세계의 부대 목록", 8,
     {"4C 8B 3D [rip] 0F 57 C0 33 C0", "48 8B 15 [rip] 2B D8 4C 8B FA", "48 8B 15 [rip] C1 E8 19 A8 01"}},
    // 지역의 이름: 판마다 다르다(역사 시나리오의 1188 은 독일, 202 는 다호메이) — 이름표 대신 게임이 지금 쓰는 글을 읽는다
    {"region_name", "지역 이름의 포인터", ACT_CONSTANT,
     {"48 8B B3 [u32] 48 85 F6 74 0D", "48 89 BE [u32] 48 85 ED 74 4D", "48 89 BB [u32] 48 8B FD 48 8B 8B 18 BD 01 00"}},
};
static_assert(sizeof(ActLayout) == ACT_WANTED * sizeof(uint32_t), "ActLayout 의 필드는 표 ACTS 의 순서대로 uint32_t 다");

// 배율(묶음 7). 자료 둘과 지역 객체 안의 자리 아홉 — 모두 치트 코드 밖에서 뽑았다(uv run srkit sig-mine [--offset]).
// 곱 보정 표의 앞 두 서명에는 지역 하나의 줄(MUL_STEP = 0x320)이 박혀 있다.
const uint32_t ACT_REGION_TABLE = 0xFFFC;     // 자료: 지역 인덱스마다 MUL_STEP 바이트인 표의 첫 칸(4바이트로 줄 맞춰져 있고 표가 모두 자료 구역 안)
const ActWanted MULTS[MULT_WANTED] = {
    {"mul_table", "곱 보정 표", ACT_REGION_TABLE,
     {"48 69 C8 20 03 00 00 48 8D 04 51 F3 0F 59 9C 30 [u32]", "48 69 C8 20 03 00 00 48 8D 04 51 48 8D 0D ? ? ? ? F3 0F 59 94 08 [u32]",
      "F3 45 0F 10 94 90 [u32] F3 45 0F 59 D2 45 33 FF"}},
    {"day", "날의 칸", 4,
     {"39 3D [rip] 7E 65 8B 81 28 BB 01 00", "39 3D [rip] 7E 68 8B 83 28 BB 01 00", "FF 05 [rip] 45 33 C9 48 8B 0D ? ? ? ?"}},
    {"tax_total", "연간 세수 칸", ACT_CONSTANT,
     {"F2 0F 10 97 [u32] F3 0F 10 4E 40 0F 57 C0", "F2 0F 10 86 [u32] 66 0F 2F 86 C0 A6 00 00 76 04",
      "F2 0F 58 80 [u32] F2 0F 58 05 ? ? ? ? F2 0F 5E C8"}},
    {"army_pay_level", "군 인건비의 지출 수준 칸", ACT_CONSTANT,
     {"F3 0F 10 93 [u32] 0F 2F DA 76 51", "B8 [u32] 0F 44 C1 F3 0F 10 34 18", "F3 44 0F 10 B6 [u32] 45 0F 2F F4 76 0F"}},
    {"army_upkeep_level", "군 유지비의 지출 수준 칸", ACT_CONSTANT,
     {"F3 0F 10 9B [u32] F2 0F 58 D0 0F 28 C5", "B8 [u32] 41 B8 E0 01 00 00 41 0F 44 C0", "F3 44 0F 10 8B [u32] 45 0F 2F CE F3 41 0F 59 C5"}},
    {"army_pay", "군 인건비 칸", ACT_CONSTANT,
     {"F3 0F 59 8B [u32] 0F 2F C1 76 14", "F3 0F 10 87 [u32] 45 8B E5 F3 0F 59 87 DC A6 00 00",
      "F3 41 0F 58 88 [u32] 66 42 0F 6E 04 00 41 0F B6 40 1C"}},
    {"army_upkeep", "군 유지비 칸", ACT_CONSTANT,
     {"F3 0F 10 AB [u32] 45 84 FF B9 DC 01 00 00", "F3 0F 10 AB [u32] 0F 5A C9 F3 0F 58 83 0C 4D 01 00",
      "F3 0F 10 87 [u32] 45 8B E5 F3 0F 59 87 E0 A6 00 00"}},
    {"unit_cost", "부대 생산비 칸", ACT_CONSTANT,
     {"89 81 [u32] 8B 81 98 BB 01 00 89 81 E0 4B 01 00", "F3 0F 58 83 [u32] F3 0F 10 8B E8 A9 00 00 0F 5A C9",
      "F3 0F 10 9B [u32] F3 44 0F 10 83 FC 4B 01 00 F3 0F 58 CB"}},
    {"research_step", "연구의 하루치 칸", ACT_CONSTANT,
     {"F3 0F 5C 83 [u32] 89 47 24 44 0F 2F F8", "F3 0F 10 89 [u32] F3 0F 10 05 ? ? ? ? 0F 2F C1 76 04", "F3 0F 11 83 [u32] 0F 5A C6 66 0F 2F 83"}},
    // 사회 지출: 비용 여덟(float, 4바이트 간격)과 지출 수준 여덟. 셋째 서명(비용 × 수준을 돌려주는 게임의 잎 함수)에 그 꼴이 박혀 있다
    {"social_cost", "사회 지출 비용 칸", ACT_CONSTANT,
     {"48 8D 9E [u32] 0F B7 46 04 41 0F 28 C1", "F3 44 0F 59 97 [u32] F3 44 0F 58 D8 F3 0F 58 FA",
      "F3 0F 10 84 81 [u32] F3 41 0F 59 84 81 90 00 00 00 C3"}},
    {"social_level", "사회 지출 수준 칸", ACT_CONSTANT,
     {"B8 [u32] 41 0F 44 C0 41 B8 84 01 00 00", "48 8D 97 [u32] 49 C7 C0 44 60 FF FF 4C 2B C7",
      "F3 0F 11 8F [u32] F3 0F 10 8F B4 5D 01 00 0F 2E CE"}},
    // 사회 항목의 품질 등급 여덟(2026-10-21): 날마다의 셈(0x76a040) · 판 시작의 초기화(0xbbbf40 — 지출 수준 칸 A680h 도 박혀 있다) · 0x495180 의 읽기
    {"social_grade", "사회 항목의 품질 등급 칸", ACT_CONSTANT,
     {"48 8D BB [u32] F3 44 0F 10 0D ? ? ? ? 4C 8D 3D ? ? ? ?", "F3 0F 11 8F [u32] F3 0F 10 87 80 A6 00 00 0F 2E C6",
      "F3 0F 10 BF [u32] 0F 28 C1 F3 41 0F 59 C3"}},
    // 예비 인원(2026-10-22): 생산 쪽의 읽기(0x846795) · 지역 값 다시 셈(0x769c33) · 0x7bd42e 의 읽기
    {"reserve", "예비 인원 칸", ACT_CONSTANT,
     {"F3 0F 10 81 [u32] 0F 2F C1 72 3C", "48 8D BE [u32] F3 0F 10 1F 0F 28 D1", "F3 0F 10 80 [u32] 41 0F 2F C0 0F 93 C0"}},
    // 군부 지지율(2026-10-26): 국민 지지율 칸을 곁에서 읽는 0x764c90 · 지역 값 다시 셈(0x768a00)의 곱 · 0x704c20 의 쓰기
    {"mil_approval", "군부 지지율 칸", ACT_CONSTANT,
     {"8B 81 [u32] 33 F6 F3 0F 10 81 04 4B 01 00", "F3 0F 59 96 [u32] F3 0F 59 D0 0F 57 C0", "89 81 [u32] F3 0F 10 81 20 4B 01 00 0F 2E C5"}},
    // 묶음 칸(2026-10-27): word 를 읽어 견주는 세 곳 — 0x770330 · 0xbcb320, 그리고 GUI 의 값(0x5391a0)이 다른 지역의 번호(+8)와 견준다
    {"group", "묶음 칸", ACT_CONSTANT,
     {"0F B7 83 [u32] 3B C8 75 4D EB 13", "45 0F B7 80 [u32] 66 45 85 C0 74 37",
      "48 05 [u32] 0F B7 00 48 8B 8C 24 90 B6 00 00 48 83 C1 08 0F B7 09 3B C1 74 3C 48 63 84 24 14 72 00 00"}},
};
static_assert(sizeof(MultLayout) == MULT_WANTED * sizeof(uint32_t), "MultLayout 의 필드는 표 MULTS 의 순서대로 uint32_t 다");
static_assert(MULT_WANTED <= ACT_WANTED, "서명 표의 크기는 가장 큰 표에 맞춘다");

// 연구의 일곱. 같은 규칙으로 뽑되(uv run srkit sig-mine …) 꼴의 상수가 박힌 명령까지 늘렸다(--with · --back · --through-jumps).
// 서명마다 무엇이 박혀 있는지는 docs/11-game-internals.md 의 표와 tests/test_toybox_game.py 의 RESEARCH_PINS 에 있다:
//   기술 표        레코드 88h · 보유 묶음 +50h · 묶음 80h 바이트 / 수준 +1 · 빈 자리(+0 이 0)
//   기술 수        선행 +4 · +6, 레코드 88h
//   부대 설계 표   보유 묶음 +F8h · 레코드 168h · 묶음 80h 바이트 / 깃발 +F0h(1000000h) · +ECh(1) · 연구 대상 +20h · 빈 자리(+0 이 0)
//   부대 설계 수   레코드 168h · 연구 대상 +20h · 선행 +34h
//   세계 객체      lea 의 목표와, 바로 뒤 명령의 "지역 표까지의 거리"(둘을 읽는다 — 상태 묶음의 지역 표와 견준다)
//   연구 목록      칸 24바이트(×3 ×8) · 노드의 종류 +1Ch · 번호 +18h · 다음 +10h · 깃발 +20h · +24h
//   다시 셈 함수   call 의 목표. 앞의 두 명령(mov edx,-1 / mov rcx,<세계 객체>)이 인자의 꼴을 박는다
const struct ResearchWanted {
    const char *name;                       // srkit locate 와 테스트가 본다(ResearchLayout 의 필드 순서와 같다)
    const char *label;                      // 로그와 창에 나오는 이름
    const char *sigs[STATE_SIGS];
} RESEARCH[RESEARCH_WANTED] = {
    {"tech_table", "기술 표",
     {"48 8B 15 [rip] 48 69 F0 88 00 00 00 4C 89 74 24 48 48 8D 7A 50 44 0F B7 71 04 41 8B EE 48 03 FE 74 3C 48 8B 17 48 85 D2 75 10 "
      "B9 80 00 00 00",
      "48 8B 0D [rip] 4C 69 CE 88 00 00 00 48 83 C1 50",
      "48 8B 05 [rip] 44 38 24 03 76 33 8B 54 03 4C F6 C2 40 75 2A 0F B6 4C 03 01"}},
    {"tech_count", "기술 수",
     {"39 3D [rip] 7E 7B 48 89 5C 24 30 48 8B DF 0F 1F 00 48 8B 0D ? ? ? ? 48 03 CB 80 39 00 76 4B 0F BF 41 04 3B C6 74 08 "
      "0F BF 41 06",
      "44 8B 0D [rip] 33 DB 48 89 7C 24 40 45 85 C9 0F 8E ? ? ? ? 33 FF 0F 1F 00 48 8B 0D ? ? ? ? 80 3C 39 00 76 7E "
      "0F BF 44 39 04 8B 55 18 3B C2 74 09 0F BF 44 39 06",
      "44 3B 35 [rip] 0F 8D ? ? ? ? 49 69 D6 88 00 00 00 48 03 15 ? ? ? ? F6 05 ? ? ? ? 02 0F BF 42 04 89 44 24 60 0F BF 42 06"}},
    {"design_table", "부대 설계 표",
     {"4C 8B 3D [rip] 49 63 04 24 49 8D 9F F8 00 00 00 44 0F B7 77 04 48 69 C8 68 01 00 00 41 8B FE 48 03 D9 74 2B 48 8B 03 "
      "48 85 C0 75 0D B9 80 00 00 00",
      "4C 8B 05 [rip] 4D 03 C2 49 39 38 74 3B 41 F7 80 F0 00 00 00 00 00 00 01 75 2E 41 F6 80 EC 00 00 00 01 75 24 66 41 39 78 20",
      "48 8B 1D [rip] 48 83 3C 2B 00 0F 84 ? ? ? ? F7 84 2B F0 00 00 00 00 00 00 01 0F 85 ? ? ? ? F6 84 2B EC 00 00 00 01 "
      "0F 85 ? ? ? ? 66 83 7C 2B 20 00"}},
    {"design_count", "부대 설계 수",
     {"3B 1D [rip] 0F 8D ? ? ? ? 48 69 CB 68 01 00 00 4A 83 3C 39 00 0F 84 ? ? ? ? 66 42 83 7C 39 20 00 0F 84 ? ? ? ? 33 C0 "
      "4D 8D 4F 34",
      "44 3B 35 [rip] 0F 8D ? ? ? ? 48 8B 0D ? ? ? ? 49 69 DE 68 01 00 00 F6 05 ? ? ? ? 02 0F B7 44 0B 34",
      "41 FF C1 49 81 C2 68 01 00 00 44 3B 0D [rip]"}},
    {"world", "세계 객체",
     {"4C 8D 15 [rip] 4D 8B 84 F2 [u32] 85 DB", "48 8D 05 [rip] 48 8B 9C D8 [u32] 8B F5", "48 8D 05 [rip] B2 01 48 8B 8C D8 [u32]"}},
    {"lists", "연구 목록",
     {"48 8D 0C 40 48 8B 94 CA [u32] 48 8B CA 48 85 D2 74 31 80 79 1C 02 75 10 44 3B 59 18 74 18 48 85 C9 75 05 48 8B CA EB EA "
      "48 8B 41 10 48 8B C8 48 85 C0 75 DE EB 0D F7 41 20 00 00 00 88",
      "49 8B 94 CF [u32] 48 8B C2 48 85 D2 74 30 48 8B CA 80 79 1C 01 75 10 44 3B 49 18 74 18 48 85 C0 75 05 48 8B C2 EB E7 "
      "48 8B 40 10 48 8B C8 48 85 C0 75 DE EB 09 F7 41 24 00 00 00 88",
      "48 8D 0C 40 49 8B 94 CC [u32] 48 8B C2 48 85 D2 74 5B 48 8B DA 0F 1F 80 00 00 00 00 80 7B 1C 01 75 0F 3B 7B 18 74 18 "
      "48 85 C0 75 05 48 8B C2 EB E1 48 8B 40 10"}},
    {"recompute", "다시 셈 함수",
     {"BA FF FF FF FF 49 8B C9 E8 [rip]", "BA FF FF FF FF 49 8B CA E8 [rip]",
      "BA FF FF FF FF 49 8B CC E8 [rip] 4C 63 84 24 50 41 00 00"}},
};

const int MAX_TABLE = ACT_WANTED * STATE_SIGS;      // 한 표의 서명 수의 상한(상태 21개, 값 12개, 더 쓰는 값 30개, 연구 21개, 부르는 함수 36개)
static_assert(VALUE_WANTED <= ACT_WANTED && STATE_WANTED <= ACT_WANTED && RESEARCH_WANTED <= ACT_WANTED && MORE_WANTED <= ACT_WANTED,
              "서명을 맞추는 배열은 가장 큰 표(부르는 함수)의 크기로 잡았다 — 더 큰 표를 더하면 MAX_TABLE 을 키운다");
static_assert(sizeof(MoreLayout) == MORE_WANTED * sizeof(uint32_t), "MoreLayout 의 필드는 표 MORE 의 순서대로 uint32_t 열이다");
static_assert(sizeof(ResearchLayout) == RESEARCH_WANTED * sizeof(uint32_t),
              "ResearchLayout 의 필드는 표 RESEARCH 의 순서대로 uint32_t 일곱이다");
static_assert(RESEARCH_NEED <= STATE_SIGS, "모두 맞아야 한다는 것은 서명의 수까지다");

// 전역 변수가 있을 수 있는 곳인가: 쓸 수 있는 자료 구역 안.
bool in_data(const Image &im, uint64_t rva, uint64_t bytes)
{
    for (int s = 0; s < im.count; s++) {
        const Section &sec = im.sections[s];
        if (sec.writable && rva >= sec.rva && rva + bytes <= static_cast<uint64_t>(sec.rva) + sec.size)
            return true;
    }
    return false;
}

// 객체 안의 자리로 말이 되는가.
bool offset_ok(uint64_t offset)
{
    return offset > 0 && offset < 0x100000;
}

// 서명 표 하나(찾을 것 n 개 × 서명 STATE_SIGS 개)를 실행 구역에 한 번 훑어 맞춘다. sigs · hits 는 MAX_TABLE 칸.
// 표가 틀렸으면 false 와 why.
template <class Row>
bool scan_table(const Image &im, const Row *table, int n, SigRow *rows, Sig *sigs, SigHit *hits, char *why, size_t why_size)
{
    const int total = n * STATE_SIGS;
    if (total > MAX_TABLE) {
        snprintf(why, why_size, "서명 표가 너무 큽니다 (%d개)", total);
        return false;
    }
    SigRange ranges[32];                                // 구역은 32개까지 읽는다(parse)
    int range_count = 0;
    for (int s = 0; s < im.count; s++)
        if (im.sections[s].code) {
            ranges[range_count].begin = im.sections[s].rva;
            ranges[range_count].end = im.sections[s].rva + im.sections[s].size;
            range_count++;
        }
    for (int i = 0; i < total; i++)
        if (!sig_parse(table[i / STATE_SIGS].sigs[i % STATE_SIGS], &sigs[i])) {
            snprintf(why, why_size, "서명 표가 틀렸습니다 (%s)", table[i / STATE_SIGS].name);
            return false;
        }
    sig_scan(im.p, im.size, ranges, range_count, sigs, total, hits);
    if (rows != nullptr)
        for (int i = 0; i < total; i++) {
            rows[i].count = hits[i].count;
            rows[i].at = hits[i].at;
            rows[i].value = static_cast<uint32_t>(hits[i].value[0]);
            rows[i].value2 = static_cast<uint32_t>(hits[i].value[1]);
        }
    return true;
}

// 찾을 것 하나(표의 w 째)의 투표. value 는 SIG_CAPTURES 칸. 못 찾으면 false 와 why. what 은 서명이 읽어 내는 것("주소를" · "값을").
// need: 정확히 한 번 맞아야 하는 서명의 수(연구 묶음은 셋 모두).
template <class Row>
bool vote_item(const Row *table, int w, const char *what, const Sig *sigs, const SigHit *hits, uint64_t *value, char *why,
               size_t why_size, int need = STATE_NEED)
{
    int matched = 0;
    if (sig_vote(sigs + w * STATE_SIGS, hits + w * STATE_SIGS, STATE_SIGS, need, value, &matched))
        return true;
    if (matched >= need)
        snprintf(why, why_size, "%s: 서명들이 서로 다른 %s 냅니다", table[w].label, what);
    else
        snprintf(why, why_size, "%s: 서명 %d개 가운데 %d개", table[w].label, STATE_SIGS, matched);
    return false;
}

// 서명 표 하나를 맞추고 찾을 것마다 투표한다 — 하나라도 못 찾으면 false 와 why.
// values: n 줄 × SIG_CAPTURES 칸(0 으로 채워서 준다).
template <class Row>
bool vote_table(const Image &im, const Row *table, int n, const char *what, SigRow *rows, uint64_t (*values)[SIG_CAPTURES],
                char *why, size_t why_size, int need = STATE_NEED)
{
    Sig sigs[MAX_TABLE];
    SigHit hits[MAX_TABLE];
    if (!scan_table(im, table, n, rows, sigs, hits, why, why_size))
        return false;
    for (int w = 0; w < n; w++)
        if (!vote_item(table, w, what, sigs, hits, values[w], why, why_size, need))
            return false;
    return true;
}

bool search_state(const uint8_t *image, size_t size, GameAddresses *out, SigRow *rows, char *why, size_t why_size)
{
    Image im = {};
    im.p = image;
    im.size = size;
    if (image == nullptr || !parse(im)) {
        snprintf(why, why_size, "실행 파일의 머리말을 읽을 수 없습니다");
        return false;
    }
    uint64_t values[STATE_WANTED][SIG_CAPTURES] = {};
    if (!vote_table(im, STATE, STATE_WANTED, "주소를", rows, values, why, why_size))
        return false;
    GameAddresses found = *out;
    for (int w = 0; w < STATE_WANTED; w++) {
        const uint64_t rva = values[w][0];
        if (rva == 0 || rva >= size || !im.has(rva, STATE[w].bytes)) {
            snprintf(why, why_size, "%s: 찾은 주소가 실행 파일 밖입니다", STATE[w].label);
            return false;
        }
        if (!in_data(im, rva, STATE[w].bytes)) {
            snprintf(why, why_size, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", STATE[w].label);
            return false;
        }
        for (int v = 0; v < w; v++)
            if (rva < values[v][0] + STATE[v].bytes && values[v][0] < rva + STATE[w].bytes) {
                snprintf(why, why_size, "%s: 찾은 주소가 %s 의 자리와 겹칩니다", STATE[w].label, STATE[v].label);
                return false;
            }
        found.*(STATE[w].field) = static_cast<uint32_t>(rva);
    }
    *out = found;
    snprintf(why, why_size, "%s", "");
    return true;
}

bool search_values(const uint8_t *image, size_t size, ValueLayout *out, SigRow *rows, char *why, size_t why_size)
{
    Image im = {};
    im.p = image;
    im.size = size;
    if (image == nullptr || !parse(im)) {
        snprintf(why, why_size, "실행 파일의 머리말을 읽을 수 없습니다");
        return false;
    }
    uint64_t v[VALUE_WANTED][SIG_CAPTURES] = {};
    if (!vote_table(im, VALUES, VALUE_WANTED, "값을", rows, v, why, why_size))
        return false;
    const uint64_t world = v[0][0], treasury = v[1][0];
    if (world == 0 || world >= size || !im.has(world, 8)) {
        snprintf(why, why_size, "%s: 찾은 주소가 실행 파일 밖입니다", VALUES[0].label);
        return false;
    }
    if (!in_data(im, world, 8)) {
        snprintf(why, why_size, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", VALUES[0].label);
        return false;
    }
    if (world % 8 != 0) {                       // 포인터와 double 은 8 의 배수 자리에, float 는 4 의 배수 자리에 놓인다
        snprintf(why, why_size, "%s: 찾은 주소가 8 의 배수가 아닙니다", VALUES[0].label);
        return false;
    }
    if (!offset_ok(treasury)) {
        snprintf(why, why_size, "%s: 찾은 자리가 범위 밖입니다", VALUES[1].label);
        return false;
    }
    if (treasury % 8 != 0) {
        snprintf(why, why_size, "%s: 찾은 자리가 8 의 배수가 아닙니다", VALUES[1].label);
        return false;
    }
    for (int w = 2; w < VALUE_WANTED; w++) {    // 표 둘: (간격, 첫 칸). 칸이 float 라 간격은 4 의 배수다
        const uint64_t step = v[w][0], first = v[w][1];
        if (!offset_ok(first) || step == 0 || step % 4 != 0 || !offset_ok(first + step * (STOCK_SLOTS - 1) + 4)) {
            snprintf(why, why_size, "%s: 찾은 자리나 간격이 범위 밖입니다", VALUES[w].label);
            return false;
        }
        if (first % 4 != 0) {
            snprintf(why, why_size, "%s: 찾은 자리가 4 의 배수가 아닙니다", VALUES[w].label);
            return false;
        }
    }
    for (int i = 0; i < STOCK_SLOTS; i++) {
        const uint64_t slot = v[2][1] + v[2][0] * static_cast<uint64_t>(i);
        if (slot < treasury + 8 && treasury < slot + 4) {
            snprintf(why, why_size, "국고 칸과 재고 칸이 겹칩니다");
            return false;
        }
    }
    out->world_pointer = static_cast<uint32_t>(world);
    out->treasury = static_cast<uint32_t>(treasury);
    out->stock_step = static_cast<uint32_t>(v[2][0]);
    out->stock_first = static_cast<uint32_t>(v[2][1]);
    out->used_step = static_cast<uint32_t>(v[3][0]);
    out->used_first = static_cast<uint32_t>(v[3][1]);
    snprintf(why, why_size, "%s", "");
    return true;
}

// [a, a + a_bytes) 와 [b, b + b_bytes) 가 겹치는가.
bool overlap(uint64_t a, uint64_t a_bytes, uint64_t b, uint64_t b_bytes)
{
    return a < b + b_bytes && b < a + a_bytes;
}

// 더 쓰는 값: 서명은 한 번에 맞추고, 묶음마다 따로 판정한다.
int search_more(const uint8_t *image, size_t size, const ValueLayout *values, MoreLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    Image im = {};
    im.p = image;
    im.size = size;
    char broken[MORE_WHY] = "";
    Sig sigs[MAX_TABLE];
    SigHit hits[MAX_TABLE];
    if (image == nullptr || !parse(im))
        snprintf(broken, sizeof(broken), "실행 파일의 머리말을 읽을 수 없습니다");
    if (broken[0] != '\0' || !scan_table(im, MORE, MORE_WANTED, rows, sigs, hits, broken, sizeof(broken))) {
        for (int g = 0; g < MORE_GROUPS; g++)
            snprintf(why[g], MORE_WHY, "%s", broken);
        return 0;
    }
    uint64_t at[MORE_WANTED] = {};              // 저마다 찾은 자리. 못 찾았으면 0
    bool ok[MORE_GROUPS];
    for (int g = 0; g < MORE_GROUPS; g++) {
        ok[g] = true;
        why[g][0] = '\0';
    }
    for (int w = 0; w < MORE_WANTED; w++) {
        uint64_t value[SIG_CAPTURES] = {};
        char one[MORE_WHY] = "";
        bool good = vote_item(MORE, w, "값을", sigs, hits, value, one, sizeof(one));
        if (good && (!offset_ok(value[0]) || !offset_ok(value[0] + MORE[w].bytes))) {
            snprintf(one, sizeof(one), "%s: 찾은 자리가 범위 밖입니다", MORE[w].label);
            good = false;
        }
        if (good && value[0] % 4 != 0) {        // 칸이 float 다
            snprintf(one, sizeof(one), "%s: 찾은 자리가 4 의 배수가 아닙니다", MORE[w].label);
            good = false;
        }
        if (good) {
            at[w] = value[0];
        } else if (ok[MORE[w].group]) {         // 묶음의 까닭은 처음 것만 남긴다
            ok[MORE[w].group] = false;
            snprintf(why[MORE[w].group], MORE_WHY, "%s", one);
        }
    }
    // 저마다는 말이 되어도 서로 겹치면 어느 한쪽이 엉뚱한 것을 읽은 것이다 — 어느 쪽인지 모르므로 두 묶음 다 버린다
    for (int w = 0; w < MORE_WANTED; w++)
        for (int v = 0; v < w; v++) {
            if (at[w] == 0 || at[v] == 0 || !overlap(at[w], MORE[w].bytes, at[v], MORE[v].bytes))
                continue;
            for (int g : {MORE[w].group, MORE[v].group})
                if (ok[g]) {
                    ok[g] = false;
                    snprintf(why[g], MORE_WHY, "%s: 찾은 자리가 %s 의 자리와 겹칩니다", MORE[w].label, MORE[v].label);
                }
        }
    if (values != nullptr)                      // 값 묶음(돈 · 물자)을 찾았으면 그 칸들과도 겹치면 안 된다 — 새 묶음을 버린다
        for (int w = 0; w < MORE_WANTED; w++) {
            if (at[w] == 0)
                continue;
            bool hit = overlap(at[w], MORE[w].bytes, values->treasury, 8);
            for (int i = 0; i < STOCK_SLOTS && !hit; i++)
                hit = overlap(at[w], MORE[w].bytes, values->stock_first + static_cast<uint64_t>(values->stock_step) * static_cast<uint64_t>(i), 4);
            if (hit && ok[MORE[w].group]) {
                ok[MORE[w].group] = false;
                snprintf(why[MORE[w].group], MORE_WHY, "%s: 찾은 자리가 국고 칸이나 재고 칸과 겹칩니다", MORE[w].label);
            }
        }
    uint32_t found[MORE_WANTED] = {};
    int groups = 0;
    for (int w = 0; w < MORE_WANTED; w++)
        if (ok[MORE[w].group]) {
            found[w] = static_cast<uint32_t>(at[w]);
            groups |= 1 << MORE[w].group;
        }
    memcpy(out, found, sizeof(found));
    return groups;
}

// 연구: 일곱을 모두 찾아야 한다. 저마다 서명 셋이 모두 맞아야 하고(RESEARCH_NEED), 읽어 낸 값이 서로 · 상태 묶음과 맞아야 한다.
bool search_research(const uint8_t *image, size_t size, const GameAddresses &state, ResearchLayout *out, SigRow *rows, char *why,
                     size_t why_size)
{
    Image im = {};
    im.p = image;
    im.size = size;
    if (image == nullptr || !parse(im)) {
        snprintf(why, why_size, "실행 파일의 머리말을 읽을 수 없습니다");
        return false;
    }
    uint64_t v[RESEARCH_WANTED][SIG_CAPTURES] = {};
    if (!vote_table(im, RESEARCH, RESEARCH_WANTED, "값을", rows, v, why, why_size, RESEARCH_NEED))
        return false;
    static const uint32_t BYTES[4] = {8, 4, 8, 4};      // 기술 표 · 기술 수 · 부대 설계 표 · 부대 설계 수(포인터는 8, 수는 4바이트)
    for (int w = 0; w < 4; w++) {
        const uint64_t rva = v[w][0];
        if (rva == 0 || rva >= size || !im.has(rva, BYTES[w])) {
            snprintf(why, why_size, "%s: 찾은 주소가 실행 파일 밖입니다", RESEARCH[w].label);
            return false;
        }
        if (!in_data(im, rva, BYTES[w])) {
            snprintf(why, why_size, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", RESEARCH[w].label);
            return false;
        }
        if (rva % BYTES[w] != 0) {
            snprintf(why, why_size, "%s: 찾은 주소가 %u 의 배수가 아닙니다", RESEARCH[w].label, BYTES[w]);
            return false;
        }
        for (int u = 0; u < w; u++)
            if (overlap(rva, BYTES[w], v[u][0], BYTES[u])) {
                snprintf(why, why_size, "%s: 찾은 주소가 %s 의 자리와 겹칩니다", RESEARCH[w].label, RESEARCH[u].label);
                return false;
            }
        for (int s = 0; s < STATE_WANTED; s++) {
            const uint64_t other = state.*(STATE[s].field);
            if (other != 0 && overlap(rva, BYTES[w], other, STATE[s].bytes)) {
                snprintf(why, why_size, "%s: 찾은 주소가 %s 의 자리와 겹칩니다", RESEARCH[w].label, STATE[s].label);
                return false;
            }
        }
    }
    const uint64_t world = v[4][0], distance = v[4][1], lists = v[5][0], recompute = v[6][0];
    if (world == 0 || world >= size || !in_data(im, world, 8)) {
        snprintf(why, why_size, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", RESEARCH[4].label);
        return false;
    }
    if (state.region_table == 0 || world + distance != state.region_table) {      // 세계 객체 안에 지역 표가 있다 — 따로 찾은 것과 맞아야 한다
        snprintf(why, why_size, "%s: 지역 표까지의 거리가 맞지 않습니다", RESEARCH[4].label);
        return false;
    }
    const uint64_t list_bytes = static_cast<uint64_t>(LIST_STEP) * REGION_SLOTS;
    if (lists == 0 || lists % 8 != 0 || !in_data(im, world + lists, list_bytes)) {
        snprintf(why, why_size, "%s: 찾은 자리가 범위 밖입니다", RESEARCH[5].label);
        return false;
    }
    if (overlap(world + lists, list_bytes, state.region_table, 8ull * REGION_SLOTS)) {
        snprintf(why, why_size, "%s: 찾은 자리가 %s 의 자리와 겹칩니다", RESEARCH[5].label, STATE[5].label);
        return false;
    }
    if (recompute == 0 || recompute >= size || function_root(im, static_cast<uint32_t>(recompute)) != recompute) {
        snprintf(why, why_size, "%s: 찾은 주소가 함수의 시작이 아닙니다", RESEARCH[6].label);
        return false;
    }
    uint32_t found[RESEARCH_WANTED];
    for (int w = 0; w < RESEARCH_WANTED; w++)
        found[w] = static_cast<uint32_t>(v[w][0]);
    memcpy(out, found, sizeof(found));
    snprintf(why, why_size, "%s", "");
    return true;
}

// 서명마다의 결과 칸에 이름과 글을 채운다 — 찾기 전에. 머리말조차 못 읽은 이미지에서도 서명 표를 볼 수 있다(테스트와 srkit locate 가 쓴다).
template <class Row>
void name_rows(const Row *table, int n, SigRow *rows)
{
    if (rows == nullptr)
        return;
    for (int i = 0; i < n * STATE_SIGS; i++) {
        rows[i].name = table[i / STATE_SIGS].name;
        rows[i].text = table[i / STATE_SIGS].sigs[i % STATE_SIGS];
        rows[i].count = 0;
        rows[i].at = 0;
        rows[i].value = 0;
        rows[i].value2 = 0;
    }
}

}  // namespace

// 올라와 있는 실행 파일에는 읽을 수 없는 쪽이 있을 수 있다(보호된 구역). 그때도 죽지 않는다 — 여기서 예외가 새면 게임이 뜨다가 죽는다.
int search_items(const ActWanted *table, int n, int need, const uint8_t *image, size_t size, uint32_t *fields, SigRow *rows,
                 char (*why)[MORE_WHY])
{
    Image im = {};
    im.p = image;
    im.size = size;
    char broken[MORE_WHY] = "";
    Sig sigs[MAX_TABLE];
    SigHit hits[MAX_TABLE];
    if (image == nullptr || !parse(im))
        snprintf(broken, sizeof(broken), "실행 파일의 머리말을 읽을 수 없습니다");
    if (broken[0] != '\0' || !scan_table(im, table, n, rows, sigs, hits, broken, sizeof(broken))) {
        for (int w = 0; w < n; w++)
            snprintf(why[w], MORE_WHY, "%s", broken);
        return 0;
    }
    int found = 0;
    for (int w = 0; w < n; w++) {
        uint64_t value[SIG_CAPTURES] = {};
        why[w][0] = '\0';
        if (!vote_item(table, w, "주소를", sigs, hits, value, why[w], MORE_WHY, need))
            continue;
        const bool function = table[w].bytes == 0;
        if (function
            && (value[0] == 0 || value[0] >= size || function_root(im, static_cast<uint32_t>(value[0])) != value[0])) {
            snprintf(why[w], MORE_WHY, "%s: 찾은 주소가 함수의 시작이 아닙니다", table[w].label);
            continue;
        }
        if (table[w].bytes == ACT_TABLE) {
            if (value[0] == 0 || value[0] % 8 != 0 || value[0] + 8ull * (GUI_SET + 1) > size) {
                snprintf(why[w], MORE_WHY, "%s: 찾은 주소가 표의 자리가 아닙니다", table[w].label);
                continue;
            }
        } else if (table[w].bytes == ACT_REGION_TABLE) {
            if (value[0] == 0 || value[0] % 4 != 0 || value[0] + static_cast<uint64_t>(MUL_STEP) * REGION_SLOTS > size
                || !in_data(im, static_cast<uint32_t>(value[0]), MUL_STEP * REGION_SLOTS)) {
                snprintf(why[w], MORE_WHY, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", table[w].label);
                continue;
            }
        } else if (table[w].bytes == ACT_WORLD_CONSTANT) {
            if (value[0] == 0 || value[0] >= 0x1000000) {
                snprintf(why[w], MORE_WHY, "%s: 찾은 자리가 범위 밖입니다", table[w].label);
                continue;
            }
        } else if (table[w].bytes == ACT_CONSTANT) {
            if (!offset_ok(value[0])) {
                snprintf(why[w], MORE_WHY, "%s: 찾은 자리가 범위 밖입니다", table[w].label);
                continue;
            }
        } else if (!function && (value[0] == 0 || value[0] + table[w].bytes > size || value[0] % table[w].bytes != 0
                                 || !in_data(im, static_cast<uint32_t>(value[0]), table[w].bytes))) {
            snprintf(why[w], MORE_WHY, "%s: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다", table[w].label);
            continue;
        }
        fields[w] = static_cast<uint32_t>(value[0]);
        found |= 1 << w;
    }
    return found;
}

int search_acts(const uint8_t *image, size_t size, ActLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    return search_items(ACTS, ACT_WANTED, STATE_SIGS, image, size, &out->colonize, rows, why);   // 셋이 모두 맞아야 한다 — 엉뚱한 함수를 부르지 않는다
}

int search_mult(const uint8_t *image, size_t size, MultLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    return search_items(MULTS, MULT_WANTED, STATE_NEED, image, size, &out->mul_table, rows, why);
}

// __try 가 든 함수에는 소멸자가 있는 지역 변수를 둘 수 없어 찾는 일(search_legacy · search_state · search_values · search_more)과 따로 뗐다.
const char *locate_legacy(const uint8_t *image, size_t size, GameAddresses *out)
{
    __try {
        return search_legacy(image, size, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "실행 파일에 읽을 수 없는 곳이 있습니다";
    }
}

bool locate_state(const uint8_t *image, size_t size, GameAddresses *out, SigRow *rows, char *why, size_t why_size)
{
    name_rows(STATE, STATE_WANTED, rows);
    __try {
        return search_state(image, size, out, rows, why, why_size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snprintf(why, why_size, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return false;
    }
}

bool locate_values(const uint8_t *image, size_t size, ValueLayout *out, SigRow *rows, char *why, size_t why_size)
{
    name_rows(VALUES, VALUE_WANTED, rows);
    __try {
        return search_values(image, size, out, rows, why, why_size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snprintf(why, why_size, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return false;
    }
}

int locate_more(const uint8_t *image, size_t size, const ValueLayout *values, MoreLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    name_rows(MORE, MORE_WANTED, rows);
    *out = MoreLayout();
    __try {
        return search_more(image, size, values, out, rows, why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = MoreLayout();
        for (int g = 0; g < MORE_GROUPS; g++)
            snprintf(why[g], MORE_WHY, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return 0;
    }
}

int locate_acts(const uint8_t *image, size_t size, ActLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    name_rows(ACTS, ACT_WANTED, rows);
    *out = ActLayout();
    __try {
        return search_acts(image, size, out, rows, why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = ActLayout();
        for (int w = 0; w < ACT_WANTED; w++)
            snprintf(why[w], MORE_WHY, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return 0;
    }
}

int locate_mult(const uint8_t *image, size_t size, MultLayout *out, SigRow *rows, char (*why)[MORE_WHY])
{
    name_rows(MULTS, MULT_WANTED, rows);
    *out = MultLayout();
    __try {
        return search_mult(image, size, out, rows, why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = MultLayout();
        for (int w = 0; w < MULT_WANTED; w++)
            snprintf(why[w], MORE_WHY, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return 0;
    }
}

int locate_more_group(int wanted)
{
    return wanted >= 0 && wanted < MORE_WANTED ? MORE[wanted].group : -1;
}

bool locate_research(const uint8_t *image, size_t size, const GameAddresses &state, ResearchLayout *out, SigRow *rows, char *why,
                     size_t why_size)
{
    name_rows(RESEARCH, RESEARCH_WANTED, rows);
    __try {
        return search_research(image, size, state, out, rows, why, why_size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snprintf(why, why_size, "실행 파일에 읽을 수 없는 곳이 있습니다");
        return false;
    }
}

const char *locate_research_shape()
{
    static char text[640];
    snprintf(text, sizeof(text),
             "tech_size\t%x\ntech_kind\t%x\ntech_level\t%x\ntech_needs\t%x\ntech_need_count\t%x\ntech_owners\t%x\n"
             "design_size\t%x\ndesign_name\t%x\ndesign_class\t%x\ndesign_year\t%x\ndesign_open\t%x\ndesign_needs\t%x\n"
             "design_need_count\t%x\ndesign_hold_a\t%x\ndesign_hold_a_bit\t%x\ndesign_hold_b\t%x\ndesign_hold_b_bit\t%x\n"
             "design_owners\t%x\nowners_bytes\t%x\nlist_step\t%x\nnode_next\t%x\nnode_id\t%x\nnode_kind\t%x\nnode_flags\t%x\n"
             "node_gone\t%x\nnode_ended\t%x\nneed\t%x\n",
             TECH_SIZE, TECH_KIND, TECH_LEVEL, TECH_NEEDS, TECH_NEED_COUNT, TECH_OWNERS, DESIGN_SIZE, DESIGN_NAME, DESIGN_CLASS,
             DESIGN_YEAR, DESIGN_OPEN, DESIGN_NEEDS, DESIGN_NEED_COUNT, DESIGN_HOLD_A, DESIGN_HOLD_A_BIT, DESIGN_HOLD_B,
             DESIGN_HOLD_B_BIT, DESIGN_OWNERS, OWNERS_BYTES, LIST_STEP, NODE_NEXT, NODE_ID, NODE_KIND, NODE_FLAGS, NODE_GONE,
             NODE_ENDED, RESEARCH_NEED);
    return text;
}

bool locate_research_fits(const ResearchLayout &research, const ValueLayout &values, char *why, size_t why_size)
{
    const uint64_t at[4] = {research.tech_table, research.tech_count, research.design_table, research.design_count};
    static const uint32_t BYTES[4] = {8, 4, 8, 4};
    for (int w = 0; w < 4; w++)
        if (overlap(at[w], BYTES[w], values.world_pointer, 8)) {
            snprintf(why, why_size, "%s: 찾은 주소가 %s 의 자리와 겹칩니다", RESEARCH[w].label, VALUES[0].label);
            return false;
        }
    snprintf(why, why_size, "%s", "");
    return true;
}

bool locate_fits(const GameAddresses &state, const ValueLayout &values, char *why, size_t why_size)
{
    const uint64_t world = values.world_pointer;
    for (int w = 0; w < STATE_WANTED; w++) {
        const uint64_t rva = state.*(STATE[w].field);
        if (world < rva + STATE[w].bytes && rva < world + 8) {
            snprintf(why, why_size, "%s: 찾은 주소가 %s 의 자리와 겹칩니다", VALUES[0].label, STATE[w].label);
            return false;
        }
    }
    snprintf(why, why_size, "%s", "");
    return true;
}

uint32_t locate_function_root(const uint8_t *image, size_t size, uint32_t rva)
{
    __try {
        Image im = {};
        im.p = image;
        im.size = size;
        return image != nullptr && parse(im) ? function_root(im, rva) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
