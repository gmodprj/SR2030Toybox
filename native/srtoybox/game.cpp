#include "game.h"
#include "techs.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "features.h"
#include "log.h"
#include "products.h"
#include "regions.h"
#include "techs.h"

namespace {

const int MAX_REGIONS = 1024;   // 지역 표의 칸 수
const int MAX_NUMBER = 12900;   // 지역 번호의 상한(게임의 번호 → 지역 표가 이 크기다)
const int MAX_ITEMS = 65536;    // 기술 · 부대 설계 표의 자리 수의 상한(선행의 번호가 16비트다)
// 지역 하나의 연구 목록에서 따라가는 노드의 상한. 게임은 노드를 지우지 않는다(깃발로 표시한다) — 목록의 길이는 그 판에서 건 연구의
// 누적이라 오래 한 판에서는 수천을 넘는다. 끝나지 않는 목록(순환)은 길이가 아니라 순환으로 가린다(shoot)
const int MAX_NODES = 1 << 20;
const char *const NOT_PLAYING = "게임이 진행 중이 아닙니다";

std::mutex g_lock;
const uint8_t *g_base;
GameAddresses g_at;
bool g_located, g_told;
ValueLayout g_layout;     // 값의 자리
bool g_values;            // 그것을 찾았다
char g_values_why[200];   // 못 찾은 까닭
bool g_write_failed;      // 값 쓰기가 실패했다 — 이번 실행에서는 더 쓰지 않는다
MoreLayout g_more;        // 더 쓰는 값의 자리(못 찾은 묶음의 것은 0)
int g_more_groups;        // 찾은 묶음의 비트
char g_more_why[MORE_GROUPS][MORE_WHY];   // 묶음마다 못 찾은 까닭
ResearchLayout g_research;    // 연구의 자리
bool g_research_found;        // 그것을 찾았다
char g_research_why[200];     // 못 찾은 까닭
void *g_recompute;            // 게임의 "지역의 효과를 다시 셈"
MultLayout g_mult;            // 배율의 자리(못 찾은 것은 0)
char g_mult_why[MULT_WANTED][MORE_WHY];   // 찾을 것마다 못 찾은 까닭
void *g_colonize;             // 게임의 "식민지화"(못 찾았으면 nullptr)
void *g_fight;                // 게임의 "전쟁"(〃)
uint32_t g_map_pick;          // "지도에서 고른 지역"의 포인터가 든 전역(RVA. 못 찾았으면 0)
void *g_become;               // 게임의 "플레이 지역 바꾸기"(못 찾았으면 nullptr)
uint32_t g_index2, g_pointer2;   // 플레이어의 인덱스 · 포인터의 둘째 사본(RVA. 못 찾았으면 0)
void *g_elect, *g_news;       // 게임의 "새 지도자 세우기" · "알림"(못 찾았으면 nullptr)
uint32_t g_leaders, g_leader_id;   // 지도자 표의 포인터가 든 전역(RVA) · 지역 객체에서 지도자 번호의 자리(못 찾았으면 0)
void *g_treaty;               // 게임의 "조약"(못 찾았으면 nullptr)
uint32_t g_treaties;          // 지역 객체에서 조약 표의 자리(못 찾았으면 0)
void *g_annex;                // 게임의 "칸의 주인 바꾸기"(못 찾았으면 nullptr)
void *g_spawn;                // 게임의 "부대 만들기"(못 찾았으면 nullptr)
void *g_deploy;               // 게임의 "배치 칸 찾기"(못 찾았으면 nullptr)
uint32_t g_map_hex;           // "지도에서 고른 칸"이 든 전역(RVA. 못 찾았으면 0)
uint32_t g_ui_flags, g_gui, g_gui_vtable;   // 화면의 깃발 · GUI 객체의 포인터 · 그 객체의 함수 표(RVA. 못 찾았으면 0)
uint32_t g_units;             // 세계의 부대 목록의 첫 부대가 든 전역(RVA. 못 찾았으면 0)
uint32_t g_region_name;       // 지역 객체에서 이름 글의 포인터의 자리(못 찾았으면 0)
uint32_t g_game_over;         // 판이 끝난 꼴이 든 전역(RVA. 못 찾았으면 0)
uint32_t g_winners;           // 이긴 지역들의 묶음의 포인터가 든 전역(RVA. 못 찾았으면 0)
uint32_t g_picked_units;      // 게임에서 고른 부대들의 목록의 첫 노드가 든 전역(RVA. 못 찾았으면 0)
uint32_t g_upgrades;          // 세계 객체 안의 개량 보정 표의 자리(상수. 못 찾았으면 0)
uint32_t g_map;               // 지도 객체의 포인터가 든 전역(RVA. 못 찾았으면 0)
char g_act_why[ACT_WANTED][MORE_WHY];   // 부르는 함수마다 못 찾은 까닭


bool env_is_zero(const char *name)
{
    char value[8];
    return GetEnvironmentVariableA(name, value, sizeof(value)) == 1 && value[0] == '0';
}

// 탈출구: SRTOYBOX_READ=0 이면 게임을 읽지 않는다 — 모든 단추가 꺼진다(ToyBox 가 게임을 잘못 읽어 탈이 날 때 끄는 길이다)
// ToyBox 가 게임 안인데도 "게임 밖"으로 잘못 알아(다른 빌드, 보지 못한 화면) 단추가 꺼진 채 풀리지 않을 때 쓴다.
bool reading_wanted()
{
    static const bool wanted = !env_is_zero("SRTOYBOX_READ");
    return wanted;
}

// 탈출구: SRTOYBOX_WRITE=0 이면 게임의 메모리에 쓰지 않는다(자리는 찾아 로그에 적는다).
bool write_wanted()
{
    static const bool wanted = !env_is_zero("SRTOYBOX_WRITE");
    return wanted;
}

// 게임의 "지역의 효과를 다시 셈"을 부른다. 예외는 잡아 code 에 적는다.
bool guarded_recompute(Recompute recompute, void *world, int index, unsigned long *code)
{
    __try {
        recompute(world, index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

typedef void (*Colonize)(void *region, int other, bool flag);

// 게임의 "식민지화"를 부른다. 예외는 잡아 code 에 적는다.
bool guarded_colonize(Colonize colonize, void *region, int other, unsigned long *code)
{
    __try {
        colonize(region, other, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// 게임이 칸 하나를 가리킬 때 쓰는 묶음(치트 annex 가 스택에 만드는 그대로 — 0x28 바이트).
#pragma pack(push, 1)
struct HexRef {
    uint64_t map;             // 지도 객체
    uint64_t hex;             // 그 칸의 레코드
    uint64_t side_a, side_b;  // 칸마다 8바이트인 두 배열에서 그 칸의 자리
    uint16_t x, y;            // 가로 · 세로
    int32_t index;            // 칸의 번호(세로 × 너비 + 가로)
};
#pragma pack(pop)
static_assert(sizeof(HexRef) == 0x28, "칸의 묶음은 0x28 바이트다");

typedef void (*Annex)(HexRef *hex, int owner, int z1, int z2, bool f1, bool f2);
// 게임의 "부대 만들기": 내장 치트 spawnunit 이 부르는 그대로 — (지역 객체, 설계의 번호, 1, 칸, 0, 0, NULL, 0, 0, 0)
typedef void *(*Spawn)(void *region, int design, int count, uint32_t hex, bool f1, bool f2, void **out, bool f3, int z, bool f4);

// GUI 객체의 가상 함수 "깃발을 바꾼다": 내장 치트 fullmapshow 가 부르는 그대로 — (객체, 새 깃발, 0, NULL, 0, NULL, 0, 0, 1)
typedef void *(*GuiSet)(void *object, uint32_t flags, int a, void *b, int c, void *d, int e, int f, int g);

bool guarded_gui(GuiSet set, void *object, uint32_t flags, unsigned long *code)
{
    __try {
        set(object, flags, 0, nullptr, 0, nullptr, 0, 0, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// 게임의 "배치 칸 찾기": 지역 객체에 유형마다 적어 둔 칸을 확인해(없으면 찾아) out 에 적고 그 포인터를 돌려준다
typedef uint32_t *(*Deploy)(void *region, uint32_t *out, int move);

// 최강 부대 묶음: 설계마다 배치 칸을 얻어 "부대 만들기"를 한 번 부른다 — 내장 치트 darran 의 인자 그대로(수만 each. 치트는 7):
// (지역 객체, 설계의 번호, each, 그 칸, 0, 1, NULL, 1, 0, 0). 예외는 잡아 code 에 적는다 — 그때까지 부른 설계의 수는 made 에 있다.
bool guarded_best(Deploy deploy, Spawn spawn, void *region, const int *designs, const int *moves, int n, int each, int *made,
                  unsigned long *code)
{
    __try {
        for (int i = 0; i < n; i++) {
            uint32_t out = 0;
            const uint32_t *hex = deploy(region, &out, moves[i]);
            spawn(region, designs[i], each, hex != nullptr ? *hex : 0, false, true, nullptr, true, 0, false);
            ++*made;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// 게임의 "부대 만들기"를 설계마다 each 번 부른다. 예외는 잡아 code 에 적는다 — 그때까지 만든 수는 made 에 있다.
bool guarded_spawn(Spawn spawn, void *region, const int *designs, int n, int each, uint32_t hex, int *made, unsigned long *code)
{
    __try {
        for (int i = 0; i < n; i++)
            for (int k = 0; k < each; k++) {
                spawn(region, designs[i], 1, hex, false, false, nullptr, false, 0, false);
                ++*made;
            }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// 게임의 "칸의 주인 바꾸기"를 칸마다 부른다. 예외는 잡아 code 에 적는다. *done 에 부른 칸의 수.
bool guarded_annex(Annex annex, uint64_t map, uint64_t hexes, uint64_t side_a, uint64_t side_b, int width, const int *list, int n,
                   int owner, int *done, unsigned long *code)
{
    __try {
        for (int k = 0; k < n; k++) {
            const int i = list[k];
            HexRef ref;
            ref.map = map;
            ref.hex = hexes + static_cast<uint64_t>(i) * HEX_SIZE;
            ref.side_a = side_a + static_cast<uint64_t>(i) * 8;
            ref.side_b = side_b + static_cast<uint64_t>(i) * 8;
            ref.x = static_cast<uint16_t>(i % width);
            ref.y = static_cast<uint16_t>(i / width);
            ref.index = i;
            annex(&ref, owner, 0, 0, false, false);
            *done = k + 1;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

typedef void (*Treaty)(void *region, bool side, int treaty, bool make, int other, bool a, bool b, bool c);

// 게임의 "조약"을 네 번 부른다 — 플레이어 쪽에서 둘, 그 나라 쪽에서 둘(내장 치트 treaty 의 순서). 예외는 잡아 code 에 적는다.
bool guarded_alliance(Treaty treaty, void *mine, int me, void *theirs, int them, unsigned long *code)
{
    __try {
        treaty(mine, false, TREATY_ALLIANCE, true, them, true, true, false);
        treaty(mine, true, TREATY_ALLIANCE, true, them, true, true, false);
        treaty(theirs, false, TREATY_ALLIANCE, true, me, true, true, false);
        treaty(theirs, true, TREATY_ALLIANCE, true, me, true, true, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

typedef void (*Elect)(void *region, int a, int b);
typedef void (*News)(void *region, int n1, int n2, int n3, int n4, bool flag, void *nothing, int before, int index, int after, int z1,
                     int z2, int z3, bool z4);

// 게임의 "새 지도자 세우기"와 "알림"을 차례로 부른다(지도자가 죽은 날 게임이 하는 그대로). 예외는 잡아 code 에 적는다.
// leader 는 그 지역의 지도자 번호(word)가 든 주소 — 세운 뒤의 번호를 알림에 넘긴다.
bool guarded_succession(Elect elect, News news, void *region, const uint16_t *leader, int before, int index, unsigned long *code)
{
    __try {
        elect(region, 0, 0);
        news(region, NEWS_LEADER_GONE, NEWS_LEADER_GONE, NEWS_LEADER_GONE, NEWS_LEADER_GONE, true, nullptr, before, index, *leader, 0, 0, 0,
             false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

typedef void (*Become)(void *region, int zero);

// 게임의 "플레이 지역 바꾸기"를 부른다. 예외는 잡아 code 에 적는다.
bool guarded_become(Become become, void *region, unsigned long *code)
{
    __try {
        become(region, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

typedef void (*Fight)(void *region, bool a, int other, bool b, bool c);

// 게임의 "전쟁"을 부른다. 예외는 잡아 code 에 적는다.
bool guarded_fight(Fight fight, void *region, int other, unsigned long *code)
{
    __try {
        fight(region, true, other, false, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// 그 주소가 프로세스 힙에서 받은 size 바이트짜리 블록인가. 게임이 보유 묶음을 받는 힙이 ToyBox 가 받을 힙과 같은지 볼 때 쓴다 —
// 다른 힙의 블록을 걸면 게임이 그것을 풀 때 깨진다. 힙의 블록이 아닌 주소를 넘겨도 죽지 않게 예외를 잡는다.
bool heap_block(uint64_t address, size_t size)
{
    __try {
        const HANDLE heap = GetProcessHeap();
        void *const block = reinterpret_cast<void *>(address);
        return HeapValidate(heap, 0, block) != 0 && HeapSize(heap, 0, block) == size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 안전한 읽기: 낡은 포인터를 만나도 죽지 않는다.
bool peek(const void *address, void *out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, out, size, &got) != 0 && got == size;
}

// base 에서 offset 만큼 떨어진 값 하나.
template <class T>
bool peek_at(const uint8_t *base, uint64_t offset, T *out)
{
    return peek(base + offset, out, sizeof(T));
}

// 객체(주소)에서 offset 만큼 떨어진 값 하나.
template <class T>
bool peek_in(uint64_t object, uint64_t offset, T *out)
{
    return peek(reinterpret_cast<const void *>(object + offset), out, sizeof(T));
}

// 읽기 · 쓰기 쪽(PAGE_READWRITE)에 통째로 든 자리인가. WriteProcessMemory 는 실행 쪽(PAGE_EXECUTE_READ)이면 보호를 풀고
// 쓴다 — 낡은 포인터가 코드를 가리켜도 쓰지 않게 먼저 가린다.
bool writable(uint64_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) == 0)
        return false;
    const uint64_t end = reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
    return info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE && address + size <= end;
}

// 안전한 쓰기: 쓰고, 다시 읽어 그 값인지 본다. 게임이 그 사이에 같은 칸을 고쳤을 수 있으므로 한 번 더 해 본다.
bool poke(uint64_t address, const void *data, size_t size)
{
    uint8_t back[8] = {};
    void *const where = reinterpret_cast<void *>(address);
    if (size > sizeof(back))
        return false;
    for (int attempt = 0; attempt < 2; attempt++) {
        SIZE_T done = 0;
        if (!writable(address, size) || !WriteProcessMemory(GetCurrentProcess(), where, data, size, &done) || done != size)
            return false;
        if (peek(where, back, size) && memcmp(back, data, size) == 0)
            return true;
    }
    return false;
}

// 칸(1바이트나 4바이트)의 비트들만 켜거나 끈다 — 원자적으로. 그 칸의 다른 비트는 읽어서 되쓰지 않으므로, 그 순간 게임이 그것들을
// 바꿔도 덮어쓰지 않는다(보유 묶음의 한 바이트에는 여덟 나라의 비트가 함께 있다). 쓴 뒤 그 비트들을 다시 읽어 본다(아니면 한 번 더).
bool poke_bits(uint64_t address, uint32_t mask, bool on, size_t size)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!writable(address, size))
            return false;
        __try {
            if (size == 1 && on)
                InterlockedOr8(reinterpret_cast<char *>(address), static_cast<char>(mask));
            else if (size == 1)
                InterlockedAnd8(reinterpret_cast<char *>(address), static_cast<char>(~mask));
            else if (on)
                InterlockedOr(reinterpret_cast<LONG *>(address), static_cast<LONG>(mask));
            else
                InterlockedAnd(reinterpret_cast<LONG *>(address), static_cast<LONG>(~mask));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        uint32_t back = 0;
        if (peek(reinterpret_cast<const void *>(address), &back, size) && (back & mask) == (on ? mask : 0u))
            return true;
    }
    return false;
}

// 지역 객체의 머리: +0 상태(dword), +4 자기 인덱스(word), +8 지역 번호(word).
// 상태는 2030 - 세계에서 읽은 분포로 본 것이다 [확인: 실행 / 뜻은 추정]: 0 쓸 수 없다, 1 유엔, 2 사람이 고른 나라,
// 3 AI 가 맡은 나라(223개), 5 이번 판에 없는 지역(서독 · 소련 · 네브래스카 등 128개).
struct Region {
    uint32_t alive;
    int index, number;
};

bool peek_region(uint64_t pointer, Region *out)
{
    uint8_t raw[10];
    uint16_t index = 0, number = 0;
    if (pointer == 0 || !peek(reinterpret_cast<const void *>(pointer), raw, sizeof(raw)))
        return false;
    memcpy(&out->alive, raw, 4);
    memcpy(&index, raw + 4, 2);
    memcpy(&number, raw + 8, 2);
    out->index = index;
    out->number = number;
    return true;
}

bool usable(const Region &r, int index)
{
    return r.alive != 0 && r.index == index && r.number > 0 && r.number < MAX_NUMBER;
}

// 이번 판에 실제로 있는 나라인가(사람이나 AI 가 맡고 있다). 창의 나라 목록에는 이것만 올린다 —
// 없는 지역에 "이 나라로 플레이" 같은 치트를 넣게 두지 않는다.
bool in_play(const Region &r)
{
    return r.alive == 2 || r.alive == 3;
}

// 게임의 상태와, 진행 중이면 플레이어 지역 객체의 주소(object).
GameState read_player(const uint8_t *base, const GameAddresses &at, uint64_t *object)
{
    GameState s;
    uint8_t multiplayer = 0;
    int32_t program = 0, mode = 0, index = 0, count = 0;
    uint64_t pointer = 0, slot = 0;
    if (base == nullptr || !peek_at(base, at.multiplayer, &multiplayer)
        || !peek_at(base, at.program_state, &program) || !peek_at(base, at.mode_state, &mode)
        || !peek_at(base, at.player_index, &index) || !peek_at(base, at.player_pointer, &pointer)
        || !peek_at(base, at.region_count, &count))
        return s;
    s.known = true;
    s.multiplayer = multiplayer != 0;
    if (mode != 2 || program != 1 || pointer == 0)
        return s;   // 게임 밖(메뉴 · 로비). 인덱스는 낡은 값이 남으므로 보지 않는다

    // 게임 안이라면 읽은 값들이 서로 맞아야 한다. 안 맞으면 구조가 바뀐 빌드일 수 있다 — 모르는 것으로 친다
    Region player = {};
    if (index < 1 || index >= MAX_REGIONS || count < 1 || count >= MAX_REGIONS
        || !peek_at(base, at.region_table + 8ull * static_cast<uint64_t>(index), &slot) || slot != pointer
        || !peek_region(pointer, &player) || !usable(player, index)) {
        s.known = false;
        return s;
    }
    s.in_game = true;
    s.player = player.number;
    s.index = index;
    s.regions = count;
    *object = pointer;
    return s;
}

// 이 물자를 이번 판에서 쓰는가: 세계 자료 객체의 그 칸(float)이 0 보다 크다. 읽을 수 없으면 false.
bool slot_used(const uint8_t *base, const ValueLayout &layout, int slot)
{
    uint64_t world = 0;
    float used = 0;
    return peek_at(base, layout.world_pointer, &world) && world != 0
        && peek_in(world, layout.used_first + static_cast<uint64_t>(layout.used_step) * static_cast<uint64_t>(slot), &used)
        && std::isfinite(used) && used > 0.0f;
}

bool located(const uint8_t **base, GameAddresses *at)
{
    std::lock_guard<std::mutex> lock(g_lock);
    *base = g_base;
    *at = g_at;
    return g_located && reading_wanted();
}

// 맞지 않은 서명을 로그에 적는다 — 셋 가운데 둘로 찾았어도, 다음 업데이트에서 깨질 것을 미리 안다.
void log_unmatched(const SigRow &row, int number)
{
    if (row.count != 1)
        log_line("맞지 않은 서명: %s #%d (%s)", row.name, number, row.count == 0 ? "안 맞음" : "여러 번 맞음");
}

void log_unmatched(const SigRow *rows, int n)
{
    for (int i = 0; i < n; i++)
        log_unmatched(rows[i], i % STATE_SIGS + 1);
}

// 그 번호의 나라(이번 판에 실제로 있는 것)의 객체와 인덱스. 없으면 false.
bool find_region(const uint8_t *base, const GameAddresses &at, int number, uint64_t *object, int *index)
{
    int32_t count = 0;
    if (number <= 0 || !peek_at(base, at.region_count, &count) || count < 1 || count >= MAX_REGIONS)
        return false;
    std::vector<uint64_t> table(static_cast<size_t>(count) + 1);
    if (!peek(base + at.region_table, table.data(), table.size() * sizeof(uint64_t)))
        return false;
    for (int i = 1; i <= count; i++) {
        Region r = {};
        if (peek_region(table[static_cast<size_t>(i)], &r) && usable(r, i) && in_play(r) && r.number == number) {
            *object = table[static_cast<size_t>(i)];
            *index = i;
            return true;
        }
    }
    return false;
}

// 플레이어와 그 번호의 나라: 두 객체의 주소와 인덱스. 판정은 Wrote 로(Done 이면 넷을 채웠다).
Wrote find_pair(const uint8_t *base, const GameAddresses &at, int number, bool writing, uint64_t *mine, int *me, uint64_t *theirs,
                int *them)
{
    const GameState s = read_player(base, at, mine);
    if (!s.in_game || (writing && s.multiplayer))
        return Wrote::NotInGame;
    *me = s.index;   // read_player 가 확인한 인덱스 — 전역을 다시 읽으면 그 사이에 바뀐 값을 쓸 수 있다
    if (number == s.player || !find_region(base, at, number, theirs, them))
        return Wrote::NoTarget;
    return Wrote::Done;
}

// 한 나라와의 관계가 놓인 여섯 칸의 주소: [0 … 2] 플레이어 객체의 그 나라 칸(관계 표 둘 · 전쟁 명분), [3 … 5] 그 나라 객체의 플레이어 칸.
void relation_cells(const MoreLayout &more, uint64_t mine, int me, uint64_t theirs, int them, uint64_t *cells)
{
    const uint32_t tables[3] = {more.relation[0], more.relation[1], more.casus};
    for (int i = 0; i < 3; i++) {
        cells[i] = mine + tables[i] + 4ull * static_cast<uint64_t>(them);
        cells[3 + i] = theirs + tables[i] + 4ull * static_cast<uint64_t>(me);
    }
}

// float 여러 칸을 쓴다. 쓰기 전에 모든 칸이 읽기 · 쓰기 쪽인지 본다 — 반쪽만 쓰고 멈추는 일을 줄인다. done 에 쓴 칸의 수.
Wrote poke_floats(const uint64_t *cells, const float *values, int n, int *done)
{
    *done = 0;
    for (int i = 0; i < n; i++)
        if (!writable(cells[i], sizeof(float)))
            return Wrote::Failed;
    for (int i = 0; i < n; i++) {
        if (!poke(cells[i], &values[i], sizeof(float)))
            return Wrote::Failed;
        ++*done;
    }
    return Wrote::Done;
}

// 값을 쓸 수 없는 까닭(found: 그 묶음의 자리를 찾았는가, why: 못 찾은 까닭). g_lock 을 쥔 채로 부른다.
std::string off_text(bool found, const char *why)
{
    if (!g_located || !reading_wanted())
        return "게임 상태를 읽을 수 있을 때만 씁니다.";
    if (!found)
        return std::string("이 게임 판에서는 쓸 수 없습니다 (") + why + ")";
    if (!write_wanted())
        return "값 쓰기를 껐습니다 (SRTOYBOX_WRITE=0)";
    if (g_write_failed)
        return "값 쓰기가 실패해 껐습니다. 게임을 다시 시작하면 다시 시도합니다.";
    return std::string();
}

// 묶음의 비트 → 까닭이 든 줄(0 지식, 1 여론, 2 관계, 3 인구, 4 지지율). 묶음이 아니면 -1.
int more_index(int group)
{
    for (int g = 0; g < MORE_GROUPS; g++)
        if (group == 1 << g)
            return g;
    return -1;
}

// 지금의 "게임"과 더 쓰는 값의 자리. 그 묶음을 쓸 수 없으면 false.
bool more_ready(int group, const uint8_t **base, GameAddresses *at, MoreLayout *more)
{
    if (!game_more_off(group).empty())
        return false;
    std::lock_guard<std::mutex> lock(g_lock);
    *base = g_base;
    *at = g_at;
    *more = g_more;
    return true;
}

// 더 쓰는 값의 쓰기가 실패했다: 이번 실행에서는 값 쓰기 전체를 끄고 로그에 적는다.
Wrote write_failed(const std::string &what, int cells, int done)
{
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_write_failed = true;
    }
    if (done > 0)
        log_line("값 쓰기 실패 (%s, %d칸 가운데 %d칸을 쓴 뒤) — 값 쓰기를 끕니다", what.c_str(), cells, done);
    else
        log_line("값 쓰기 실패 (%s) — 값 쓰기를 끕니다", what.c_str());
    return Wrote::Failed;
}

// 프로세스의 게임에 값 하나를 쓴다. slot 이 음수면 국고.
Wrote write(int slot, double value)
{
    if (!game_values_off().empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ValueLayout layout = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        layout = g_layout;
    }
    const Wrote wrote = slot < 0 ? write_treasury(base, at, layout, value) : write_stock(base, at, layout, slot, static_cast<float>(value));
    if (wrote == Wrote::Failed) {
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_write_failed = true;
        }
        log_line("값 쓰기 실패 (%s) — 값 쓰기를 끕니다", slot < 0 ? "국고" : product_label(slot).c_str());
    }
    return wrote;
}

// 읽을 수 있다고 물어 둔 구역 [lo, hi).
struct Span {
    uint64_t lo = 0, hi = 0;
};

bool copy_guarded(const void *from, void *to, size_t size)
{
    __try {
        memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 작은 블록 수천 개(보유 묶음 · 부대 설계의 이름)를 읽을 때의 peek. ReadProcessMemory 를 블록마다 부르면 스냅숏 한 번이 수십 ms 다 —
// 그 구역이 읽을 수 있는 쪽인지 VirtualQuery 로 한 번 물어 두고, 그 안의 블록은 바로 읽는다(예외 가드 안에서 — 그사이 풀린 쪽이면 실패다).
// 묻지 않고 바로 읽지는 않는다: 가드 쪽(PAGE_GUARD)을 건드리면 가드가 지워진다. 그런 구역과 구역에 걸친 블록은 peek 으로 읽는다.
bool peek_near(Span *span, uint64_t address, void *out, size_t size)
{
    const void *const from = reinterpret_cast<const void *>(address);
    if (address < span->lo || address + size > span->hi) {
        MEMORY_BASIC_INFORMATION info;
        span->lo = span->hi = 0;
        if (VirtualQuery(from, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT
            || (info.Protect != PAGE_READWRITE && info.Protect != PAGE_READONLY))
            return peek(from, out, size);
        const uint64_t lo = reinterpret_cast<uint64_t>(info.BaseAddress), hi = lo + info.RegionSize;
        if (address < lo || address + size > hi)
            return peek(from, out, size);
        span->lo = lo;
        span->hi = hi;
    }
    return copy_guarded(from, out, size);
}

// 부대 설계의 이름(UTF-8)을 번호마다 기억해 둔다 — 이름은 판이 도는 동안 바뀌지 않고, 스냅숏은 0.5초마다 뜬다.
// 이름의 포인터가 달라졌으면 다시 읽고, 표가 달라졌거나 스냅숏이 한동안(NAMES_STALE_MS) 없었으면 모두 버린다(다른 판일 수 있다).
const int NAME_BYTES = 96;                        // 이름은 95바이트까지 읽는다(원본의 가장 긴 이름이 52자, 한글은 한 글자가 3바이트다)
const unsigned long long NAMES_STALE_MS = 2000;
std::mutex g_names_lock;
std::vector<std::pair<uint64_t, std::string>> g_names;
uint64_t g_names_table;
unsigned long long g_names_at;

// 연구의 스냅숏과, 그것을 쓸 때 필요한 주소들.
struct Shot {
    ResearchTables tables;
    std::vector<uint64_t> tech_owners, design_owners;   // tables 의 행과 같은 순서: 보유 묶음의 주소(없으면 0)
    std::vector<uint64_t> tech_cells, design_cells;     // 〃 레코드에서 그 포인터가 든 칸의 주소
    std::vector<uint64_t> nodes;                        // tables.queue 와 같은 순서: 노드의 주소
    uint64_t world = 0;                                 // 세계 객체의 주소
    uint64_t player = 0;                                // 플레이어 지역 객체의 주소
    int me = 0;                                         // 플레이어의 인덱스
    bool multiplayer = false;
};

// 레코드의 한 칸.
template <class T>
T field(const uint8_t *record, uint32_t offset)
{
    T value;
    memcpy(&value, record + offset, sizeof(T));
    return value;
}

// 보유 묶음에서 플레이어(me)와 고른 나라(them. 없으면 0)의 비트, 그리고 플레이어를 뺀 보유 나라의 수. 묶음이 없으면(0) 아무도 없다.
// first 에는 플레이어가 아닌 보유 나라 가운데 인덱스가 낮은 둘(없으면 0). 비트 0 은 나라가 아니다(지역 인덱스는 1 부터) — 세지 않는다.
bool owners_of(Span *span, uint64_t owners, int me, int them, bool *mine, bool *picked, int *others, int *first)
{
    *mine = *picked = false;
    *others = 0;
    first[0] = first[1] = 0;
    if (owners == 0)
        return true;
    uint8_t bits[OWNERS_BYTES];
    if (!peek_near(span, owners, bits, sizeof(bits)))
        return false;
    bits[0] &= 0xFE;
    int count = 0;
    for (uint8_t byte : bits)
        for (; byte != 0; byte = static_cast<uint8_t>(byte & (byte - 1)))
            count++;
    for (int i = 1, found = 0; i < OWNERS_BYTES * 8 && found < 2; i++)
        if (i != me && (bits[i / 8] >> (i % 8) & 1) != 0)
            first[found++] = i;
    *mine = (bits[me / 8] >> (me % 8) & 1) != 0;
    *picked = them > 0 && (bits[them / 8] >> (them % 8) & 1) != 0;
    *others = count - (*mine ? 1 : 0);
    return true;
}

// 표 둘과 플레이어의 연구 목록을 읽는다. 게임을 진행 중이 아니면 why 는 NOT_PLAYING 이다(멀티플레이는 진행 중으로 읽고 표시만 한다).
bool shoot(const uint8_t *base, const GameAddresses &at, const ResearchLayout &r, int picked, Shot *shot, std::string *why)
{
    uint64_t player = 0, other = 0, tech_table = 0, design_table = 0, node = 0;
    int32_t tech_count = 0, design_count = 0;
    int them = 0;
    const GameState s = read_player(base, at, &player);
    // 플레이어의 인덱스는 보유 묶음의 비트와 "다시 셈"의 인자가 된다 — 지역 표의 마지막 인덱스(지역 수)보다 크면 쓰지 않는다
    const int me = s.index;   // read_player 가 확인한 값(전역을 다시 읽지 않는다)
    if (!s.in_game || me > s.regions) {
        *why = NOT_PLAYING;
        return false;
    }
    if (picked > 0 && picked != s.player && !find_region(base, at, picked, &other, &them))
        them = 0;
    if (!peek_at(base, r.tech_count, &tech_count) || !peek_at(base, r.design_count, &design_count)
        || !peek_at(base, r.tech_table, &tech_table) || !peek_at(base, r.design_table, &design_table)) {
        *why = "표의 전역을 읽을 수 없습니다";
        return false;
    }
    if (tech_count < 2 || tech_count > MAX_ITEMS || design_count < 2 || design_count > MAX_ITEMS || tech_table == 0 || design_table == 0) {
        *why = "표가 없거나 자리 수가 범위 밖입니다";
        return false;
    }
    Span span;
    shot->me = me;
    shot->player = player;
    shot->multiplayer = s.multiplayer;
    shot->world = reinterpret_cast<uint64_t>(base) + r.world;
    ResearchTables &t = shot->tables;
    std::vector<uint64_t> region_table(static_cast<size_t>(s.regions) + 1);
    t.regions.assign(region_table.size(), 0);
    if (peek(base + at.region_table, region_table.data(), region_table.size() * sizeof(uint64_t)))
        for (int i = 1; i <= s.regions; i++) {
            Region region = {};
            if (peek_region(region_table[static_cast<size_t>(i)], &region) && usable(region, i))
                t.regions[static_cast<size_t>(i)] = region.number;
        }
    t.tech_slots = tech_count;
    t.design_slots = design_count;
    if (!peek(reinterpret_cast<const void *>(player + REGION_MASK), &t.region_mask, sizeof(t.region_mask)))
        t.region_mask = 0;                      // 읽지 못했다 — 마스크가 있는 항목이 모두 막힌다(실패로 치지 않는다)
    if (!peek(reinterpret_cast<const void *>(player + REGION_YEAR), &t.year, sizeof(t.year)) || !std::isfinite(t.year))
        t.year = 0;                             // 읽지 못했다 — 단종으로 막는 것이 없다

    std::vector<uint8_t> raw(static_cast<size_t>(tech_count) * TECH_SIZE);
    if (!peek(reinterpret_cast<const void *>(tech_table), raw.data(), raw.size())) {
        *why = "기술 표를 읽을 수 없습니다";
        return false;
    }
    for (int i = 1; i < tech_count; i++) {
        const uint8_t *record = raw.data() + static_cast<size_t>(i) * TECH_SIZE;
        if (record[TECH_KIND] == 0)
            continue;                           // 빈 자리
        TechRow row;
        row.id = i;
        row.kind = record[TECH_KIND];
        row.level = record[TECH_LEVEL];
        for (int n = 0; n < TECH_NEED_COUNT; n++)
            row.needs[n] = field<uint16_t>(record, TECH_NEEDS + 2 * static_cast<uint32_t>(n));
        const uint64_t owners = field<uint64_t>(record, TECH_OWNERS);
        if (!owners_of(&span, owners, me, them, &row.mine, &row.picked, &row.others, row.owners)) {
            *why = "기술 " + std::to_string(i) + " 의 보유 묶음을 읽을 수 없습니다";
            return false;
        }
        row.housed = owners != 0;
        row.mask = field<uint64_t>(record, TECH_MASK);
        t.techs.push_back(row);
        shot->tech_owners.push_back(owners);
        shot->tech_cells.push_back(tech_table + static_cast<uint64_t>(i) * TECH_SIZE + TECH_OWNERS);
    }

    raw.assign(static_cast<size_t>(design_count) * DESIGN_SIZE, 0);
    if (!peek(reinterpret_cast<const void *>(design_table), raw.data(), raw.size())) {
        *why = "부대 설계 표를 읽을 수 없습니다";
        return false;
    }
    std::lock_guard<std::mutex> names_lock(g_names_lock);
    const unsigned long long now = GetTickCount64();
    if (g_names_table != design_table || g_names.size() != static_cast<size_t>(design_count) || now - g_names_at > NAMES_STALE_MS)
        g_names.assign(static_cast<size_t>(design_count), std::pair<uint64_t, std::string>());
    g_names_table = design_table;
    g_names_at = now;
    for (int i = 1; i < design_count; i++) {
        const uint8_t *record = raw.data() + static_cast<size_t>(i) * DESIGN_SIZE;
        const uint64_t name = field<uint64_t>(record, DESIGN_NAME);
        if (name == 0)
            continue;                           // 빈 자리
        DesignRow row;
        row.id = i;
        std::pair<uint64_t, std::string> &known = g_names[static_cast<size_t>(i)];
        if (known.first != name) {              // 읽지 못하면 빈 글로 둔다(창이 "#번호"로 적는다) — 다음 스냅숏에 다시 읽지는 않는다
            char text[NAME_BYTES] = {};
            known.first = name;
            known.second = peek_near(&span, name, text, sizeof(text) - 1) ? game_text_to_utf8(text, sizeof(text) - 1) : std::string();
        }
        row.name = known.second;
        row.cls = record[DESIGN_CLASS];
        row.year = record[DESIGN_YEAR];
        row.open = field<uint16_t>(record, DESIGN_OPEN) != 0;
        row.held = (field<uint32_t>(record, DESIGN_HOLD_A) & DESIGN_HOLD_A_BIT) != 0
            || (field<uint32_t>(record, DESIGN_HOLD_B) & DESIGN_HOLD_B_BIT) != 0;
        for (int n = 0; n < DESIGN_NEED_COUNT; n++)
            row.needs[n] = field<uint16_t>(record, DESIGN_NEEDS + 2 * static_cast<uint32_t>(n));
        const uint64_t owners = field<uint64_t>(record, DESIGN_OWNERS);
        if (!owners_of(&span, owners, me, them, &row.mine, &row.picked, &row.others, row.owners)) {
            *why = "부대 설계 " + std::to_string(i) + " 의 보유 묶음을 읽을 수 없습니다";
            return false;
        }
        row.housed = owners != 0;
        row.mask = field<uint64_t>(record, DESIGN_MASK);
        // 깃발 A 의 비트 1 · 깃발 B 의 0x1000000 — 둘 다 게임의 연구 목록에 나오지 않는다(A: 0x7abc4e, B: 0x7abc5c 의 전역이 2 인 판 — 2030 - 세계에서 봤다, U7)
        row.special = (field<uint32_t>(record, DESIGN_HOLD_A) & DESIGN_SPECIAL_BIT) != 0 || (field<uint32_t>(record, DESIGN_HOLD_B) & DESIGN_HOLD_B_BIT) != 0;
        row.upgrade_to = field<uint16_t>(record, DESIGN_UPGRADE_TO);
        row.upgrade_to_2 = field<uint16_t>(record, DESIGN_UPGRADE_TO_2);
        row.retired = record[DESIGN_RETIRED];
        row.kept = (record[DESIGN_KEPT] & DESIGN_KEPT_BIT) != 0;
        t.designs.push_back(row);
        shot->design_owners.push_back(owners);
        shot->design_cells.push_back(design_table + static_cast<uint64_t>(i) * DESIGN_SIZE + DESIGN_OWNERS);
    }

    // 플레이어의 연구 목록: 머리 노드부터 "다음"을 따라간다
    if (!peek(reinterpret_cast<const void *>(shot->world + r.lists + static_cast<uint64_t>(me) * LIST_STEP), &node, sizeof(node))) {
        *why = "연구 목록을 읽을 수 없습니다";
        return false;
    }
    // 순환은 Brent 의 방법으로 잡는다: 표시(mark)를 2의 거듭제곱 걸음마다 지금 노드로 옮기고, 다시 그 노드에 오면 순환이다
    uint64_t mark = 0;
    size_t reach = 1, walked = 0;
    while (node != 0) {
        uint8_t record[NODE_FLAGS + 8];
        if (node == mark || shot->nodes.size() >= static_cast<size_t>(MAX_NODES)) {
            *why = "연구 목록이 끝나지 않습니다";
            return false;
        }
        if (walked == reach) {
            mark = node;
            reach *= 2;
            walked = 0;
        }
        walked++;
        if (!peek(reinterpret_cast<const void *>(node), record, sizeof(record))) {
            *why = "연구 목록의 노드를 읽을 수 없습니다";
            return false;
        }
        QueueRow row;
        row.id = field<int32_t>(record, NODE_ID);
        row.kind = record[NODE_KIND];
        row.flags[0] = field<uint32_t>(record, NODE_FLAGS);
        row.flags[1] = field<uint32_t>(record, NODE_FLAGS + 4);
        t.queue.push_back(row);
        shot->nodes.push_back(node);
        node = field<uint64_t>(record, NODE_NEXT);
    }
    research_mark_queued(&t);
    return true;
}

// 바꿀 항목 하나: 보유 묶음의 주소(없으면 0)와 레코드에서 그 포인터가 든 칸.
struct Item {
    bool tech;
    int id;
    uint64_t owners, cell;
};

// 계획의 번호들(오름차순)에 묶음과 칸의 주소를 붙인다. rows 도 번호순이다.
template <class Row>
void attach(const std::vector<int> &ids, const std::vector<Row> &rows, const std::vector<uint64_t> &owners,
            const std::vector<uint64_t> &cells, bool tech, std::vector<Item> *items)
{
    size_t k = 0;
    for (size_t i = 0; i < rows.size() && k < ids.size(); i++)
        if (rows[i].id == ids[k]) {
            items->push_back({tech, ids[k], owners[i], cells[i]});
            k++;
        }
}

// 쓰기가 막혔다: 어디서(where), 몇 칸 가운데 몇 칸을 쓴 뒤인지 적는다.
Wrote research_failed(ResearchDone *done, const std::string &where)
{
    done->why = where;
    return Wrote::Failed;
}

std::string item_name(const Item &item)
{
    return std::string(item.tech ? "기술 " : "부대 설계 ") + std::to_string(item.id);
}

}  // namespace

GameState read_game(const uint8_t *base, const GameAddresses &at)
{
    uint64_t object = 0;
    return read_player(base, at, &object);
}

std::vector<int> read_regions(const uint8_t *base, const GameAddresses &at)
{
    std::vector<int> out;
    int32_t count = 0;
    if (!read_game(base, at).in_game || !peek_at(base, at.region_count, &count) || count < 1 || count >= MAX_REGIONS)
        return out;   // 지역 수는 방금 다시 읽은 값이다 — 그 사이에 바뀌었을 수 있다
    std::vector<uint64_t> table(static_cast<size_t>(count) + 1);
    if (!peek(base + at.region_table, table.data(), table.size() * sizeof(uint64_t)))
        return out;
    for (int i = 1; i <= count; i++) {
        Region r = {};
        if (peek_region(table[static_cast<size_t>(i)], &r) && usable(r, i) && in_play(r))
            out.push_back(r.number);
    }
    std::sort(out.begin(), out.end());
    return out;
}

GameValues read_values(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout)
{
    GameValues v;
    uint64_t object = 0;
    if (!read_player(base, at, &object).in_game || !peek_in(object, layout.treasury, &v.treasury))
        return v;
    for (int i = 0; i < STOCK_SLOTS; i++) {
        const uint64_t slot = layout.stock_first + static_cast<uint64_t>(layout.stock_step) * static_cast<uint64_t>(i);
        if (!peek_in(object, slot, &v.stock[i]) || !std::isfinite(v.stock[i]))
            return v;   // 재고 칸이 수가 아니면 구조가 바뀐 것일 수 있다 — 모르는 것으로 친다(쓰지 않는 물자의 칸이어도)
    }
    uint64_t world = 0;
    float probe = 0;
    if (!peek_at(base, layout.world_pointer, &world) || world == 0 || !peek_in(world, layout.used_first, &probe))
        return v;       // 세계 자료를 읽을 수 없다 — 어느 물자를 쓰는지 모른다
    for (int i = 0; i < STOCK_SLOTS; i++)
        v.used[i] = slot_used(base, layout, i);
    v.ok = true;
    return v;
}

Wrote write_treasury(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout, double value)
{
    uint64_t object = 0;
    if (!std::isfinite(value))
        return Wrote::BadValue;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    return poke(object + layout.treasury, &value, sizeof(value)) ? Wrote::Done : Wrote::Failed;
}

Wrote write_stock(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout, int slot, float value)
{
    uint64_t object = 0;
    if (slot < 0 || slot >= STOCK_SLOTS || !std::isfinite(value) || value < 0.0f)
        return Wrote::BadValue;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (!slot_used(base, layout, slot))
        return Wrote::NotUsed;
    const uint64_t where = object + layout.stock_first + static_cast<uint64_t>(layout.stock_step) * static_cast<uint64_t>(slot);
    return poke(where, &value, sizeof(value)) ? Wrote::Done : Wrote::Failed;
}

GameMore read_more(const uint8_t *base, const GameAddresses &at, const MoreLayout &more)
{
    GameMore v;
    uint64_t object = 0;
    if (!read_player(base, at, &object).in_game || (more.tech != 0 && !peek_in(object, more.tech, &v.tech)))
        return v;
    for (int i = 0; i < 3; i++)
        if (more.opinion[i] != 0 && !peek_in(object, more.opinion[i], &v.opinion[i]))
            return v;
    for (int i = 0; i < 2; i++)
        if (more.people[i] != 0 && !peek_in(object, more.people[i], &v.people[i]))
            return v;
    if (more.approval != 0 && !peek_in(object, more.approval, &v.approval))
        return v;
    v.ok = true;
    return v;
}

Relation read_relation(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int number)
{
    Relation r;
    uint64_t mine = 0, theirs = 0, cells[6];
    int me = 0, them = 0;
    if (more.relation[0] == 0 || more.relation[1] == 0 || more.casus == 0
        || find_pair(base, at, number, false, &mine, &me, &theirs, &them) != Wrote::Done)
        return r;
    relation_cells(more, mine, me, theirs, them, cells);
    for (int i = 0; i < 3; i++)
        if (!peek(reinterpret_cast<const void *>(cells[i]), &r.mine[i], sizeof(float))
            || !peek(reinterpret_cast<const void *>(cells[3 + i]), &r.theirs[i], sizeof(float)))
            return r;
    r.ok = true;
    return r;
}

Wrote write_tech(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, float value)
{
    uint64_t object = 0;
    if (more.tech == 0)
        return Wrote::Off;
    if (!std::isfinite(value) || value < 0.0f)
        return Wrote::BadValue;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    return poke(object + more.tech, &value, sizeof(value)) ? Wrote::Done : Wrote::Failed;
}

Wrote write_people(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, float add, int *done)
{
    uint64_t object = 0, cells[2];
    float values[2];
    *done = 0;
    if (more.people[0] == 0 || more.people[1] == 0)
        return Wrote::Off;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    for (int i = 0; i < 2; i++) {
        cells[i] = object + more.people[i];
        if (!peek(reinterpret_cast<const void *>(cells[i]), &values[i], sizeof(float)) || !std::isfinite(values[i]) || values[i] < 0.0f
            || !std::isfinite(values[i] + add))
            return Wrote::BadValue;
        values[i] += add;
    }
    return poke_floats(cells, values, 2, done);
}

Wrote write_approval(const uint8_t *base, const GameAddresses &at, const MoreLayout &more)
{
    uint64_t object = 0;
    const float best = 1.0f;
    if (more.approval == 0)
        return Wrote::Off;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    return poke(object + more.approval, &best, sizeof(best)) ? Wrote::Done : Wrote::Failed;
}

Wrote write_opinion(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int *done)
{
    uint64_t object = 0;
    *done = 0;
    if (more.opinion[0] == 0 || more.opinion[1] == 0 || more.opinion[2] == 0)
        return Wrote::Off;
    const GameState s = read_player(base, at, &object);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    const uint64_t cells[3] = {object + more.opinion[0], object + more.opinion[1], object + more.opinion[2]};
    const float best[3] = {1.0f, 1.0f, 1.0f};
    return poke_floats(cells, best, 3, done);
}

Wrote write_relation(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int number, float level, int *done)
{
    uint64_t mine = 0, theirs = 0, cells[6];
    int me = 0, them = 0;
    *done = 0;
    if (more.relation[0] == 0 || more.relation[1] == 0 || more.casus == 0)
        return Wrote::Off;
    if (!std::isfinite(level) || level < -1.0f || level > 1.0f)
        return Wrote::BadValue;
    const Wrote found = find_pair(base, at, number, true, &mine, &me, &theirs, &them);
    if (found != Wrote::Done)
        return found;
    relation_cells(more, mine, me, theirs, them, cells);
    const float values[6] = {level, level, 0.0f, level, level, 0.0f};
    return poke_floats(cells, values, 6, done);
}

bool read_research(const uint8_t *base, const GameAddresses &at, const ResearchLayout &research, int picked, ResearchTables *out,
                   std::string *why)
{
    Shot shot;
    std::string reason;
    bool ok = shoot(base, at, research, picked, &shot, &reason);
    if (ok && shot.multiplayer) {
        ok = false;
        reason = "멀티플레이에서는 연구를 읽지 않습니다";
    }
    if (why != nullptr)
        *why = reason;
    if (ok)
        *out = std::move(shot.tables);
    return ok;
}

int hang_owners(uint64_t cell, uint64_t block, uint64_t *owners)
{
    if (!writable(cell, sizeof(uint64_t)))
        return -1;
    LONG64 was = 0;
    __try {
        was = InterlockedCompareExchange64(reinterpret_cast<LONG64 *>(cell), static_cast<LONG64>(block), 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    *owners = was == 0 ? block : static_cast<uint64_t>(was);
    return was == 0 ? 0 : 1;
}

// cell 의 qword 가 was 이면 now 로 바꾼다. 0 바꿨다 · 1 값이 was 가 아니었다(그대로 둔다) · -1 쓸 수 없다.
int swap_qword(uint64_t cell, uint64_t was, uint64_t now)
{
    LONG64 seen = 0;
    __try {
        seen = InterlockedCompareExchange64(reinterpret_cast<LONG64 *>(cell), static_cast<LONG64>(now), static_cast<LONG64>(was));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return static_cast<uint64_t>(seen) == was ? 0 : 1;
}

Wrote write_research(const uint8_t *base, const GameAddresses &at, const ResearchLayout &research, Recompute recompute, Research action,
                     const ResearchWhat &what, ResearchDone *done)
{
    *done = ResearchDone();
    Shot shot;
    if (!shoot(base, at, research, 0, &shot, &done->why))
        return done->why == NOT_PLAYING ? Wrote::NotInGame : Wrote::Unreadable;
    if (shot.multiplayer)
        return Wrote::NotInGame;

    // 새 묶음을 걸 수 있는가: 이미 있는 묶음 하나가 프로세스 힙의 OWNERS_BYTES 짜리 블록이어야 한다(게임이 그 힙에서 그 크기로 받는다는 뜻)
    uint64_t sample = 0;
    for (uint64_t owners : shot.tech_owners)
        sample = sample != 0 ? sample : owners;
    for (uint64_t owners : shot.design_owners)
        sample = sample != 0 ? sample : owners;
    bool can_house = sample != 0 && heap_block(sample, OWNERS_BYTES);

    const HANDLE heap = GetProcessHeap();
    std::vector<Item> items;
    std::vector<void *> fresh;                  // 새로 받은 묶음. 앞에서부터 차례로 건다
    size_t hung = 0;                            // 그 가운데 이미 건 것의 수(건 것은 게임의 것이다 — 풀지 않는다)
    const auto release = [&]() {
        for (size_t i = hung; i < fresh.size(); i++)
            HeapFree(heap, 0, fresh[i]);
        fresh.clear();
        hung = 0;
    };
    ResearchPlan plan;
    for (int pass = 0; pass < 2; pass++) {      // 묶음을 받지 못하면, 묶음이 없는 항목을 건너뛰는 계획으로 한 번 더
        plan = research_plan(shot.tables, action, what, can_house);
        items.clear();
        attach(plan.techs, shot.tables.techs, shot.tech_owners, shot.tech_cells, true, &items);
        attach(plan.designs, shot.tables.designs, shot.design_owners, shot.design_cells, false, &items);
        bool short_of = false;
        for (const Item &item : items)
            if (item.owners == 0 && !short_of) {
                void *const block = HeapAlloc(heap, HEAP_ZERO_MEMORY, OWNERS_BYTES);
                if (block == nullptr)
                    short_of = true;
                else
                    fresh.push_back(block);
            }
        if (!short_of)
            break;
        release();
        can_house = false;
    }
    done->plan = plan;
    done->skipped = plan.skipped;

    // 쓰기 전에: 모든 칸이 읽기 · 쓰기 쪽인가(반쪽만 쓰고 멈추지 않게)
    const uint64_t byte = static_cast<uint64_t>(shot.me) / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (shot.me % 8));
    done->cells = static_cast<int>(items.size() + fresh.size() + 2 * plan.nodes.size());
    for (const Item &item : items)
        if (item.owners != 0 ? !writable(item.owners + byte, 1) : !writable(item.cell, sizeof(uint64_t))) {
            release();
            return research_failed(done, item_name(item) + (item.owners != 0 ? " 의 보유 묶음" : " 의 묶음 칸"));
        }
    for (int n : plan.nodes)
        if (!writable(shot.nodes[static_cast<size_t>(n)] + NODE_FLAGS, 2 * sizeof(uint32_t))) {
            release();
            return research_failed(done, "연구 목록의 노드");
        }
    const uint64_t region_cell = shot.player + REGION_MASK;
    if (plan.region_add != 0) {
        done->cells++;
        if (!writable(region_cell, sizeof(uint64_t))) {
            release();
            return research_failed(done, "플레이어 지역의 지역 코드");
        }
    }

    // 다른 지역 전용을 연다(2026-10-17): 플레이어 지역의 지역 코드 칸에 비트를 더한다 — 맨 먼저. 표를 읽은 뒤에 값이 바뀌었으면 아무것도 쓰지 않는다
    if (plan.region_add != 0) {
        const int swapped = swap_qword(region_cell, shot.tables.region_mask, shot.tables.region_mask | plan.region_add);
        if (swapped != 0) {
            release();
            return swapped < 0 ? research_failed(done, "플레이어 지역의 지역 코드") : Wrote::NotInGame;
        }
        done->written++;
    }

    // 묶음(없는 항목만) → 비트
    for (Item &item : items) {
        if (item.owners == 0) {
            const int taken = hang_owners(item.cell, reinterpret_cast<uint64_t>(fresh[hung]), &item.owners);
            if (taken < 0) {
                release();
                return research_failed(done, item_name(item) + " 의 묶음 칸");
            }
            if (taken == 0) {                   // 걸었다 — 이제 게임의 것이다
                hung++;
                done->housed++;
                done->written++;
            } else {                            // 표를 읽은 뒤에 게임이 제 묶음을 걸었다 — 그것을 쓴다. 받아 둔 것은 끝에 돌려준다
                done->cells--;
            }
        }
        if (!poke_bits(item.owners + byte, mask, action != Research::Revoke, 1)) {
            release();
            return research_failed(done, item_name(item) + " 의 보유 묶음");
        }
        done->written++;
    }
    release();                                  // 걸지 않고 남은 묶음(게임이 먼저 건 항목의 몫)을 돌려준다

    // 대기열에서 뺀다: 노드의 깃발 둘에 "뺐다"를 켠다(다른 비트는 그대로)
    for (int n : plan.nodes)
        for (uint32_t side = 0; side < 2; side++) {
            if (!poke_bits(shot.nodes[static_cast<size_t>(n)] + NODE_FLAGS + 4 * side, NODE_GONE, true, sizeof(uint32_t)))
                return research_failed(done, "연구 목록의 노드");
            done->written++;
        }

    // 기술의 효과는 지역마다 미리 계산해 둔 표로 쓰인다 — 기술의 보유가 바뀌었으면 플레이어 지역의 것을 다시 셈하게 한다
    if (!plan.techs.empty() && recompute != nullptr) {
        if (!guarded_recompute(recompute, reinterpret_cast<void *>(shot.world), shot.me, &done->code))
            return Wrote::Crashed;
        done->recomputed = true;
    }
    return Wrote::Done;
}

void game_init()
{
    if (!reading_wanted()) {
        log_line("게임 상태를 읽지 않습니다 (SRTOYBOX_READ=0) — 단추가 꺼집니다");
        return;
    }
    const uint8_t *base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
    const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const IMAGE_NT_HEADERS64 *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    game_init_from(base, nt->OptionalHeader.SizeOfImage);
}

void game_init_from(const uint8_t *base, size_t size)
{
    // 새 찾기: 게임 상태를 읽는 전역 일곱 — 내장 치트와 무관한 서명으로
    GameAddresses at = {};
    SigRow rows[STATE_WANTED * STATE_SIGS];
    char why[160] = "";
    const ULONGLONG started = GetTickCount64();
    const bool state = locate_state(base, size, &at, rows, why, sizeof(why));
    int matched = 0;
    for (const SigRow &row : rows)
        matched += row.count == 1 ? 1 : 0;

    // 새 찾기: 값을 읽고 쓰는 자리 — 같은 규칙의 서명으로. 상태를 읽지 못하면 값도 쓰지 않으므로 찾지 않는다
    ValueLayout layout = {};
    SigRow value_rows[VALUE_WANTED * STATE_SIGS];
    char value_why[160] = "";
    const bool values = state && locate_values(base, size, &layout, value_rows, value_why, sizeof(value_why))
        && locate_fits(at, layout, value_why, sizeof(value_why));

    // 새 찾기: 더 쓰는 값(기술 수준 · 세계 시장 여론 · 관계) — 묶음마다 따로 찾는다. 값 묶음을 찾았으면 그 칸들과 겹치지 않아야 한다
    MoreLayout more = {};
    SigRow more_rows[MORE_WANTED * STATE_SIGS];
    char more_why[MORE_GROUPS][MORE_WHY] = {};
    const int groups = state ? locate_more(base, size, values ? &layout : nullptr, &more, more_rows, more_why) : 0;

    // 새 찾기: 연구 — 기술 · 부대 설계의 표, 지역별 연구 목록, 지역의 효과를 다시 셈하는 함수. 일곱을 모두, 서명도 셋씩 모두 맞아야 한다.
    // 값 묶음을 찾았으면 그 세계 자료 포인터와 겹치지 않아야 한다(겹치면 연구 묶음만 버린다)
    ResearchLayout research = {};
    char research_why[160] = "";
    ActLayout acts = {};
    char act_why[ACT_WANTED][MORE_WHY] = {};
    const int acting = state ? locate_acts(base, size, &acts, nullptr, act_why) : 0;
    const bool researching = state && locate_research(base, size, at, &research, nullptr, research_why, sizeof(research_why))
        && (!values || locate_research_fits(research, layout, research_why, sizeof(research_why)));
    // 새 찾기: 배율 — 곱 보정 표 · 날의 칸 · 예산 칸. 따로 찾고 따로 꺼진다
    MultLayout mult = {};
    char mult_why[MULT_WANTED][MORE_WHY] = {};
    const int multiplying = state ? locate_mult(base, size, &mult, nullptr, mult_why) : 0;
    const unsigned long long took = GetTickCount64() - started;

    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_mult = mult;
        memcpy(g_mult_why, mult_why, sizeof(g_mult_why));
        g_base = base;
        g_at = at;
        g_located = state;
        g_told = false;
        g_layout = layout;
        g_values = values;
        g_write_failed = false;
        snprintf(g_values_why, sizeof(g_values_why), "%s", value_why);
        g_more = more;
        g_more_groups = groups;
        memcpy(g_more_why, more_why, sizeof(g_more_why));
        g_research = research;
        g_research_found = researching;
        snprintf(g_research_why, sizeof(g_research_why), "%s", research_why);
        g_recompute = researching ? const_cast<uint8_t *>(base) + research.recompute : nullptr;
        g_colonize = (acting & ACT_COLONIZE) != 0 ? const_cast<uint8_t *>(base) + acts.colonize : nullptr;
        g_fight = (acting & ACT_FIGHT) != 0 ? const_cast<uint8_t *>(base) + acts.fight : nullptr;
        g_map_pick = (acting & ACT_MAP_PICK) != 0 ? acts.map_pick : 0;
        g_become = (acting & ACT_BECOME) != 0 ? const_cast<uint8_t *>(base) + acts.become : nullptr;
        g_index2 = (acting & ACT_INDEX2) != 0 ? acts.player_index2 : 0;
        g_pointer2 = (acting & ACT_POINTER2) != 0 ? acts.player_pointer2 : 0;
        g_elect = (acting & ACT_ELECT) != 0 ? const_cast<uint8_t *>(base) + acts.elect : nullptr;
        g_news = (acting & ACT_NEWS) != 0 ? const_cast<uint8_t *>(base) + acts.news : nullptr;
        g_leaders = (acting & ACT_LEADERS) != 0 ? acts.leader_table : 0;
        g_leader_id = (acting & ACT_LEADER_ID) != 0 ? acts.leader_id : 0;
        g_treaty = (acting & ACT_TREATY) != 0 ? const_cast<uint8_t *>(base) + acts.treaty : nullptr;
        g_treaties = (acting & ACT_TREATIES) != 0 ? acts.treaties : 0;
        g_annex = (acting & ACT_ANNEX) != 0 ? const_cast<uint8_t *>(base) + acts.annex : nullptr;
        g_map = (acting & ACT_MAP) != 0 ? acts.map : 0;
        g_spawn = (acting & ACT_SPAWN) != 0 ? const_cast<uint8_t *>(base) + acts.spawn : nullptr;
        g_map_hex = (acting & ACT_MAP_HEX) != 0 ? acts.map_hex : 0;
        g_units = (acting & ACT_UNITS) != 0 ? acts.units : 0;
        g_region_name = (acting & ACT_REGION_NAME) != 0 ? acts.region_name : 0;
        g_ui_flags = (acting & ACT_UI_FLAGS) != 0 ? acts.ui_flags : 0;
        g_gui = (acting & ACT_GUI_OBJECT) != 0 ? acts.gui : 0;
        g_gui_vtable = (acting & ACT_GUI_VTABLE) != 0 ? acts.gui_vtable : 0;
        g_game_over = (acting & ACT_GAME_OVER) != 0 ? acts.game_over : 0;
        g_winners = (acting & ACT_WINNERS) != 0 ? acts.winners : 0;
        g_deploy = (acting & ACT_DEPLOY) != 0 ? const_cast<uint8_t *>(base) + acts.deploy_hex : nullptr;
        g_picked_units = (acting & ACT_PICKED_UNITS) != 0 ? acts.picked_units : 0;
        g_upgrades = (acting & ACT_UPGRADES) != 0 ? acts.upgrades : 0;
        memcpy(g_act_why, act_why, sizeof(g_act_why));
    }
    if (state) {
        if ((acting & ACT_COLONIZE) != 0)
            log_line("게임의 함수를 부릅니다 (식민지화 +0x%X)", acts.colonize);
        else
            log_line("식민지화를 쓸 수 없습니다 (%s)", act_why[0]);
        if ((acting & ACT_FIGHT) != 0 && (acting & ACT_MAP_PICK) != 0)
            log_line("게임의 함수를 부릅니다 (전쟁 +0x%X, 지도에서 고른 지역 +0x%X)", acts.fight, acts.map_pick);
        else
            log_line("전쟁 붙이기를 쓸 수 없습니다 (%s)", (acting & ACT_FIGHT) == 0 ? act_why[1] : act_why[5]);
        const int become = ACT_BECOME | ACT_INDEX2 | ACT_POINTER2;
        if ((acting & become) == become)
            log_line("게임의 함수를 부릅니다 (플레이 지역 바꾸기 +0x%X, 플레이어의 둘째 사본 +0x%X +0x%X)", acts.become, acts.player_index2,
                     acts.player_pointer2);
        else
            log_line("이 나라로 플레이를 쓸 수 없습니다 (%s)",
                     (acting & ACT_BECOME) == 0 ? act_why[2] : (acting & ACT_INDEX2) == 0 ? act_why[6] : act_why[7]);
        const int novichok = ACT_ELECT | ACT_NEWS | ACT_LEADERS | ACT_LEADER_ID;
        if ((acting & novichok) == novichok)
            log_line("게임의 함수를 부릅니다 (새 지도자 세우기 +0x%X, 알림 +0x%X, 지도자 표 +0x%X, 지도자 번호 +0x%X)", acts.elect, acts.news,
                     acts.leader_table, acts.leader_id);
        else
            log_line("지도자 제거를 쓸 수 없습니다 (%s)", (acting & ACT_ELECT) == 0 ? act_why[3] : (acting & ACT_NEWS) == 0 ? act_why[4]
                     : (acting & ACT_LEADERS) == 0 ? act_why[8] : act_why[9]);
        if ((acting & ACT_TREATY) != 0 && (acting & ACT_TREATIES) != 0)
            log_line("게임의 함수를 부릅니다 (조약 +0x%X, 조약 표 +0x%X)", acts.treaty, acts.treaties);
        else
            log_line("동맹 맺기를 쓸 수 없습니다 (%s)", (acting & ACT_TREATY) == 0 ? act_why[10] : act_why[11]);
        if ((acting & ACT_ANNEX) != 0 && (acting & ACT_MAP) != 0)
            log_line("게임의 함수를 부릅니다 (칸의 주인 바꾸기 +0x%X, 지도 +0x%X)", acts.annex, acts.map);
        else
            log_line("병합을 쓸 수 없습니다 (%s)", (acting & ACT_ANNEX) == 0 ? act_why[12] : act_why[13]);
        if ((acting & ACT_SPAWN) != 0 && (acting & ACT_MAP_HEX) != 0)
            log_line("게임의 함수를 부릅니다 (부대 만들기 +0x%X, 지도에서 고른 칸 +0x%X)", acts.spawn, acts.map_hex);
        else
            log_line("부대 생성을 쓸 수 없습니다 (%s)", (acting & ACT_SPAWN) == 0 ? act_why[14] : act_why[15]);
        if ((acting & ACT_SUPPLY) == ACT_SUPPLY)
            log_line("고른 부대의 보급을 씁니다 (고른 부대의 목록 +0x%X, 개량 보정 표 +0x%X)", acts.picked_units, acts.upgrades);
        else
            log_line("보급 채우기를 쓸 수 없습니다 (%s)", (acting & ACT_PICKED_UNITS) == 0 ? act_why[16] : act_why[17]);
        if ((acting & ACT_VICTORY) == ACT_VICTORY)
            log_line("즉시 승리를 씁니다 (판이 끝난 꼴 +0x%X, 이긴 지역들의 묶음 +0x%X)", acts.game_over, acts.winners);
        else
            log_line("즉시 승리를 쓸 수 없습니다 (%s)", (acting & ACT_GAME_OVER) == 0 ? act_why[18] : act_why[19]);
        if ((acting & ACT_UNITS) != 0)
            log_line("내 부대 전부에 씁니다 (세계의 부대 목록 +0x%X)", acts.units);
        else
            log_line("내 부대 전부에 쓰기를 쓸 수 없습니다 (%s)", act_why[24]);
        if ((acting & ACT_GUI) == ACT_GUI)
            log_line("게임의 함수를 부릅니다 (GUI 객체의 함수 표 +0x%X 의 %d번, 화면의 깃발 +0x%X, GUI 객체 +0x%X)", acts.gui_vtable, GUI_SET,
                     acts.ui_flags, acts.gui);
        else
            log_line("GUI 숨기기를 쓸 수 없습니다 (%s)",
                     (acting & ACT_UI_FLAGS) == 0 ? act_why[20] : (acting & ACT_GUI_OBJECT) == 0 ? act_why[21] : act_why[22]);
        if ((acting & ACT_BEST) == ACT_BEST)
            log_line("게임의 함수를 부릅니다 (배치 칸 찾기 +0x%X — 최강 부대 묶음)", acts.deploy_hex);
        else
            log_line("최강 부대 묶음을 쓸 수 없습니다 (%s)", (acting & ACT_DEPLOY) == 0 ? act_why[23] : act_why[14]);
    }
    if (!state) {
        log_line("게임 상태를 읽을 수 없습니다 (%s) — 단추가 꺼집니다", why);
        return;
    }
    log_line("게임 상태를 읽습니다 (서명 %d개 가운데 %d개, %llu ms)", STATE_WANTED * STATE_SIGS, matched, took);
    log_unmatched(rows, STATE_WANTED * STATE_SIGS);
    if (!values) {
        log_line("값을 쓸 수 없습니다 (%s)", value_why);
    } else {
        if (write_wanted())
            log_line("값을 씁니다 (국고 +0x%X, 재고 +0x%X 간격 0x%X × %d)", layout.treasury, layout.stock_first, layout.stock_step,
                     STOCK_SLOTS);
        else
            log_line("값을 쓰지 않습니다 (SRTOYBOX_WRITE=0. 국고 +0x%X, 재고 +0x%X 간격 0x%X × %d)", layout.treasury,
                     layout.stock_first, layout.stock_step, STOCK_SLOTS);
        log_unmatched(value_rows, VALUE_WANTED * STATE_SIGS);
    }
    static const char *const MISSING[MORE_GROUPS] = {"기술 수준을 쓸 수 없습니다 (%s)", "세계 시장 여론을 쓸 수 없습니다 (%s)",
                                                     "관계를 쓸 수 없습니다 (%s)", "인구를 쓸 수 없습니다 (%s)",
                                                     "지지율을 쓸 수 없습니다 (%s)"};
    if (groups != 0) {
        char found[256] = "";
        size_t used = 0;
        const auto add = [&](const char *format, uint32_t a, uint32_t b, uint32_t c) {
            used += static_cast<size_t>(snprintf(found + used, sizeof(found) - used, format, used == 0 ? "" : " · ", a, b, c));
        };
        if (groups & MORE_TECH)
            add("%s기술 수준 +0x%X", more.tech, 0, 0);
        if (groups & MORE_OPINION)
            add("%s세계 시장 여론 +0x%X +0x%X +0x%X", more.opinion[0], more.opinion[1], more.opinion[2]);
        if (groups & MORE_RELATIONS)
            add("%s관계 +0x%X +0x%X 전쟁 명분 +0x%X", more.relation[0], more.relation[1], more.casus);
        if (groups & MORE_PEOPLE)
            add("%s인구 +0x%X 풀 +0x%X", more.people[0], more.people[1], 0);
        if (groups & MORE_APPROVAL)
            add("%s지지율 +0x%X", more.approval, 0, 0);
        log_line(write_wanted() ? "값을 더 씁니다 (%s)" : "값을 더 쓰지 않습니다 (SRTOYBOX_WRITE=0. %s)", found);
    }
    for (int g = 0; g < MORE_GROUPS; g++)
        if ((groups & (1 << g)) == 0)
            log_line(MISSING[g], more_why[g]);
    for (int i = 0; i < MORE_WANTED * STATE_SIGS; i++)   // 찾은 묶음에서만: 셋 가운데 둘로 찾았을 때 맞지 않은 하나
        if ((groups & (1 << locate_more_group(i / STATE_SIGS))) != 0)
            log_unmatched(more_rows[i], i % STATE_SIGS + 1);
    if (state) {
        if (multiplying == (1 << MULT_WANTED) - 1) {
            log_line("배율을 씁니다 (곱 보정 표 +0x%X · 날의 칸 +0x%X · 연간 세수 +0x%X · 군 인건비 +0x%X × +0x%X · 군 유지비 +0x%X × +0x%X · "
                     "부대 생산비 +0x%X · 연구의 하루치 +0x%X · 사회 지출 +0x%X × +0x%X · 품질 등급 +0x%X · 예비 인원 +0x%X · 군부 지지율 +0x%X · 위 나라 +0x%X)",
                     mult.mul_table, mult.day, mult.tax_total, mult.army_pay_level, mult.army_pay, mult.army_upkeep_level, mult.army_upkeep,
                     mult.unit_cost, mult.research_step, mult.social_level, mult.social_cost, mult.social_grade, mult.reserve, mult.mil_approval, mult.parent);
        } else {
            int missing = 0, first = -1;           // 한 줄로: 처음 못 찾은 것과 그 밖의 수
            for (int w = 0; w < MULT_WANTED; w++)
                if ((multiplying & (1 << w)) == 0 && missing++ == 0)
                    first = w;
            if (missing > 1)
                log_line("배율의 일부를 쓸 수 없습니다 (%s 외 %d개)", mult_why[first], missing - 1);
            else
                log_line("배율의 일부를 쓸 수 없습니다 (%s)", mult_why[first]);
        }
    }
    if (researching)
        log_line(write_wanted() ? "연구를 씁니다 (기술 표 +0x%X · 부대 설계 표 +0x%X · 연구 목록 +0x%X · 다시 셈 +0x%X)"
                                : "연구를 쓰지 않습니다 (SRTOYBOX_WRITE=0. 기술 표 +0x%X · 부대 설계 표 +0x%X · 연구 목록 +0x%X · 다시 셈 +0x%X)",
                 research.tech_table, research.design_table, research.lists, research.recompute);
    else
        log_line("연구를 쓸 수 없습니다 (%s)", research_why);
}

GameState game_state()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    if (!located(&base, &at))
        return GameState();
    const GameState s = read_game(base, at);
    if (!s.known) {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_told) {
            g_told = true;
            log_line("게임에서 읽은 값이 서로 맞지 않습니다 — 단추가 꺼집니다");
        }
    }
    return s;
}

std::vector<int> game_regions()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    return located(&base, &at) ? read_regions(base, at) : std::vector<int>();
}

bool game_reads()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_located && reading_wanted();
}

void game_set_for_test(const uint8_t *base, const GameAddresses *at)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_base = base;
    g_at = at != nullptr ? *at : GameAddresses();
    g_located = base != nullptr && at != nullptr;
    g_told = false;
    g_layout = ValueLayout();
    g_values = false;
    g_write_failed = false;
    snprintf(g_values_why, sizeof(g_values_why), "값의 자리를 주지 않았습니다");
    g_more = MoreLayout();
    g_more_groups = 0;
    for (int g = 0; g < MORE_GROUPS; g++)
        snprintf(g_more_why[g], MORE_WHY, "값의 자리를 주지 않았습니다");
    g_research = ResearchLayout();
    g_research_found = false;
    g_recompute = nullptr;
    g_mult = MultLayout();
    for (int w = 0; w < MULT_WANTED; w++)
        snprintf(g_mult_why[w], MORE_WHY, "배율의 자리를 주지 않았습니다");
    g_colonize = g_fight = g_become = g_elect = g_news = g_treaty = g_annex = g_spawn = g_deploy = nullptr;
    g_units = g_ui_flags = g_gui = g_gui_vtable = g_game_over = g_winners = g_picked_units = g_upgrades = g_map_hex = g_map_pick = g_index2 = g_pointer2 = g_leaders = g_leader_id = g_treaties = g_map = 0;
    for (int w = 0; w < ACT_WANTED; w++)
        snprintf(g_act_why[w], MORE_WHY, "%s", "함수의 자리를 주지 않았습니다");
    snprintf(g_research_why, sizeof(g_research_why), "연구의 자리를 주지 않았습니다");
}

void game_set_research_for_test(const ResearchLayout *layout, void *recompute)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_research = layout != nullptr ? *layout : ResearchLayout();
    g_research_found = layout != nullptr;
    g_recompute = layout != nullptr ? recompute : nullptr;
    g_write_failed = false;
}

void game_set_values_for_test(const ValueLayout *layout)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_layout = layout != nullptr ? *layout : ValueLayout();
    g_values = layout != nullptr;
    g_write_failed = false;
}

void game_set_more_for_test(const MoreLayout *layout)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_more = layout != nullptr ? *layout : MoreLayout();
    g_more_groups = (g_more.tech != 0 ? MORE_TECH : 0)
        | (g_more.opinion[0] != 0 && g_more.opinion[1] != 0 && g_more.opinion[2] != 0 ? MORE_OPINION : 0)
        | (g_more.relation[0] != 0 && g_more.relation[1] != 0 && g_more.casus != 0 ? MORE_RELATIONS : 0)
        | (g_more.people[0] != 0 && g_more.people[1] != 0 ? MORE_PEOPLE : 0)
        | (g_more.approval != 0 ? MORE_APPROVAL : 0);
    g_write_failed = false;
}

std::string game_values_off()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return off_text(g_values, g_values_why);
}

bool game_writes()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_located && reading_wanted() && write_wanted() && !g_write_failed;
}

std::string game_more_off(int group)
{
    const int index = more_index(group);
    std::lock_guard<std::mutex> lock(g_lock);
    return index < 0 ? std::string("없는 묶음입니다") : off_text((g_more_groups & group) != 0, g_more_why[index]);
}

GameMore game_more()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !reading_wanted())
            return GameMore();
        base = g_base;
        at = g_at;
        more = g_more;
    }
    return read_more(base, at, more);
}

Relation game_relation(int number)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !reading_wanted())
            return Relation();
        base = g_base;
        at = g_at;
        more = g_more;
    }
    return read_relation(base, at, more, number);
}

Wrote game_write_tech(float value)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    if (!more_ready(MORE_TECH, &base, &at, &more))
        return Wrote::Off;
    const Wrote wrote = write_tech(base, at, more, value);
    return wrote == Wrote::Failed ? write_failed("기술 수준", 1, 0) : wrote;
}

Wrote game_write_opinion()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    int done = 0;
    if (!more_ready(MORE_OPINION, &base, &at, &more))
        return Wrote::Off;
    const Wrote wrote = write_opinion(base, at, more, &done);
    return wrote == Wrote::Failed ? write_failed("세계 시장 여론", 3, done) : wrote;
}

Wrote game_write_people(float add)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    int done = 0;
    if (!more_ready(MORE_PEOPLE, &base, &at, &more))
        return Wrote::Off;
    const Wrote wrote = write_people(base, at, more, add, &done);
    return wrote == Wrote::Failed ? write_failed("인구", 2, done) : wrote;
}

Wrote game_write_approval()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    if (!more_ready(MORE_APPROVAL, &base, &at, &more))
        return Wrote::Off;
    const Wrote wrote = write_approval(base, at, more);
    return wrote == Wrote::Failed ? write_failed("지지율", 1, 0) : wrote;
}

Wrote game_write_relation(int number, float level)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MoreLayout more = {};
    int done = 0;
    if (!more_ready(MORE_RELATIONS, &base, &at, &more))
        return Wrote::Off;
    const Wrote wrote = write_relation(base, at, more, number, level, &done);
    return wrote == Wrote::Failed
        ? write_failed("관계 — " + region_label(number) + " (" + std::to_string(number) + ")", 6, done) : wrote;
}

GameValues game_values()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ValueLayout layout = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !g_values || !reading_wanted())
            return GameValues();
        base = g_base;
        at = g_at;
        layout = g_layout;
    }
    return read_values(base, at, layout);
}

Wrote game_write_treasury(double value)
{
    return write(-1, value);
}

Wrote game_write_stock(int slot, float value)
{
    return slot < 0 ? Wrote::BadValue : write(slot, value);
}

std::string game_research_off()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return off_text(g_research_found, g_research_why);
}

bool game_research(int picked, ResearchTables *out, std::string *why)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout research = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !g_research_found || !reading_wanted()) {
            if (why != nullptr)
                *why = !g_located || !reading_wanted() ? "게임 상태를 읽을 수 없습니다" : g_research_why;
            return false;
        }
        base = g_base;
        at = g_at;
        research = g_research;
    }
    return read_research(base, at, research, picked, out, why);
}

std::string game_act_off(int act)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (act == ACT_COLONIZE)
        return off_text(g_colonize != nullptr, g_act_why[0]);
    if (act == ACT_FIGHT)                      // 함수와 "지도에서 고른 지역"을 둘 다 찾아야 한다
        return off_text(g_fight != nullptr && g_map_pick != 0, g_fight == nullptr ? g_act_why[1] : g_act_why[5]);
    if (act == ACT_GUI)                        // GUI 숨기기/보이기: 화면의 깃발 · GUI 객체 · 그 함수 표
        return off_text(g_ui_flags != 0 && g_gui != 0 && g_gui_vtable != 0,
                        g_ui_flags == 0 ? g_act_why[20] : g_gui == 0 ? g_act_why[21] : g_act_why[22]);
    if (act == ACT_VICTORY) {                  // 즉시 승리: 전역 둘, 그리고 새 묶음을 걸 때 견줄 기술 표(연구 묶음)
        if (g_game_over != 0 && g_winners != 0 && !g_research_found)
            return g_research_why;
        return off_text(g_game_over != 0 && g_winners != 0, g_game_over == 0 ? g_act_why[18] : g_act_why[19]);
    }
    if (act == ACT_BEST) {                     // 최강 부대 묶음: 함수 둘과 부대 설계 표(연구 묶음)
        if (g_deploy != nullptr && g_spawn != nullptr && !g_research_found)
            return g_research_why;
        return off_text(g_deploy != nullptr && g_spawn != nullptr, g_deploy == nullptr ? g_act_why[23] : g_act_why[14]);
    }
    if (act == ACT_UNITS || act == ACT_PICKED_UNITS || act == ACT_SUPPLY_ALL) {   // 부대의 상태 · 내 부대 전부의 보급
        const bool all = act != ACT_PICKED_UNITS, supply = act == ACT_SUPPLY_ALL;
        if (all && g_units == 0)
            return off_text(false, g_act_why[24]);
        if (!all && g_picked_units == 0)
            return off_text(false, g_act_why[16]);
        if (supply && g_upgrades == 0)
            return off_text(false, g_act_why[17]);
        return g_research_found ? std::string() : std::string(g_research_why);   // 부대 설계 표(부대 객체인지 가린다)
    }
    if (act == ACT_SUPPLY) {                   // 보급 채우기: 고른 부대의 목록 · 개량 보정 표 · 기술 표와 부대 설계 표(연구 묶음)
        if (g_picked_units != 0 && g_upgrades != 0 && !g_research_found)
            return g_research_why;
        return off_text(g_picked_units != 0 && g_upgrades != 0, g_picked_units == 0 ? g_act_why[16] : g_act_why[17]);
    }
    if (act == ACT_SPAWN) {                    // 부대 생성: 함수 · 지도에서 고른 칸 · 지도 객체(칸이 지도 안인가) · 부대 설계 표(연구 묶음)
        if (g_spawn != nullptr && g_map_hex != 0 && g_map != 0 && !g_research_found)
            return g_research_why;
        return off_text(g_spawn != nullptr && g_map_hex != 0 && g_map != 0,
                        g_spawn == nullptr ? g_act_why[14] : g_map_hex == 0 ? g_act_why[15] : g_act_why[13]);
    }
    if (act == ACT_ANNEX)                      // 병합: 함수와 지도 객체
        return off_text(g_annex != nullptr && g_map != 0, g_annex == nullptr ? g_act_why[12] : g_act_why[13]);
    if (act == ACT_TREATY)                     // 동맹 맺기: 함수와 조약 표의 자리(이미 맺었는지 읽는다)
        return off_text(g_treaty != nullptr && g_treaties != 0, g_treaty == nullptr ? g_act_why[10] : g_act_why[11]);
    if (act == ACT_ELECT)                      // 지도자 제거: 함수 둘 · 지도자 표 · 지도자 번호의 자리를 모두 찾아야 한다
        return off_text(g_elect != nullptr && g_news != nullptr && g_leaders != 0 && g_leader_id != 0,
                        g_elect == nullptr ? g_act_why[3] : g_news == nullptr ? g_act_why[4] : g_leaders == 0 ? g_act_why[8] : g_act_why[9]);
    if (act == ACT_BECOME)                     // 함수와 둘째 사본 둘을 모두 찾아야 한다 — 전역을 반쪽만 쓰지 않는다
        return off_text(g_become != nullptr && g_index2 != 0 && g_pointer2 != 0,
                        g_become == nullptr ? g_act_why[2] : g_index2 == 0 ? g_act_why[6] : g_act_why[7]);
    return "없는 기능입니다";
}

namespace {

// "지도에서 고른 칸"을 읽는다: dword — 낮은 word 가 가로, 높은 word 가 세로. 0 이면 고르지 않은 것이다(게임도 그렇게 본다).
// 지도 안의 칸일 때만 참.
bool picked_hex(const uint8_t *base, uint32_t where, uint32_t map_where, uint32_t *hex, int *x, int *y)
{
    uint64_t map = 0;
    uint8_t head[MAP_COUNT + 4] = {};
    if (!peek_at(base, where, hex) || *hex == 0 || !peek_at(base, map_where, &map) || map == 0
        || !peek(reinterpret_cast<const void *>(map), head, sizeof(head)))
        return false;
    const int32_t width = field<int32_t>(head, MAP_WIDTH), count = field<int32_t>(head, MAP_COUNT);
    *x = static_cast<int>(*hex & 0xFFFF);
    *y = static_cast<int>(*hex >> 16);
    return width >= 1 && width <= 0xFFFF && count >= width && count % width == 0 && *x < width && *y < count / width;
}

}  // namespace

namespace {

// 보급의 "가득"을 셈할 때 읽는 표들(모두 읽기만 한다)
struct SupplyTables {
    uint64_t designs, techs, upgrades;   // 부대 설계 표 · 기술 표의 주소, 개량 보정 표에서 첫 레코드의 깃발의 주소
    int32_t design_count, tech_count;
};

// 부대 객체와 그 설계 레코드를 읽는다. 설계 포인터가 부대 설계 표 안의 레코드를 가리킬 때만 참 — 부대 객체가 아닌 것을 걸러 낸다.
bool read_unit(const SupplyTables &t, uint64_t address, uint8_t *unit, uint8_t *design)
{
    if (address == 0 || !peek(reinterpret_cast<const void *>(address), unit, UNIT_READ))
        return false;
    const uint64_t record = field<uint64_t>(unit, UNIT_DESIGN);
    if (record < t.designs + DESIGN_SIZE || record >= t.designs + static_cast<uint64_t>(t.design_count) * DESIGN_SIZE
        || (record - t.designs) % DESIGN_SIZE != 0)
        return false;
    return peek(reinterpret_cast<const void *>(record), design, DESIGN_SIZE);
}

// 개량의 보정(값 · 비트의 번호 n)을 x 에 곱한다 — 깃발의 그 비트가 켜진 레코드는 값을 그대로 쓴다. 내장 치트 stranded 의 셈 그대로.
bool upgrade_factor(const SupplyTables &t, const uint8_t *unit, const uint8_t *design, int n, float *x)
{
    if ((unit[UNIT_FLAGS] & 0x40) == 0)
        return true;
    for (int k = 0; k < 8; k++) {
        const int step = unit[UNIT_SLOTS + k];
        if (step == 0)
            continue;
        const uint64_t flags_at = t.upgrades
            + static_cast<uint64_t>(design[DESIGN_CLASS] * 25 + 3 * (k + 1) + (step - 1 > 2 ? step - 1 : 2)) * UPGRADE_SIZE;
        uint32_t flags = 0;
        float value = 0.0f;
        if (!peek(reinterpret_cast<const void *>(flags_at), &flags, sizeof(flags))
            || !peek(reinterpret_cast<const void *>(flags_at - UPGRADE_VALUES + 4ull * static_cast<uint64_t>(n)), &value, sizeof(value)))
            return false;
        *x = (flags & (1u << n)) != 0 ? value : *x * (1.0f + value);
    }
    return true;
}

// 설계의 기술 여덟 가운데 부대의 주인이 보유한 것의 그 효과를 곱한 값(없으면 1).
bool tech_factor(const SupplyTables &t, const uint8_t *unit, const uint8_t *design, uint16_t effect, float *out)
{
    *out = 1.0f;
    const uint16_t owner = field<uint16_t>(unit, UNIT_OWNER);
    if (unit[UNIT_TECHS_ON] == 0)
        return true;
    for (int i = 0; i < 8; i++) {
        const uint16_t id = field<uint16_t>(design, DESIGN_TECHS + 2 * static_cast<uint32_t>(i));
        uint8_t tech[TECH_SIZE];
        uint64_t word = 0;
        if (id >= t.tech_count)
            continue;
        if (!peek(reinterpret_cast<const void *>(t.techs + static_cast<uint64_t>(id) * TECH_SIZE), tech, sizeof(tech)))
            return false;
        const uint64_t owners = field<uint64_t>(tech, TECH_OWNERS);
        if (owners == 0)
            continue;
        if (!peek(reinterpret_cast<const void *>(owners + 8ull * (owner >> 6)), &word, sizeof(word)))
            return false;
        if ((word >> (owner & 63) & 1) == 0)
            continue;
        for (uint32_t j = 0; j < 4; j++)
            if (field<uint16_t>(tech, TECH_EFFECTS + 2 * j) == effect)
                *out *= 1.0f + field<float>(tech, TECH_EFFECT_VALUES + 4 * j);
    }
    return true;
}

// 보급 두 칸의 "가득"(내장 치트 stranded 가 채울 때 쓰는 값). 읽지 못하거나 말이 안 되는 값이면 거짓.
bool supply_full(const SupplyTables &t, const uint8_t *unit, const uint8_t *design, float *a, float *b)
{
    const float size = field<float>(unit, UNIT_SIZE);
    float xa = size * field<float>(design, DESIGN_CAP_A), xb = size * field<float>(design, DESIGN_CAP_B), ma = 1.0f, mb = 1.0f;
    if (!upgrade_factor(t, unit, design, UPGRADE_CAP_A, &xa) || !tech_factor(t, unit, design, EFFECT_CAP_A, &ma)
        || !upgrade_factor(t, unit, design, UPGRADE_CAP_B, &xb) || !tech_factor(t, unit, design, EFFECT_CAP_B, &mb))
        return false;
    *a = ma * xa;
    *b = mb * xb;
    // 다른 부대를 실은 부대: 실은 것들이 실을 수 있는 몫을 넘으면 넘는 만큼을 둘째 칸에서 뺀다
    uint64_t cargo = field<uint64_t>(unit, UNIT_CARGO);
    if (field<uint64_t>(unit, UNIT_CARRIER) == 0 && cargo != 0 && field<float>(unit, UNIT_HELD) == 0.0f) {
        float room = size * field<float>(design, DESIGN_CARGO_CAP), held = 0.0f;
        if (!upgrade_factor(t, unit, design, UPGRADE_CARGO, &room))
            return false;
        for (int n = 0; cargo != 0; n++) {
            uint8_t other[UNIT_READ], other_design[DESIGN_SIZE];
            if (n >= 256 || !read_unit(t, cargo, other, other_design))
                return false;
            const float other_size = field<float>(other, UNIT_SIZE);
            if (other_size > 0.0f)
                held += other_size * field<float>(other_design, DESIGN_CARGO_USE)
                    + ((field<uint32_t>(other, UNIT_FLAGS4) & 0x800) != 0 ? 0.0f : field<float>(other, UNIT_STORE_B));
            cargo = field<uint64_t>(other, UNIT_CARGO);
        }
        if (room - held < 0.0f)
            *b += room - held;
        if (!(*b >= 0.0f))
            *b = 0.0f;
    }
    return std::isfinite(*a) && std::isfinite(*b) && *a >= 0.0f && *b >= 0.0f && *a < 1e9f && *b < 1e9f;
}

// 게임에서 고른 부대들의 주소를 모은다. 목록이 말이 안 되면(읽을 수 없다 · 너무 길다) 거짓.
bool picked_units(const uint8_t *base, uint32_t where, std::vector<uint64_t> *units)
{
    uint64_t node = 0;
    if (!peek_at(base, where, &node))
        return false;
    for (int n = 0; node != 0; n++) {
        uint8_t raw[PICKED_UNIT + 8];
        if (n >= PICKED_LIMIT || !peek(reinterpret_cast<const void *>(node), raw, sizeof(raw)))
            return false;
        units->push_back(field<uint64_t>(raw, PICKED_UNIT));
        node = field<uint64_t>(raw, PICKED_NEXT);
    }
    return true;
}

}  // namespace

namespace {

// 세계의 부대 목록에서 그 지역(인덱스)의 부대(시설은 뺀다)의 주소를 모은다. 목록이 말이 안 되면(읽을 수 없다 · 너무 길다) 거짓.
// 설계가 0번(designs — 부대 설계 표의 첫 자리)인 것은 부대가 아니다: 게임은 지역마다 그런 객체를 하나씩 목록에 둔다(게임에서 봤다) — 뺀다.
// 그래서 "내 부대 전부" = 지휘 패널의 배치 + 예비(와 첩보원 같은 몇)다.
bool owned_units(const uint8_t *base, uint32_t where, int owner, uint64_t designs, std::vector<uint64_t> *units)
{
    uint64_t unit = 0;
    if (!peek_at(base, where, &unit))
        return false;
    for (int n = 0; unit != 0; n++) {
        uint8_t raw[UNIT_READ];
        if (n >= UNITS_LIMIT || !peek(reinterpret_cast<const void *>(unit), raw, sizeof(raw)))
            return false;
        // 깃발 UNIT_UNBUILT 인 것도 뺀다: 그 칸들의 값이 배치 · 예비 부대와 다른 뜻일 수 있다(게임에서 1.00 / 0.20 같은 값이었다) — 건드리지 않는다
        if (field<uint16_t>(raw, UNIT_OWNER) == owner && (field<uint32_t>(raw, UNIT_FLAGS4) & (UNIT_FACILITY | UNIT_UNBUILT)) == 0
            && field<uint64_t>(raw, UNIT_DESIGN) != designs)
            units->push_back(unit);
        unit = field<uint64_t>(raw, UNIT_NEXT);
    }
    return true;
}

}  // namespace

Wrote game_unit_stat(int which, float value, bool all, int *done, int *skipped)
{
    *done = *skipped = 0;
    static const uint32_t CELLS[STAT_COUNT] = {UNIT_EXPERIENCE, UNIT_MORALE, UNIT_EFFICIENCY};
    if (!game_act_off(all ? ACT_UNITS : ACT_PICKED_UNITS).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    uint32_t where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
        where = all ? g_units : g_picked_units;
    }
    uint64_t player = 0;
    SupplyTables t = {};
    std::vector<uint64_t> units, targets;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (which < 0 || which >= STAT_COUNT || !(value >= 0.0f && value <= 1.0f) || !peek_at(base, r.design_table, &t.designs) || t.designs == 0
        || !peek_at(base, r.design_count, &t.design_count) || t.design_count < 2
        || !(all ? owned_units(base, where, s.index, t.designs, &units) : picked_units(base, where, &units)))
        return Wrote::BadValue;
    for (uint64_t unit : units) {              // 모두 읽은 뒤에 쓴다 — 하나라도 부대 객체로 보이지 않으면 하나도 쓰지 않는다
        uint8_t raw[UNIT_READ], design[DESIGN_SIZE];
        if (!read_unit(t, unit, raw, design))
            return Wrote::BadValue;
        if (field<uint16_t>(raw, UNIT_OWNER) != s.index || (field<uint32_t>(raw, UNIT_FLAGS4) & UNIT_FACILITY) != 0) {
            ++*skipped;                        // 남의 부대 · 시설에는 쓰지 않는다
            continue;
        }
        targets.push_back(unit);
    }
    for (uint64_t unit : targets)
        if (!writable(unit + CELLS[which], sizeof(float)))
            return Wrote::Failed;
    for (uint64_t unit : targets) {
        if (!poke(unit + CELLS[which], &value, sizeof(value)))
            return Wrote::Failed;
        ++*done;
    }
    return Wrote::Done;
}

bool game_picked_units(int *all, int *mine)
{
    *all = *mine = 0;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    uint32_t where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !reading_wanted() || g_picked_units == 0)
            return false;
        base = g_base;
        at = g_at;
        where = g_picked_units;
    }
    uint64_t player = 0;
    std::vector<uint64_t> units;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || !picked_units(base, where, &units))
        return false;
    for (uint64_t unit : units) {
        uint16_t owner = 0;
        if (unit == 0 || !peek(reinterpret_cast<const void *>(unit + UNIT_OWNER), &owner, sizeof(owner)))
            return false;
        *mine += owner == s.index ? 1 : 0;
    }
    *all = static_cast<int>(units.size());
    return true;
}

Wrote game_supply(bool fill, bool all, int *done, int *skipped)
{
    *done = *skipped = 0;
    if (!game_act_off(all ? ACT_SUPPLY_ALL : ACT_SUPPLY).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    uint32_t where = 0, upgrades = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
        where = all ? g_units : g_picked_units;
        upgrades = g_upgrades;
    }
    uint64_t player = 0;
    SupplyTables t = {};
    std::vector<uint64_t> units;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (!peek_at(base, r.design_table, &t.designs) || t.designs == 0 || !peek_at(base, r.design_count, &t.design_count) || t.design_count < 2
        || !peek_at(base, r.tech_table, &t.techs) || t.techs == 0 || !peek_at(base, r.tech_count, &t.tech_count) || t.tech_count < 1
        || !(all ? owned_units(base, where, s.index, t.designs, &units) : picked_units(base, where, &units)))
        return Wrote::BadValue;
    t.upgrades = reinterpret_cast<uint64_t>(base) + r.world + upgrades;
    struct Target {
        uint64_t unit;
        float values[2];
    };
    std::vector<Target> targets;
    for (uint64_t unit : units) {              // 모두 읽고 셈한 뒤에 쓴다 — 하나라도 말이 안 되면 하나도 쓰지 않는다
        uint8_t raw[UNIT_READ], design[DESIGN_SIZE];
        Target target = {unit, {0.0f, 0.0f}};
        if (!read_unit(t, unit, raw, design))
            return Wrote::BadValue;
        if (field<uint16_t>(raw, UNIT_OWNER) != s.index || (field<uint32_t>(raw, UNIT_FLAGS4) & UNIT_FACILITY) != 0) {
            ++*skipped;                        // 남의 부대 · 시설에는 쓰지 않는다
            continue;
        }
        if (fill && !supply_full(t, raw, design, &target.values[0], &target.values[1]))
            return Wrote::BadValue;
        targets.push_back(target);
    }
    for (const Target &target : targets)
        if (!writable(target.unit + UNIT_STORE_A, 2 * sizeof(float)))
            return Wrote::Failed;
    for (const Target &target : targets) {
        if (!poke(target.unit + UNIT_STORE_A, target.values, sizeof(target.values)))
            return Wrote::Failed;
        ++*done;
    }
    return Wrote::Done;
}

Wrote game_gui_toggle(unsigned long *code, bool *hidden)
{
    *code = 0;
    *hidden = false;
    if (!game_act_off(ACT_GUI).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    uint32_t flags_at = 0, gui_at = 0, table_at = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        flags_at = g_ui_flags;
        gui_at = g_gui;
        table_at = g_gui_vtable;
    }
    uint64_t player = 0, object = 0, table = 0, set = 0;
    uint32_t flags = 0, object_flags = 0;
    if (!read_player(base, at, &player).in_game)
        return Wrote::NotInGame;
    const uint64_t flags_cell = reinterpret_cast<uint64_t>(base) + flags_at, want = reinterpret_cast<uint64_t>(base) + table_at;
    if (!peek_at(base, flags_at, &flags) || !peek_at(base, gui_at, &object))
        return Wrote::BadValue;
    // GUI 객체가 있으면: 그 객체의 함수 표가 찾아 둔 표일 때만 부른다 — 아니면 깃발도 쓰지 않는다
    if (object != 0 && (!peek(reinterpret_cast<const void *>(object), &table, sizeof(table)) || table != want
                        || !peek(reinterpret_cast<const void *>(table + 8ull * GUI_SET), &set, sizeof(set)) || set == 0
                        || !peek(reinterpret_cast<const void *>(object + GUI_OBJECT_FLAGS), &object_flags, sizeof(object_flags))))
        return Wrote::BadValue;
    const uint32_t next = flags ^ GUI_HIDDEN;
    if (!writable(flags_cell, sizeof(next)) || !poke(flags_cell, &next, sizeof(next)))
        return Wrote::Failed;
    *hidden = (next & GUI_HIDDEN) != 0;
    if (object == 0)
        return Wrote::Done;                    // 치트도 객체가 없으면 깃발만 쓴다
    const uint32_t shown = *hidden ? object_flags | GUI_OBJECT_HIDDEN : object_flags & ~GUI_OBJECT_HIDDEN;
    return guarded_gui(reinterpret_cast<GuiSet>(set), reinterpret_cast<void *>(object), shown, code) ? Wrote::Done : Wrote::Crashed;
}

Wrote game_victory(void *window)
{
    if (!game_act_off(ACT_VICTORY).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    uint32_t over_at = 0, winners_at = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
        over_at = g_game_over;
        winners_at = g_winners;
    }
    uint64_t player = 0, bundle = 0, techs = 0;
    uint32_t over = 0;
    int32_t tech_count = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || window == nullptr)
        return Wrote::NotInGame;
    const uint64_t over_cell = reinterpret_cast<uint64_t>(base) + over_at, winners_cell = reinterpret_cast<uint64_t>(base) + winners_at;
    if (s.index < 1 || s.index >= static_cast<int>(OWNERS_BYTES) * 8 || !peek_at(base, over_at, &over) || !peek_at(base, winners_at, &bundle))
        return Wrote::BadValue;
    if (over != 0)
        return Wrote::NotUsed;                 // 이미 끝난 판이다
    if (!writable(over_cell, sizeof(over)) || !writable(winners_cell, sizeof(bundle)))
        return Wrote::Failed;
    const uint64_t byte = static_cast<uint64_t>(s.index) / 8;
    const uint8_t bit = static_cast<uint8_t>(1u << (s.index % 8));
    if (bundle == 0) {
        // 묶음이 아직 없다: 게임처럼 프로세스 힙에서 받아 건다 — 게임의 묶음(기술의 보유 묶음 하나)이 그 힙의 그 크기 블록일 때만(연구와 같은 검사)
        uint64_t sample = 0;
        if (!peek_at(base, r.tech_table, &techs) || techs == 0 || !peek_at(base, r.tech_count, &tech_count))
            return Wrote::BadValue;
        for (int id = 1; id < tech_count && id < 4096 && sample == 0; id++)
            if (!peek(reinterpret_cast<const void *>(techs + static_cast<uint64_t>(id) * TECH_SIZE + TECH_OWNERS), &sample, sizeof(sample)))
                return Wrote::BadValue;
        if (sample == 0 || !heap_block(sample, OWNERS_BYTES))
            return Wrote::Failed;
        const HANDLE heap = GetProcessHeap();
        uint8_t *const block = static_cast<uint8_t *>(HeapAlloc(heap, HEAP_ZERO_MEMORY, OWNERS_BYTES));
        if (block == nullptr)
            return Wrote::Failed;
        block[byte] = bit;
        const int hung = hang_owners(winners_cell, reinterpret_cast<uint64_t>(block), &bundle);
        if (hung != 0)
            HeapFree(heap, 0, block);          // 걸지 못했거나 그 사이에 게임이 걸었다
        if (hung < 0)
            return Wrote::Failed;
    }
    uint8_t now = 0;
    if (!peek(reinterpret_cast<const void *>(bundle + byte), &now, 1) || !writable(bundle + byte, 1))
        return Wrote::Failed;
    now = static_cast<uint8_t>(now | bit);
    const uint32_t won = 1;
    if (!poke(bundle + byte, &now, 1) || !poke(over_cell, &won, sizeof(won)))
        return Wrote::Failed;
    return PostMessageA(static_cast<HWND>(window), VICTORY_MESSAGE, VICTORY_WPARAM, 0) != 0 ? Wrote::Done : Wrote::Failed;
}

Wrote game_best(int domain, int each, unsigned long *code, int *made)
{
    *code = 0;
    *made = 0;
    if (!game_act_off(ACT_BEST).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    Spawn spawn = nullptr;
    Deploy deploy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
        spawn = reinterpret_cast<Spawn>(g_spawn);
        deploy = reinterpret_cast<Deploy>(g_deploy);
    }
    uint64_t player = 0, table = 0;
    int32_t count = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || player == 0)
        return Wrote::NotInGame;
    if (each < 1 || each > BEST_EACH_MAX || !peek_at(base, r.design_table, &table) || table == 0 || !peek_at(base, r.design_count, &count)
        || count < 2)
        return Wrote::BadValue;
    const int last = count - 1 < SPAWN_MAX_EQUIP ? count - 1 : SPAWN_MAX_EQUIP;
    std::vector<uint8_t> raw(static_cast<size_t>(last + 1) * DESIGN_SIZE);
    if (!peek(reinterpret_cast<const void *>(table), raw.data(), raw.size()))
        return Wrote::BadValue;
    // 병과마다 따로, 그 능력치가 가장 높은 설계(같으면 번호가 낮은 것). 빈 자리는 보지 않는다.
    // 내장 치트는 병과가 바뀔 때 앞 병과에서 고른 설계를 지우지 않아 몇 병과에서 앞 병과의 설계를 또 준다 — 그것은 옮기지 않았다
    int best[BEST_CLASSES] = {};
    for (int id = 1; id <= last; id++) {
        const uint8_t *record = raw.data() + static_cast<size_t>(id) * DESIGN_SIZE;
        const int cls = record[DESIGN_CLASS];
        if (field<uint64_t>(record, 0) == 0 || cls >= BEST_CLASSES || BEST_STAT[cls] < 0 || (domain >= 0 && design_domain(cls) != domain))
            continue;
        const uint32_t stat = DESIGN_STATS + 4 * static_cast<uint32_t>(BEST_STAT[cls]);
        if (best[cls] == 0 || field<float>(record, stat) > field<float>(raw.data() + static_cast<size_t>(best[cls]) * DESIGN_SIZE, stat))
            best[cls] = id;
    }
    std::vector<int> designs, moves;
    for (int id : best)
        if (id != 0) {
            designs.push_back(id);
            moves.push_back(raw[static_cast<size_t>(id) * DESIGN_SIZE + DESIGN_MOVE]);
        }
    if (designs.empty())
        return Wrote::Done;                    // 그 영역의 설계가 없다(*made 는 0)
    return guarded_best(deploy, spawn, reinterpret_cast<void *>(player), designs.data(), moves.data(), static_cast<int>(designs.size()), each,
                        made, code) ? Wrote::Done : Wrote::Crashed;
}

bool game_map_hex(int *x, int *y)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    uint32_t where = 0, map_where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !reading_wanted() || g_map_hex == 0 || g_map == 0)
            return false;
        base = g_base;
        at = g_at;
        where = g_map_hex;
        map_where = g_map;
    }
    uint64_t player = 0;
    uint32_t hex = 0;
    return read_player(base, at, &player).in_game && picked_hex(base, where, map_where, &hex, x, y);
}

Wrote game_spawn(const int *designs, int n, int each, const float *stats, unsigned long *code, int *made, int *stated, int *x, int *y)
{
    *code = 0;
    *made = *stated = *x = *y = 0;
    if (!game_act_off(ACT_SPAWN).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    Spawn spawn = nullptr;
    uint32_t where = 0, map_where = 0, units_where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
        spawn = reinterpret_cast<Spawn>(g_spawn);
        where = g_map_hex;
        map_where = g_map;
        units_where = g_units;
    }
    uint64_t player = 0, table = 0;
    int32_t count = 0;
    uint32_t hex = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || player == 0)
        return Wrote::NotInGame;
    if (!picked_hex(base, where, map_where, &hex, x, y))
        return Wrote::NoTarget;
    if (n < 1 || each < 1 || n > SPAWN_LIMIT || each > SPAWN_LIMIT || n * each > SPAWN_LIMIT || !peek_at(base, r.design_table, &table)
        || table == 0 || !peek_at(base, r.design_count, &count))
        return Wrote::BadValue;
    for (int i = 0; i < n; i++) {              // 하나라도 없는 설계면 하나도 만들지 않는다
        uint64_t first = 0;
        if (designs[i] < 1 || designs[i] > SPAWN_MAX_EQUIP || designs[i] >= count
            || !peek(reinterpret_cast<const void *>(table + static_cast<uint64_t>(designs[i]) * DESIGN_SIZE), &first, sizeof(first)) || first == 0)
            return Wrote::BadValue;
    }
    bool want = false;                         // 만든 부대의 상태를 정한다
    for (int c = 0; stats != nullptr && c < STAT_COUNT; c++)
        want = want || stats[c] >= 0.0f;
    std::vector<uint64_t> before, after;
    const bool listed = want && units_where != 0 && owned_units(base, units_where, s.index, table, &before);
    if (!guarded_spawn(spawn, reinterpret_cast<void *>(player), designs, n, each, hex, made, code))
        return Wrote::Crashed;
    if (!want)
        return Wrote::Done;
    *stated = -1;
    if (!listed || !owned_units(base, units_where, s.index, table, &after))
        return Wrote::Done;                    // 부대는 만들었다 — 상태만 못 쓴다
    std::sort(before.begin(), before.end());
    static const uint32_t CELLS[STAT_COUNT] = {UNIT_EXPERIENCE, UNIT_MORALE, UNIT_EFFICIENCY};
    SupplyTables t = {};
    t.designs = table;
    t.design_count = count;
    int done = 0;
    for (uint64_t unit : after) {
        if (std::binary_search(before.begin(), before.end(), unit))
            continue;                          // 앞서부터 있던 부대
        uint8_t raw[UNIT_READ], design[DESIGN_SIZE];
        if (!read_unit(t, unit, raw, design) || field<uint16_t>(raw, UNIT_OWNER) != s.index
            || (field<uint32_t>(raw, UNIT_FLAGS4) & UNIT_FACILITY) != 0)
            continue;
        bool ok = true;
        for (int c = 0; c < STAT_COUNT && ok; c++) {
            const float value = stats[c] > 1.0f ? 1.0f : stats[c];
            if (stats[c] >= 0.0f)
                ok = poke(unit + CELLS[c], &value, sizeof(value));
        }
        if (!ok)
            return write_failed("만든 부대의 상태", *made, done);
        done++;
    }
    *stated = done > 0 ? done : -1;
    return Wrote::Done;
}

Wrote game_annex(int number, unsigned long *code, int *hexes)
{
    *code = 0;
    *hexes = 0;
    if (!game_act_off(ACT_ANNEX).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Annex annex = nullptr;
    uint32_t where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        annex = reinterpret_cast<Annex>(g_annex);
        where = g_map;
    }
    uint64_t mine = 0, theirs = 0, map = 0;
    int me = 0, them = 0;
    const Wrote found = find_pair(base, at, number, true, &mine, &me, &theirs, &them);
    if (found != Wrote::Done)
        return found;
    uint8_t head[MAP_COUNT + 4] = {};
    if (!peek_at(base, where, &map) || map == 0 || !peek(reinterpret_cast<const void *>(map), head, sizeof(head)))
        return Wrote::BadValue;
    const uint64_t cells = field<uint64_t>(head, MAP_HEXES), side_a = field<uint64_t>(head, MAP_SIDE_A),
                   side_b = field<uint64_t>(head, MAP_SIDE_B);
    const int32_t width = field<int32_t>(head, MAP_WIDTH), count = field<int32_t>(head, MAP_COUNT);
    if (cells == 0 || side_a == 0 || side_b == 0 || width < 1 || width > 0xFFFF || count < width || count % width != 0
        || count / width > 0xFFFF)
        return Wrote::BadValue;
    std::vector<uint8_t> raw(static_cast<size_t>(count) * HEX_SIZE);
    if (!peek(reinterpret_cast<const void *>(cells), raw.data(), raw.size()))
        return Wrote::BadValue;
    std::vector<int> list;
    for (int i = 0; i < count; i++) {
        const uint16_t owner = field<uint16_t>(raw.data() + static_cast<size_t>(i) * HEX_SIZE, 0);
        if (owner >= MAX_REGIONS)
            return Wrote::BadValue;            // 칸 레코드의 꼴이 다르다 — 한 칸도 부르지 않는다
        if (owner == them)
            list.push_back(i);
    }
    if (list.empty())
        return Wrote::Done;                    // 그 나라의 땅이 없다(*hexes 는 0)
    return guarded_annex(annex, map, cells, side_a, side_b, width, list.data(), static_cast<int>(list.size()), me, hexes, code)
        ? Wrote::Done : Wrote::Crashed;
}

Wrote game_treaty(int number, unsigned long *code)
{
    *code = 0;
    if (!game_act_off(ACT_TREATY).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Treaty treaty = nullptr;
    uint32_t treaties = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        treaty = reinterpret_cast<Treaty>(g_treaty);
        treaties = g_treaties;
    }
    uint64_t mine = 0, theirs = 0, bits = 0, word = 0;
    int me = 0, them = 0;
    const Wrote found = find_pair(base, at, number, true, &mine, &me, &theirs, &them);
    if (found != Wrote::Done)
        return found;
    // 플레이어의 조약 표에서 이 조약을 맺은 지역들의 비트 묶음(없으면 아직 아무와도 맺지 않았다 — 치트도 그렇게 본다)
    if (!peek_in(mine, treaties + 8ull * TREATY_ALLIANCE, &bits))
        return Wrote::BadValue;
    if (bits != 0) {
        if (!peek(reinterpret_cast<const void *>(bits + 8ull * static_cast<uint64_t>(them / 64)), &word, sizeof(word)))
            return Wrote::BadValue;
        if ((word >> (them % 64) & 1) != 0)
            return Wrote::NotUsed;             // 이미 동맹이다 — 한 번 더 부르면 깨진다(치트의 뒤집기). 부르지 않는다
    }
    return guarded_alliance(treaty, reinterpret_cast<void *>(mine), me, reinterpret_cast<void *>(theirs), them, code) ? Wrote::Done
                                                                                                                     : Wrote::Crashed;
}

Wrote game_novichok(int number, unsigned long *code)
{
    *code = 0;
    if (!game_act_off(ACT_ELECT).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Elect elect = nullptr;
    News news = nullptr;
    uint32_t leaders = 0, leader_id = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        elect = reinterpret_cast<Elect>(g_elect);
        news = reinterpret_cast<News>(g_news);
        leaders = g_leaders;
        leader_id = g_leader_id;
    }
    uint64_t player = 0, target = 0, table = 0;
    int them = 0;
    uint16_t before = 0;
    uint32_t flags = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (number == s.player || !find_region(base, at, number, &target, &them))
        return Wrote::NoTarget;
    if (!peek_at(base, leaders, &table) || table == 0 || !peek_in(target, leader_id, &before))
        return Wrote::BadValue;
    const uint64_t cell = table + static_cast<uint64_t>(before) * LEADER_SIZE + LEADER_FLAGS;
    if (!peek(reinterpret_cast<const void *>(cell), &flags, sizeof(flags)))
        return Wrote::BadValue;
    flags |= LEADER_GONE;
    if (!writable(cell, sizeof(flags)) || !poke(cell, &flags, sizeof(flags)))
        return write_failed("지도자", 1, 0);
    return guarded_succession(elect, news, reinterpret_cast<void *>(target), reinterpret_cast<const uint16_t *>(target + leader_id), before,
                              them, code) ? Wrote::Done : Wrote::Crashed;
}

Wrote game_become(int number, unsigned long *code)
{
    *code = 0;
    if (!game_act_off(ACT_BECOME).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Become become = nullptr;
    uint32_t index2 = 0, pointer2 = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        become = reinterpret_cast<Become>(g_become);
        index2 = g_index2;
        pointer2 = g_pointer2;
    }
    uint64_t player = 0, target = 0;
    int them = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (number == s.player || !find_region(base, at, number, &target, &them))
        return Wrote::NoTarget;
    // 치트가 쓰는 순서 그대로: 인덱스, 그 둘째 사본, 포인터, 그 둘째 사본
    const uint64_t origin = reinterpret_cast<uint64_t>(base);
    const uint64_t cells[4] = {origin + at.player_index, origin + index2, origin + at.player_pointer, origin + pointer2};
    const size_t sizes[4] = {4, 4, 8, 8};
    const int32_t new_index = them;
    uint64_t old[4] = {}, fresh[4] = {};
    memcpy(&fresh[0], &new_index, 4);
    memcpy(&fresh[1], &new_index, 4);
    fresh[2] = fresh[3] = target;
    for (int i = 0; i < 4; i++)
        if (!peek(reinterpret_cast<const void *>(cells[i]), &old[i], sizes[i]))
            return Wrote::BadValue;
    // 인덱스 둘은 늘 같고, 첫째 포인터는 지금의 플레이어 객체다. 둘째 포인터는 같지 않아도 된다 — 게임이 첫 자정부터 그것을
    // 플레이어 지역의 "전날 사본"으로 돌린다(게임에서 봤다 — 바꾸지 않은 판에서도). 치트는 그래도 새 나라의 객체를 쓰고, 다음 자정에 게임이 다시 돌린다.
    if (old[0] != old[1] || old[2] != player || old[3] == 0)
        return Wrote::BadValue;
    for (int i = 0; i < 4; i++)
        if (!writable(cells[i], sizes[i]))
            return write_failed("플레이하는 나라", 4, 0);
    for (int i = 0; i < 4; i++)
        if (!poke(cells[i], &fresh[i], sizes[i])) {
            for (int back = i - 1; back >= 0; back--)   // 반쪽만 바뀐 채로 두지 않는다
                poke(cells[back], &old[back], sizes[back]);
            return write_failed("플레이하는 나라", 4, i);
        }
    return guarded_become(become, reinterpret_cast<void *>(target), code) ? Wrote::Done : Wrote::Crashed;
}

int game_map_pick()
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    uint32_t where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !reading_wanted() || g_map_pick == 0)
            return 0;
        base = g_base;
        at = g_at;
        where = g_map_pick;
    }
    uint64_t player = 0, pointer = 0, slot = 0;
    uint16_t index = 0;
    Region region = {};
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || !peek_at(base, where, &pointer) || pointer == 0 || !peek(reinterpret_cast<const void *>(pointer), &index, sizeof(index))
        || index < 1 || index > s.regions || !peek_at(base, at.region_table + 8ull * index, &slot) || !peek_region(slot, &region)
        || !usable(region, index) || !in_play(region))
        return 0;
    return region.number;
}

Wrote game_fight(int attacker, int target, unsigned long *code)
{
    *code = 0;
    if (!game_act_off(ACT_FIGHT).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Fight fight = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        fight = reinterpret_cast<Fight>(g_fight);
    }
    uint64_t player = 0, first = 0, second = 0;
    int a = 0, b = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer)
        return Wrote::NotInGame;
    if (attacker == target || !find_region(base, at, attacker, &first, &a) || !find_region(base, at, target, &second, &b))
        return Wrote::NoTarget;
    return guarded_fight(fight, reinterpret_cast<void *>(first), b, code) ? Wrote::Done : Wrote::Crashed;
}

Wrote game_colonize(int number, unsigned long *code)
{
    *code = 0;
    if (!game_act_off(ACT_COLONIZE).empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    Colonize colonize = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        colonize = reinterpret_cast<Colonize>(g_colonize);
    }
    uint64_t mine = 0, theirs = 0;
    int me = 0, them = 0;
    const Wrote found = find_pair(base, at, number, true, &mine, &me, &theirs, &them);
    if (found != Wrote::Done)
        return found;
    return guarded_colonize(colonize, reinterpret_cast<void *>(mine), them, code) ? Wrote::Done : Wrote::Crashed;
}

void game_set_acts_for_test(const ActLayout *layout, void *colonize, void *fight, void *become, void *elect, void *news, void *treaty,
                            void *annex, void *spawn, void *deploy)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_deploy = layout != nullptr ? deploy : nullptr;
    g_units = layout != nullptr ? layout->units : 0;
    g_region_name = layout != nullptr ? layout->region_name : 0;
    g_ui_flags = layout != nullptr ? layout->ui_flags : 0;
    g_gui = layout != nullptr ? layout->gui : 0;
    g_gui_vtable = layout != nullptr ? layout->gui_vtable : 0;
    g_game_over = layout != nullptr ? layout->game_over : 0;
    g_winners = layout != nullptr ? layout->winners : 0;
    g_spawn = layout != nullptr ? spawn : nullptr;
    g_map_hex = layout != nullptr ? layout->map_hex : 0;
    g_picked_units = layout != nullptr ? layout->picked_units : 0;
    g_upgrades = layout != nullptr ? layout->upgrades : 0;
    g_colonize = layout != nullptr ? colonize : nullptr;
    g_fight = layout != nullptr ? fight : nullptr;
    g_map_pick = layout != nullptr ? layout->map_pick : 0;
    snprintf(g_act_why[0], MORE_WHY, "%s", g_colonize != nullptr ? "" : "함수의 자리를 주지 않았습니다");
    snprintf(g_act_why[1], MORE_WHY, "%s", g_fight != nullptr ? "" : "함수의 자리를 주지 않았습니다");
    g_become = layout != nullptr ? become : nullptr;
    g_index2 = layout != nullptr ? layout->player_index2 : 0;
    g_pointer2 = layout != nullptr ? layout->player_pointer2 : 0;
    snprintf(g_act_why[2], MORE_WHY, "%s", g_become != nullptr ? "" : "함수의 자리를 주지 않았습니다");
    g_elect = layout != nullptr ? elect : nullptr;
    g_news = layout != nullptr ? news : nullptr;
    g_leaders = layout != nullptr ? layout->leader_table : 0;
    g_leader_id = layout != nullptr ? layout->leader_id : 0;
    g_treaty = layout != nullptr ? treaty : nullptr;
    g_treaties = layout != nullptr ? layout->treaties : 0;
    g_annex = layout != nullptr ? annex : nullptr;
    g_map = layout != nullptr ? layout->map : 0;
    const bool given[ACT_WANTED] = {g_colonize != nullptr, g_fight != nullptr, g_become != nullptr, g_elect != nullptr, g_news != nullptr,
                                    g_map_pick != 0, g_index2 != 0, g_pointer2 != 0, g_leaders != 0, g_leader_id != 0,
                                    g_treaty != nullptr, g_treaties != 0, g_annex != nullptr, g_map != 0,
                                    g_spawn != nullptr, g_map_hex != 0, g_picked_units != 0, g_upgrades != 0,
                                    g_game_over != 0, g_winners != 0, g_ui_flags != 0, g_gui != 0, g_gui_vtable != 0,
                                    g_deploy != nullptr, g_units != 0, g_region_name != 0};
    for (int w = 0; w < ACT_WANTED; w++)
        snprintf(g_act_why[w], MORE_WHY, "%s", given[w] ? "" : "함수의 자리를 주지 않았습니다");
}

std::string game_owner_names(bool design, int id)
{
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located || !g_research_found || !reading_wanted())
            return std::string();
        base = g_base;
        at = g_at;
        r = g_research;
    }
    uint64_t player = 0, table = 0, owners = 0;
    int32_t count = 0;
    uint8_t bits[OWNERS_BYTES];
    const GameState s = read_player(base, at, &player);
    const uint32_t size = design ? DESIGN_SIZE : TECH_SIZE, cell = design ? DESIGN_OWNERS : TECH_OWNERS;
    if (!s.in_game || !peek_at(base, design ? r.design_table : r.tech_table, &table) || table == 0
        || !peek_at(base, design ? r.design_count : r.tech_count, &count) || id < 1 || id >= count
        || !peek(reinterpret_cast<const void *>(table + static_cast<uint64_t>(id) * size + cell), &owners, sizeof(owners)) || owners == 0
        || !peek(reinterpret_cast<const void *>(owners), bits, sizeof(bits)))
        return std::string();
    std::string out;
    for (int i = 1; i <= s.regions && i < OWNERS_BYTES * 8; i++) {
        if (i == s.index || (bits[i / 8] >> (i % 8) & 1) == 0)
            continue;
        uint64_t slot = 0;
        Region region = {};
        const bool known = peek_at(base, at.region_table + 8ull * static_cast<uint64_t>(i), &slot) && peek_region(slot, &region) && usable(region, i);
        out += (out.empty() ? "" : ", ") + (known ? region_label(region.number) : "#" + std::to_string(i));
    }
    return out;
}

Wrote game_write_research(Research action, const ResearchWhat &what, ResearchDone *done)
{
    *done = ResearchDone();
    if (!game_research_off().empty())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout research = {};
    Recompute recompute = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        research = g_research;
        recompute = reinterpret_cast<Recompute>(g_recompute);
    }
    const Wrote wrote = write_research(base, at, research, recompute, action, what, done);
    if (wrote == Wrote::Failed) {
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_write_failed = true;
        }
        if (done->written > 0)
            log_line("연구 쓰기 실패 (%s, %d칸 가운데 %d칸을 쓴 뒤) — 값 쓰기를 끕니다", done->why.c_str(), done->cells, done->written);
        else
            log_line("연구 쓰기 실패 (%s) — 값 쓰기를 끕니다", done->why.c_str());
    }
    return wrote;
}

// ---- 배율(묶음 7) ----

void game_set_mult_for_test(const MultLayout *layout)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_mult = layout != nullptr ? *layout : MultLayout();
    g_write_failed = false;
}

std::string game_mult_off(int part)
{
    std::string values, research;
    if (part == MULT_PART_MONEY || part == MULT_PART_SOCIAL)
        values = game_values_off();
    if (part == MULT_PART_SPEED)
        research = game_research_off();
    std::lock_guard<std::mutex> lock(g_lock);
    const uint32_t *const fields = &g_mult.mul_table;
    const auto missing = [&](int first, int last) -> const char * {
        for (int w = first; w <= last; w++)
            if (fields[w] == 0)
                return g_mult_why[w][0] != '\0' ? g_mult_why[w] : "배율의 자리를 찾지 못했습니다";
        return nullptr;
    };
    const char *why = nullptr;
    if (part == MULT_PART_CELLS)
        why = missing(0, 0);
    else if (part == MULT_PART_MONEY)
        why = missing(1, 7);                    // 날의 칸 · 연간 세수 · 국방의 다섯
    else if (part == MULT_PART_SPEED)
        why = missing(1, 1) != nullptr ? missing(1, 1) : missing(8, 8);
    else if (part == MULT_PART_SOCIAL)
        why = missing(1, 1) != nullptr ? missing(1, 1) : missing(9, 10);
    else if (part == MULT_PART_GRADE)
        why = missing(1, 1) != nullptr ? missing(1, 1) : missing(11, 11);
    else if (part == MULT_PART_RESERVE)
        why = missing(12, 12);
    else if (part == MULT_PART_MILAPPROVAL)
        why = missing(13, 13);
    else if (part == MULT_PART_PARENT)
        why = missing(14, 14);
    else
        return "없는 묶음입니다";
    if (why != nullptr)
        return why;
    return part == MULT_PART_MONEY || part == MULT_PART_SOCIAL ? values : part == MULT_PART_SPEED ? research : std::string();
}

bool game_mult_row(float *cells, int *index)
{
    if (!game_mult_off(MULT_PART_CELLS).empty())
        return false;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS)
        return false;
    *index = s.index;
    return peek(base + mult.mul_table + static_cast<uint64_t>(MUL_STEP) * s.index, cells, sizeof(float) * MUL_CELLS);
}

Wrote game_write_mult(int cell, float value, float max)
{
    if (!game_mult_off(MULT_PART_CELLS).empty() || !game_writes())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS)
        return Wrote::NotInGame;
    if (cell < 1 || cell >= MUL_CELLS || !std::isfinite(value) || !(value > 0.0f && value < max && max <= 100000.0f))
        return Wrote::BadValue;
    const uint64_t where = reinterpret_cast<uint64_t>(base) + mult.mul_table + static_cast<uint64_t>(MUL_STEP) * s.index + 4ull * cell;
    if (!poke(where, &value, sizeof(value)))
        return write_failed("곱 보정 칸", 1, 0);
    return Wrote::Done;
}

Daily game_daily()
{
    Daily d;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located)
            return d;
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (mult.day == 0 || !s.in_game || s.multiplayer || !peek_at(base, mult.day, &d.day))
        return d;
    d.ok = true;
    d.index = s.index;
    double tax = 0;
    if (mult.tax_total != 0 && peek_in(player, mult.tax_total, &tax) && std::isfinite(tax) && tax > 0 && tax < 1e18)
        d.tax = tax / 365.0;
    float level[2] = {}, cost[2] = {}, units = 0;
    if (mult.army_pay_level != 0 && mult.army_upkeep_level != 0 && mult.army_pay != 0 && mult.army_upkeep != 0 && mult.unit_cost != 0
        && peek_in(player, mult.army_pay_level, &level[0]) && peek_in(player, mult.army_upkeep_level, &level[1])
        && peek_in(player, mult.army_pay, &cost[0]) && peek_in(player, mult.army_upkeep, &cost[1]) && peek_in(player, mult.unit_cost, &units)) {
        const double yearly = static_cast<double>(level[0]) * cost[0] + static_cast<double>(level[1]) * cost[1];
        const double daily = yearly / 365.0 + units;
        if (std::isfinite(daily) && daily > 0 && daily < 1e16 && level[0] >= 0 && level[1] >= 0 && cost[0] >= 0 && cost[1] >= 0 && units >= 0)
            d.defense = daily;
    }
    float costs[SOCIAL_COUNT] = {}, levels[SOCIAL_COUNT] = {};
    if (mult.social_cost != 0 && mult.social_level != 0 && peek_in(player, mult.social_cost, &costs) && peek_in(player, mult.social_level, &levels)) {
        double yearly = 0;
        bool sane = true;
        for (int i = 0; i < SOCIAL_COUNT; i++) {
            sane = sane && std::isfinite(costs[i]) && std::isfinite(levels[i]) && costs[i] >= 0 && levels[i] >= 0 && levels[i] <= 100.0f;
            yearly += static_cast<double>(costs[i]) * levels[i];
        }
        if (sane && std::isfinite(yearly) && yearly > 0 && yearly < 1e18)
            d.social = yearly / 365.0;
    }
    float step = 0;
    if (mult.research_step != 0 && peek_in(player, mult.research_step, &step) && std::isfinite(step) && step > 0 && step <= DAILY_STEP_MAX)
        d.step = step;
    return d;
}

Grades game_social_grades()
{
    Grades out;
    if (!game_mult_off(MULT_PART_GRADE).empty())
        return out;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS || !peek_at(base, mult.day, &out.day)
        || !peek_in(player, mult.social_grade, &out.grade))
        return out;
    out.ok = true;
    out.index = s.index;
    return out;
}

Wrote game_write_social_grade(int item, float value)
{
    if (!game_mult_off(MULT_PART_GRADE).empty() || !game_writes())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS)
        return Wrote::NotInGame;
    const auto sane = [](float v) { return std::isfinite(v) && v >= 0.0f && v < SOCIAL_GRADE_SANE; };
    float now = 0;
    const uint64_t where = player + mult.social_grade + 4ull * static_cast<unsigned>(item);
    if (item < 0 || item >= SOCIAL_COUNT || !sane(value) || !peek(reinterpret_cast<const uint8_t *>(where), &now, sizeof(now)) || !sane(now))
        return Wrote::BadValue;
    if (!poke(where, &value, sizeof(value)))
        return write_failed("품질 등급 칸", 1, 0);
    return Wrote::Done;
}

namespace {

// 예비 칸의 자리(플레이어 지역 객체 + 칸). 진행 중이 아니거나 못 찾았으면 0.
uint64_t reserve_cell()
{
    if (!game_mult_off(MULT_PART_RESERVE).empty())
        return 0;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    MultLayout mult = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        mult = g_mult;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS || player == 0)
        return 0;
    return player + mult.reserve;
}

bool reserve_sane(float v)
{
    return std::isfinite(v) && v >= 0.0f && v <= RESERVE_MAX;
}

}  // namespace

bool game_reserve(float *out)
{
    const uint64_t where = reserve_cell();
    return where != 0 && peek(reinterpret_cast<const uint8_t *>(where), out, sizeof(*out));
}

Wrote game_write_reserve(float value)
{
    if (!game_mult_off(MULT_PART_RESERVE).empty() || !game_writes())
        return Wrote::Off;
    const uint64_t where = reserve_cell();
    if (where == 0)
        return Wrote::NotInGame;
    float now = 0;
    if (!reserve_sane(value) || !peek(reinterpret_cast<const uint8_t *>(where), &now, sizeof(now)) || !reserve_sane(now))
        return Wrote::BadValue;
    if (!poke(where, &value, sizeof(value)))
        return write_failed("예비 인원 칸", 1, 0);
    return Wrote::Done;
}

Wrote game_research_push(float days, int *nodes)
{
    *nodes = 0;
    if (!game_mult_off(MULT_PART_SPEED).empty() || !game_writes())
        return Wrote::Off;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    ResearchLayout r = {};
    {
        std::lock_guard<std::mutex> lock(g_lock);
        base = g_base;
        at = g_at;
        r = g_research;
    }
    uint64_t player = 0;
    const GameState s = read_player(base, at, &player);
    if (!s.in_game || s.multiplayer || s.index <= 0 || s.index >= REGION_SLOTS)
        return Wrote::NotInGame;
    if (!std::isfinite(days) || days == 0.0f)
        return Wrote::BadValue;
    uint64_t node = 0;
    if (!peek_at(base, static_cast<uint64_t>(r.world) + r.lists + static_cast<uint64_t>(LIST_STEP) * s.index, &node))
        return Wrote::Unreadable;
    std::vector<std::pair<uint64_t, float>> todo;      // 모두 읽은 뒤에 쓴다
    for (int seen = 0; node != 0; seen++) {
        uint32_t flags[2] = {};
        float left = 0;
        uint64_t next = 0;
        if (seen >= 65536 || !peek_in(node, NODE_FLAGS, &flags) || !peek_in(node, NODE_LEFT, &left) || !peek_in(node, NODE_NEXT, &next))
            return Wrote::Unreadable;
        if ((flags[0] & NODE_GONE) == 0 && (flags[1] & NODE_GONE) == 0 && (flags[1] & NODE_ACTIVE) != 0 && std::isfinite(left) && left > 0.0f
            && left <= NODE_LEFT_MAX) {
            float to = left - days;
            to = to < NODE_LEFT_MIN ? NODE_LEFT_MIN : to > NODE_LEFT_MAX ? NODE_LEFT_MAX : to;
            if (to != left)
                todo.push_back({node + NODE_LEFT, to});
        }
        node = next;
    }
    for (const auto &cell : todo)
        if (!writable(cell.first, sizeof(float)))
            return write_failed("연구 노드의 남은 기간", static_cast<int>(todo.size()), 0);
    for (const auto &cell : todo) {
        if (!poke(cell.first, &cell.second, sizeof(float)))
            return write_failed("연구 노드의 남은 기간", static_cast<int>(todo.size()), *nodes);
        ++*nodes;
    }
    return Wrote::Done;
}

// ---- 지역의 이름(게임이 지금 쓰는 글) ----

std::vector<std::pair<int, std::string>> game_region_names()
{
    std::vector<std::pair<int, std::string>> out;
    const uint8_t *base = nullptr;
    GameAddresses at = {};
    uint32_t where = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_located)
            return out;
        base = g_base;
        at = g_at;
        where = g_region_name;
    }
    int32_t count = 0;
    if (where == 0 || !read_game(base, at).in_game || !peek_at(base, at.region_count, &count) || count < 1 || count >= MAX_REGIONS)
        return out;
    std::vector<uint64_t> table(static_cast<size_t>(count) + 1);
    if (!peek(base + at.region_table, table.data(), table.size() * sizeof(uint64_t)))
        return out;
    for (int i = 1; i <= count; i++) {
        Region r = {};
        uint64_t text = 0;
        char name[NAME_BYTES] = {};
        if (!peek_region(table[static_cast<size_t>(i)], &r) || !usable(r, i) || !in_play(r) || !peek_in(table[static_cast<size_t>(i)], where, &text)
            || text == 0)
            continue;
        size_t size = sizeof(name) - 1;                 // 글이 쪽의 끝에 걸쳐 있으면 짧게 다시 읽는다
        while (size >= 8 && !peek(reinterpret_cast<const void *>(text), name, size))
            size /= 2;
        if (size < 8)
            continue;
        const std::string decoded = game_text_to_utf8(name, size);
        if (!decoded.empty())
            out.push_back({r.number, decoded});
    }
    return out;
}
