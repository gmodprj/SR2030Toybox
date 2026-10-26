// 찾은 주소(locate.h)로 게임의 상태와 값을 읽고 고친다. 내장 치트를 거치지 않는다.
// 게임의 메모리에 쓰는 곳은 이것뿐이다(끝의 "이 나라로 플레이"만 전역에 쓴다 — 사용자가 정했다, 2026-10-11):
//   플레이어 지역 객체의 국고 1칸 · 재고 STOCK_SLOTS 칸 · 기술 수준 1칸 · 세계 시장 여론 3칸 · 인구 2칸(인구 칸과 그 풀) · 국내 지지율 1칸(3단계 4) ·
//   관계 표 셋에서 고른 나라의 칸,
//   그리고 고른 나라의 지역 객체의 관계 표 셋에서 플레이어의 칸.
//   연구(3단계 3): 기술 · 부대 설계의 보유 비트 묶음에서 플레이어의 비트, 묶음이 없는 항목의 포인터 칸(새 묶음을 걸 때),
//   플레이어의 연구 목록 노드의 깃발 두 칸.
//   "연구 가능으로"가 다른 지역 전용을 열 때(2026-10-17): 플레이어 지역 객체의 지역 코드 칸(+0x40, qword — CVP 의 worldavail)에 비트를 OR 한다(끄지 않는다).
// 그 밖의 지역 · 다른 칸 · 전역 · 치트 허용 비트에는 쓰지 않는다. 기술 표와 부대 설계 표는 모든 나라가 함께 쓰는 표다 — 거기에 쓰는 것은
// 플레이어의 비트와, 플레이어의 비트 하나만 켜질 빈 묶음을 거는 것뿐이다(다른 나라의 비트 · 연구 기간 · 비용은 건드리지 않는다).
//   "이 나라로 플레이"(3단계 4): 전역 넷 — 플레이어의 인덱스와 그 둘째 사본, 플레이어의 지역 객체 포인터와 그 둘째 사본.
//   "지도자 제거"(3단계 4): 지도자 표에서 고른 나라의 지도자 레코드의 깃발 칸에 "죽었다" 비트 하나(사용자가 정했다, 2026-10-11).
// 게임의 함수는 열하나만 부른다(모두 치트가 아니다 — 게임이 스스로도 부르는 함수):
//   "지역의 효과를 다시 셈" — 플레이어 지역에 대해서만(연구가 끝날 때 게임이 부르는 것).
//   "식민지화"(3단계 4) — (플레이어의 지역 객체, 고른 나라의 인덱스, 1) 로 한 번. 사용자가 정했다(2026-10-10).
//   "전쟁"(3단계 4) — (지도에서 고른 나라의 지역 객체, 1, 목록에서 고른 나라의 인덱스, 0, 0) 로 한 번. 사용자가 정했다(2026-10-10).
//   플레이어가 두 나라 가운데 하나여도 막지 않는다(내장 치트와 같다 — 사용자가 정했다).
//   "플레이 지역 바꾸기"(3단계 4) — 전역 넷을 쓴 뒤 (새 플레이어의 지역 객체, 0) 으로 한 번. 사용자가 정했다(2026-10-11).
//   "새 지도자 세우기" · "알림"(3단계 4, 지도자 제거) — 위의 비트를 켠 뒤 (그 나라의 지역 객체, 0, 0) 과,
//   (그 지역 객체, 알림 0x65C × 4, 1, NULL, 앞 지도자, 지역 인덱스, 새 지도자, 0, 0, 0, 0) 으로 한 번씩. 게임이 지도자가 죽은 날 스스로 하는 것과 같다.
//   "조약"(3단계 4, 동맹 맺기) — 플레이어 쪽에서 둘 · 고른 나라 쪽에서 둘, 모두 넷(내장 치트 treaty 와 같은 인자). 사용자가 정했다(2026-10-11).
//   이미 동맹이면 부르지 않는다(치트는 그때 깬다 — 옮기지 않았다).
//   "칸의 주인 바꾸기"(3단계 4, 병합) — 지도의 칸 가운데 주인이 고른 나라인 칸마다 (그 칸의 묶음, 플레이어의 인덱스, 0, 0, 0, 0) 으로 한 번씩
//   (내장 치트 annex 와 같은 인자). 사용자가 정했다(2026-10-11).
//   "부대 만들기"(3단계 5, 부대 생성) — (플레이어의 지역 객체, 부대 설계의 번호, 1, 지도에서 고른 칸, 0, 0, NULL, 0, 0, 0) 으로 부대마다 한 번
//   (내장 치트 spawnunit 과 같은 인자). 한 번에 SPAWN_LIMIT 기까지. 사용자가 정했다(2026-10-11).
//   "배치 칸 찾기"(3단계 5, 최강 부대 묶음) — (플레이어의 지역 객체, 받을 자리, 설계의 유형) 으로 설계마다 한 번, 이어서 "부대 만들기"를
//   (플레이어의 지역 객체, 설계의 번호, 수, 그 칸, 0, 1, NULL, 1, 0, 0) 으로(내장 치트 darran 과 같은 인자. 수는 1 … 10). 사용자가 정했다(2026-10-11).
//   "GUI 의 깃발 바꾸기"(3단계 6, GUI 숨기기/보이기) — GUI 객체의 가상 함수(그 객체의 함수 표의 5번. 표는 서명으로 찾는다)를
//   (객체, 새 깃발, 0, NULL, 0, NULL, 0, 0, 1) 로 한 번. 그 앞에 화면의 깃발(전역 dword)의 한 비트를 뒤집는다. 사용자가 정했다(2026-10-11).
// "부대의 상태"(세분화 — 경험 · 사기 · 효율)는 플레이어의 부대 객체의 float 한 칸씩(+0xA0 · +0xA4 · +0x9C)에 쓴다 — 게임에서 고른 부대, 또는 내 부대 전부,
// 또는 부대 생성이 방금 만든 부대(만들기 앞뒤의 "내 부대"를 견줘 찾는다. 사용자가 정했다, 2026-10-12).
// "내 부대 전부"는 세계의 부대 목록(읽기만 한다)에서 주인이 플레이어이고 시설이 아닌 것이다 — 보급 채우기도 그렇게 할 수 있다. 사용자가 정했다(2026-10-11).
// "즉시 승리"(3단계 6, 내장 치트 instantwin 을 옮긴 것)는 전역 둘에 쓴다: 판이 끝난 꼴(dword)에 1, 이긴 지역들의 묶음에서 플레이어의 비트
// (묶음이 없으면 그 포인터 칸에 새 묶음을 건다). 그리고 게임 창에 메시지 하나를 보낸다(Windows 의 PostMessageA — 게임의 함수가 아니다). 사용자가 정했다(2026-10-11).
// "고른 부대의 보급"(3단계 5, 내장 치트 stranded 를 옮긴 것)은 게임에서 고른 부대 가운데 플레이어의 부대 객체의 float 두 칸(+0x80 · +0x84)에 쓴다
// — 플레이어 지역 객체 밖에 쓰는 곳이다. 게임의 함수는 부르지 않는다. 사용자가 정했다(2026-10-11).
// "배율"(묶음 7)은 플레이어의 곱 보정 줄(모든 지역이 함께 든 표에서 플레이어의 줄)의 칸 서른다섯 — 물자의 산업 소비 · 인구 소비 · 산출,
// 시설 건설 속도(16. 2026-10-14 — "다음 자정에 완료"는 그날의 셈 하루 동안 게임의 값 × 1000), 유닛 생산 속도(15. 2026-10-19 에 되살렸다 — 완료 대기는 맡지 않는다) — 과
// 플레이어의 연구 목록에서 진행 중인 노드의 남은 기간 칸(float)에 쓴다. 세금 · 국방비 · 사회 지출의 배율은 국고 칸에 쓴다(이미 쓰는 곳). 다른 지역의 줄에는 쓰지 않는다.
// "사회"(2026-10-21)는 플레이어 지역 객체의 사회 항목 여덟의 품질 등급 칸(float × 8 — 0 … 2, 게임 화면의 "품질 등급" ÷ 100)에 쓴다 —
// 날이 바뀔 때 그날 게임이 움직인 양에 배율을 곱해 더 움직이고, 0.5초마다 최소 유지 값으로 올린다. 다른 지역의 등급 · 지출 수준에는 쓰지 않는다(docs/superpowers/specs/2026-10-21-toybox-social-grades-design.md).
// "예비 인원"(2026-10-22)은 플레이어 지역 객체의 예비 인원 칸(float — 지휘 패널의 "예비 인원") 하나에 쓴다 — 단추로 더하고, 최소 유지로 올린다(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md).
// "충성 · 지지"(2026-10-26)는 지도의 칸 레코드에서 **주인이 플레이어인 칸의 +2(충성 대상, word)**와 플레이어 지역 객체의 **군부 지지율 칸**(float)에 새로 쓴다 —
// 점령지 충성(단추 · 5초마다의 유지)은 충성 대상이 내 편(플레이어 인덱스, 또는 묶음 칸이 플레이어의 지역 번호인 하위 지역)이 아닌 내 칸만 바꾸고,
// 국민 · 군부 지지율과 세계 시장 여론(이미 쓰는 칸)의 최소 유지는 0.5초마다 올린다. 남의 칸 · 남의 지역 객체 · 칸의 주인에는 쓰지 않고,
// 하위 지역의 묶음 칸(+0x8a)은 읽기만 한다. 게임의 함수는 부르지 않는다(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md. 사용자가 정했다, 2026-10-26).
// 날의 칸(전역)과 지역 객체의 예산 칸(세수 · 국방의 다섯 · 연구의 하루치 · 사회 지출의 비용 여덟과 수준 여덟)은 읽기만 한다. 게임의 함수는 부르지 않는다. 사용자가 정했다(2026-10-11).
// 지역 객체의 이름 글(포인터 한 칸 — 이번 판 · 지금 언어의 나라 이름)도 읽기만 한다: 창의 나라 이름이 그것을 따른다(DLC 의 역사 시나리오는 같은 번호가 다른 나라다. 2026-10-12).
// 읽기만 하는 전역이 넷 더 있다: 게임에서 고른 부대들의 목록(보급), 그리고 "지도에서 고른 지역"(전쟁의 첫째 나라), 지도 객체(병합이 칸의 주인을 읽는다 · 부대 생성이 칸이 지도 안인지 본다),
// "지도에서 고른 칸"(부대 생성).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "locate.h"
#include "research.h"

struct GameState {
    bool known = false;         // 주소를 찾았고(서명), 읽은 값이 서로 맞는다. 거짓이면 모든 단추가 꺼진다
    bool in_game = false;       // 게임을 진행 중이다
    bool multiplayer = false;
    int player = 0;             // 플레이어 지역의 번호(in_game 일 때만)
    int index = 0;              // 플레이어 지역의 인덱스(in_game 일 때만) — 지역 표의 그 칸이 플레이어 객체임을 확인한 값이다
    int regions = 0;            // 지역 수(in_game 일 때만)
};

// 플레이어의 값(locate_values 가 찾은 자리에서 읽는다).
struct GameValues {
    bool ok = false;                 // 읽었다: 게임을 진행 중이고 재고 열두 칸이 모두 유한한 수다
    double treasury = 0;             // 국고(달러). 유한한 수가 아닐 수 있다 — 쓰는 쪽(values.h)이 거른다
    bool used[STOCK_SLOTS] = {};     // 이번 판에서 쓰는 물자인가
    float stock[STOCK_SLOTS] = {};   // 재고
};

// 더 쓰는 값(locate_more 가 찾은 자리에서 읽는다): 플레이어의 기술 수준과 세계 시장 여론의 세 칸.
struct GameMore {
    bool ok = false;         // 읽었다: 게임을 진행 중이다. 못 찾은 묶음의 값은 0 으로 둔다
    float tech = 0;          // 기술 수준. 유한한 수가 아닐 수 있다 — 쓰는 쪽(keeper)이 거른다
    float opinion[3] = {};
    float people[2] = {};    // 인구 칸과 인구의 풀 칸
    float approval = 0;      // 국내 지지율(0 … 1)
};

// 한 나라와의 관계: [0] · [1] 은 관계 표 둘, [2] 는 전쟁 명분 표.
struct Relation {
    bool ok = false;         // 읽었다: 게임을 진행 중이고 그 나라가 이번 판에 있다(플레이어 자신이 아니다)
    float mine[3] = {};      // 플레이어 객체의 표에서 그 나라의 칸
    float theirs[3] = {};    // 그 나라 객체의 표에서 플레이어의 칸
};

enum class Wrote {
    Done,        // 썼고, 다시 읽어 그 값인 것을 봤다
    NotInGame,   // 게임을 진행 중이 아니다(멀티플레이, 읽은 값이 서로 맞지 않는 것 포함)
    NotUsed,     // 이번 판에서 쓰지 않는 물자의 칸이다
    BadValue,    // 쓸 값이 유한한 수가 아니다, 재고가 음수다, 없는 칸이다
    Failed,      // 쓸 수 없는 자리이거나 쓴 값이 남지 않았다
    Off,         // 값 쓰기가 꺼져 있다(game_values_off · game_more_off), 그 묶음의 자리를 모른다
    NoTarget,    // 그 번호의 나라가 이번 판에 없다(없는 번호, 사람도 AI 도 맡지 않은 지역, 플레이어 자신)
    Unreadable,  // (연구) 표나 연구 목록을 읽을 수 없다 — 꼴이 다른 게임일 수 있다. 쓰지 않았다
    Crashed,     // (연구) 비트는 썼는데 효과를 다시 셈하는 게임의 함수에서 예외가 났다 — 게임의 상태를 믿을 수 없다
};

// 연구를 쓴 결과(write_research).
struct ResearchDone {
    ResearchPlan plan;           // 바꾼 것(묶음을 만들 수 없어 건너뛴 항목은 빠져 있다)
    int housed = 0;              // 새로 만들어 건 보유 비트 묶음의 수
    int skipped = 0;             // 묶음을 만들 수 없어 건너뛴 항목의 수(아무도 보유하지 않은 항목)
    int cells = 0, written = 0;  // 쓰려던 칸의 수 · 쓴 칸의 수(비트 하나, 포인터 하나, 노드의 깃발 하나가 저마다 한 칸)
    bool recomputed = false;     // 플레이어 지역의 효과를 다시 셈했다
    unsigned long code = 0;      // 다시 셈에서 난 예외의 코드(Wrote::Crashed)
    std::string why;             // 읽지 못한 까닭(Wrote::Unreadable) · 쓰지 못한 곳(Wrote::Failed)
};

// 게임의 "지역의 효과를 다시 셈": 그 지역이 보유한 기술의 효과를 처음부터 다시 쌓는다. 다른 함수를 부르지 않는다(docs/11).
typedef void (*Recompute)(void *world, int index);

// base: 실행 파일이 올라온 주소(테스트에서는 가짜 메모리). 읽을 수 없는 주소를 만나도 죽지 않는다.
GameState read_game(const uint8_t *base, const GameAddresses &at);
// 이번 게임에 실제로 있는 나라(사람이나 AI 가 맡은 지역)의 번호들(오름차순). 진행 중이 아니면 빈 목록.
std::vector<int> read_regions(const uint8_t *base, const GameAddresses &at);
GameValues read_values(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout);
// 쓰기 직전에 다시 확인한다: 게임을 진행 중이다(일관성 포함) · 멀티플레이가 아니다 · (재고) 그 칸이 쓰는 물자다 ·
// 쓸 값이 유한한 수다 · 쓸 자리가 읽기 · 쓰기 쪽이다. 쓴 뒤 다시 읽어 그 값인지 본다(아니면 한 번 더).
Wrote write_treasury(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout, double value);
Wrote write_stock(const uint8_t *base, const GameAddresses &at, const ValueLayout &layout, int slot, float value);
GameMore read_more(const uint8_t *base, const GameAddresses &at, const MoreLayout &more);
Relation read_relation(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int number);
// 쓰기 직전의 확인은 write_treasury 와 같다. 그 묶음의 자리를 모르면(0) Wrote::Off.
// 여러 칸을 쓰는 것은 쓰기 전에 모든 칸이 읽기 · 쓰기 쪽인지 먼저 본다(반쪽만 쓰고 멈추지 않게). done 에 쓴 칸의 수.
Wrote write_tech(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, float value);   // 0 이상의 유한한 수
Wrote write_opinion(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int *done);  // 세 칸에 1.0
// 인구 칸과 인구의 풀 칸에 add 를 더한다. 풀에 더한 것이 자정의 셈("민간 인구 + 풀 + 현역 인력")에 남고, 인구 칸에 더한 것은 그때까지
// 화면에 보이게 하는 것이다(내장 치트 populate 는 인구 칸 · 군 인력 칸 · 셋째 칸에 더했고 첫 자정에 되돌아갔다).
// 두 칸 가운데 하나라도 유한한 수가 아니거나 음수면 쓰지 않는다(BadValue).
Wrote write_people(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, float add, int *done);
Wrote write_approval(const uint8_t *base, const GameAddresses &at, const MoreLayout &more);            // 지지율 칸에 1.0
// 그 번호의 나라와의 관계를 level(-1 … 1)로, 전쟁 명분을 0 으로: 플레이어 객체의 그 나라 칸 셋, 그 나라 객체의 플레이어 칸 셋.
Wrote write_relation(const uint8_t *base, const GameAddresses &at, const MoreLayout &more, int number, float level, int *done);

// 연구의 표 둘과 플레이어의 연구 목록을 읽는다(행의 queued 까지 채운다). picked: 고른 나라의 번호 — 이번 판에 없거나 0 이면 행의
// picked 는 모두 false 다. 읽을 수 없으면 false 와 why: 게임을 진행 중이 아니다(일관성 포함) / 멀티플레이다 / 표의 자리 수가 2 … 65536 이
// 아니다 / 표 · 보유 묶음 · 연구 목록의 노드 가운데 읽을 수 없는 것이 있다 / 연구 목록이 4096 노드 안에 끝나지 않는다.
bool read_research(const uint8_t *base, const GameAddresses &at, const ResearchLayout &research, int picked, ResearchTables *out,
                   std::string *why);
// 연구를 완료 · 미완료로 바꾼다. 방금 읽은 표에 규칙(research_plan)을 돌려 바꿀 것을 정하고, 쓰기 전에 모든 칸이 읽기 · 쓰기 쪽인지 본다.
//   비트    보유 묶음의 (플레이어 인덱스 / 8)째 바이트에서 그 비트만, 원자적으로 — 같은 바이트의 다른 나라의 비트는 읽어서
//           되쓰지 않는다(그 순간 게임이 다른 나라의 연구를 끝내도 덮어쓰지 않는다).
//   묶음    완료로 바꿀 항목에 묶음이 없으면 프로세스 힙에서 OWNERS_BYTES 를 0 으로 받아 레코드의 포인터 칸에 건다(게임이 하는 그대로).
//           먼저 이미 있는 묶음 하나가 그 힙의 OWNERS_BYTES 짜리 블록인지 본다 — 아니면 만들지 않고 그 항목들을 건너뛴다(skipped).
//           거는 것은 칸이 아직 비어 있을 때만이다(hang_owners) — 표를 읽은 뒤에 게임이 제 묶음을 걸었으면 그 묶음을 쓴다.
//   노드    대기열에서 뺄 노드의 깃발 둘에 NODE_GONE 을 켠다(게임이 연구를 끝낼 때 하는 그대로). 그 비트만, 원자적으로.
//   다시 셈 기술의 비트를 하나라도 바꿨으면 끝에 recompute(세계 객체, 플레이어 인덱스)를 한 번 부른다. -1(모든 지역)로는 부르지 않는다.
// 순서는 칸의 확인 → 묶음 → 비트 → 노드 → 다시 셈. 도중에 쓰기가 실패하면 거기서 멈춘다(Wrote::Failed. 앞서 쓴 것은 되돌리지 않고
// 다시 셈도 부르지 않는다). 바꿀 것이 없어도 Wrote::Done 이다(done->plan 이 비어 있다).
Wrote write_research(const uint8_t *base, const GameAddresses &at, const ResearchLayout &research, Recompute recompute, Research action,
                     const ResearchWhat &what, ResearchDone *done);
// 비어 있는(0) 포인터 칸에 새 묶음(block)을 건다 — 한 번의 원자적 비교 · 교환으로. 그사이 게임이 그 칸에 제 묶음을 걸었으면(다른 나라가
// 그 항목을 처음 보유했다) 덮어쓰지 않는다 — 덮어쓰면 그 나라의 보유가 사라진다. *owners 는 그 칸에 걸려 있는 묶음이다.
// 0 걸었다 / 1 이미 걸려 있었다(block 은 쓰이지 않았다) / -1 쓸 수 없는 칸이다(*owners 는 그대로).
int hang_owners(uint64_t cell, uint64_t block, uint64_t *owners);

void game_init();                  // 올라와 있는 실행 파일에서 주소와 자리를 찾는다(시작할 때 한 번). 결과를 로그에 적는다
void game_init_from(const uint8_t *base, size_t size);   // 그 일의 몸통(테스트는 가짜 이미지로 부른다)
GameState game_state();            // 지금의 상태. 부를 때마다 읽는다 — 플레이어는 게임 중에 바뀔 수 있다(cheat becomeregion)
std::vector<int> game_regions();
bool game_reads();                 // 상태 묶음을 찾았다(게임을 읽는다)

std::string game_values_off();     // 값을 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글
GameValues game_values();          // 지금의 값. 부를 때마다 읽는다
// 쓰기가 실패하면(Wrote::Failed) 이번 실행에서는 값 쓰기를 끄고 로그에 적는다 — 그 뒤로는 Wrote::Off 다.
Wrote game_write_treasury(double value);
Wrote game_write_stock(int slot, float value);

// 값 쓰기가 켜져 있다: 게임을 읽고, SRTOYBOX_WRITE 가 0 이 아니고, 이번 실행에서 쓰기가 실패한 적이 없다.
// 어느 묶음의 자리를 찾았는지는 보지 않는다(game_values_off · game_more_off 가 본다).
bool game_writes();
// 더 쓰는 값의 묶음(MORE_TECH · MORE_OPINION · MORE_RELATIONS · MORE_PEOPLE · MORE_APPROVAL)을 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글.
// 묶음마다 따로다 — 한 묶음을 못 찾아도 다른 묶음과 국고 · 재고는 그대로다.
std::string game_more_off(int group);
GameMore game_more();                  // 지금의 값. 부를 때마다 읽는다
Relation game_relation(int number);
Wrote game_write_tech(float value);    // 실패하면 game_write_treasury 처럼 값 쓰기 전체를 끈다
Wrote game_write_opinion();
Wrote game_write_people(float add);
Wrote game_write_approval();
Wrote game_write_relation(int number, float level);

// 게임의 함수를 부르는 기능(ACT_COLONIZE · ACT_FIGHT · ACT_BECOME · ACT_ELECT = 지도자 제거 · ACT_TREATY = 동맹 맺기 · ACT_ANNEX = 병합)을 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글. 다른 묶음과 따로다.
std::string game_act_off(int act);
// 고른 나라(지역 번호)를 플레이어의 식민지로: 게임의 함수를 (플레이어의 지역 객체, 그 나라의 인덱스, 1) 로 한 번 부른다 — 구조적 예외 가드 안에서.
// 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 그 나라가 이번 판에 있고 플레이어가 아니다(NotInGame · NoTarget).
// 함수 안에서 예외가 나면 Wrote::Crashed 와 code — 부른 쪽이 오류 가드를 건다(runner_win.h).
Wrote game_colonize(int number, unsigned long *code);
// 게임의 지도에서 고른 지역의 번호(읽기만). 고른 것이 없거나 이번 판에 없는 지역이거나 읽을 수 없으면 0.
int game_map_pick();
// 지도에서 고른 나라(attacker)와 목록에서 고른 나라(target)를 싸움 붙인다: 게임의 함수를
// (attacker 의 지역 객체, 1, target 의 인덱스, 0, 0) 로 한 번 부른다 — 구조적 예외 가드 안에서.
// 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 두 나라가 이번 판에 있고 서로 다르다(NotInGame · NoTarget). 플레이어여도 막지 않는다.
Wrote game_fight(int attacker, int target, unsigned long *code);
// 플레이하는 나라를 그 번호의 나라로 바꾼다(내장 치트 becomeregion 이 하는 그대로): 전역 넷(플레이어의 인덱스 둘 · 포인터 둘)을 쓰고
// 게임의 함수를 (그 나라의 지역 객체, 0) 으로 한 번 부른다 — 구조적 예외 가드 안에서.
// 쓰기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 그 나라가 이번 판에 있고 지금의 플레이어가 아니다(NotInGame · NoTarget) /
// 전역의 지금 값이 말이 된다 — 인덱스 둘이 같고, 첫째 포인터가 지금의 플레이어 객체이고, 둘째 포인터가 비어 있지 않다(아니면 BadValue. 쓰지 않는다).
// 둘째 포인터는 첫째와 달라도 된다: 게임이 첫 자정부터 그것을 플레이어 지역의 전날 사본으로 돌린다 / 네 칸이 모두 읽기 · 쓰기 쪽이다.
// 쓰다가 실패하면 앞서 쓴 칸을 되돌리고 Wrote::Failed(값 쓰기 전체를 끈다). 함수 안에서 예외가 나면 Wrote::Crashed(전역은 새 나라인 채다).
Wrote game_become(int number, unsigned long *code);
// 그 번호의 나라의 지도자를 없앤다(내장 치트 novichok 이 하는 그대로 — 게임이 지도자가 죽은 날 스스로 하는 것과도 같다):
// 지도자 표에서 그 나라의 지도자 레코드의 깃발에 LEADER_GONE 을 켜고, "새 지도자 세우기"를 (그 나라의 지역 객체, 0, 0) 으로,
// "알림"을 (그 지역 객체, NEWS_LEADER_GONE × 4, 1, NULL, 앞 지도자, 지역 인덱스, 새 지도자, 0, 0, 0, 0) 으로 부른다 — 구조적 예외 가드 안에서.
// 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 그 나라가 이번 판에 있고 플레이어가 아니다(NotInGame · NoTarget) /
// 지도자 표의 포인터와 지도자 번호를 읽을 수 있다(아니면 BadValue) / 깃발 칸이 읽기 · 쓰기 쪽이다(아니면 Failed — 값 쓰기 전체를 끈다).
Wrote game_novichok(int number, unsigned long *code);
// 그 번호의 나라와 동맹을 맺는다(내장 치트 treaty 가 하는 그대로): 게임의 "조약" 함수를 네 번 —
// (플레이어의 객체, 0 · 1, TREATY_ALLIANCE, 1, 그 나라의 인덱스, 1, 1, 0) 과 (그 나라의 객체, 0 · 1, TREATY_ALLIANCE, 1, 플레이어의 인덱스, 1, 1, 0).
// 한 구조적 예외 가드 안에서. 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 그 나라가 이번 판에 있고 플레이어가 아니다(NotInGame · NoTarget) /
// 플레이어의 조약 표에서 그 조약의 비트 묶음을 읽어 **이미 맺었으면 부르지 않는다**(NotUsed — 치트는 그때 깬다). 읽을 수 없으면 BadValue.
Wrote game_treaty(int number, unsigned long *code);
// 그 번호의 나라의 땅을 모두 플레이어의 것으로 만든다(내장 치트 annex 가 하는 그대로): 지도의 칸 레코드를 한 번에 읽어 주인이 그 나라인 칸을 고르고,
// 칸마다 게임의 함수를 (그 칸의 묶음, 플레이어의 인덱스, 0, 0, 0, 0) 으로 부른다 — 모두 한 구조적 예외 가드 안에서. *hexes 에 부른 칸의 수.
// 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 · 그 나라가 이번 판에 있고 플레이어가 아니다(NotInGame · NoTarget) /
// 지도의 꼴이 말이 된다 — 포인터 셋이 비어 있지 않고, 너비가 1 … 65535, 칸 수가 너비의 배수이고 세로가 65535 이하이며,
// 칸 레코드를 통째로 읽을 수 있고, 주인의 인덱스가 모두 지역 표 안이다(아니면 BadValue. 한 칸도 부르지 않는다).
// 함수 안에서 예외가 나면 Wrote::Crashed — 그때까지 부른 칸은 이미 넘어갔다(*hexes).
Wrote game_annex(int number, unsigned long *code, int *hexes);
// 게임에서 고른 부대의 수와 그 가운데 플레이어의 부대의 수. 읽을 수 없으면 거짓. 읽기만 한다.
bool game_picked_units(int *all, int *mine);
// 게임에서 고른 부대 가운데 플레이어의 부대의 보급 두 칸을 "가득"(fill) 또는 0 으로 쓴다. "가득"은 내장 치트 stranded 가 채울 때 쓰는 값과
// 같은 셈이다: 규모 × 설계의 용량 × 개량의 보정 × 보유한 기술의 효과(둘째 칸은 실은 부대가 넘치면 그만큼 뺀다). 치트와 달리 번갈아 하지 않는다.
// 모두 읽고 셈한 뒤에 쓴다 — 목록 · 부대 · 표 가운데 하나라도 말이 안 되면 하나도 쓰지 않는다(Wrote::BadValue).
// 부대 객체인지는 그 설계 포인터가 부대 설계 표 안의 레코드인가로 본다. 남의 부대는 건너뛴다(*skipped). 게임의 함수는 부르지 않는다.
// all 이면 고른 부대가 아니라 세계의 부대 목록에서 플레이어의 부대(시설은 뺀다 — 예비 부대는 든다) 전부에.
Wrote game_supply(bool fill, bool all, int *done, int *skipped);
// 부대의 상태 하나(which: STAT_EXPERIENCE · STAT_MORALE · STAT_EFFICIENCY)를 value(0 … 1)로 쓴다 — 게임에서 고른 부대 가운데 내 부대에,
// all 이면 내 부대 전부에(시설은 뺀다). 모두 읽은 뒤에 쓴다: 목록 · 부대(설계 포인터가 설계 표 안인가) 가운데 하나라도 말이 안 되면 하나도 쓰지 않는다.
// 남의 부대 · 시설은 건너뛴다(*skipped). 게임의 함수는 부르지 않는다.
Wrote game_unit_stat(int which, float value, bool all, int *done, int *skipped);
// GUI 숨기기/보이기(내장 치트 fullmapshow 가 하는 그대로): 화면의 깃발에서 GUI_HIDDEN 비트를 뒤집고, GUI 객체가 있으면 그 객체의 가상 함수
// (함수 표의 GUI_SET 번)를 (객체, 객체의 깃발에 숨김 비트를 켜거나 끈 값, 0, NULL, 0, NULL, 0, 0, 1) 로 부른다 — 구조적 예외 가드 안에서.
// 객체의 함수 표가 서명으로 찾아 둔 표가 아니면 깃발도 쓰지 않는다(Wrote::BadValue). *hidden 은 이제 숨겨졌는가.
Wrote game_gui_toggle(unsigned long *code, bool *hidden);
// 즉시 승리(내장 치트 instantwin 이 하는 그대로): 이긴 지역들의 묶음에서 플레이어의 비트를 켜고(묶음이 없으면 프로세스 힙에서 받아 건다 —
// 연구의 새 묶음과 같은 검사), 판이 끝난 꼴에 1 을 쓰고, 게임 창(window)에 메시지 VICTORY_MESSAGE 를 보낸다(PostMessageA). 게임의 함수는 부르지 않는다.
// 쓰기 전에 본다: 진행 중 · 멀티플레이가 아니다 / 판이 끝난 꼴이 0 이다(아니면 Wrote::NotUsed — 이미 끝난 판) / 쓸 칸이 쓸 수 있는 쪽이다.
Wrote game_victory(void *window);
// 최강 부대 묶음(내장 치트 darran 의 "받기" 쪽만 — 적 부대를 없애는 쪽은 옮기지 않았다. 사용자가 정했다, 2026-10-11):
// 병과마다 BEST_STAT 의 능력치가 가장 높은 설계를 골라(보유 여부는 보지 않는다 — 치트와 같다), 설계마다 게임의 "배치 칸 찾기"로 칸을 얻고
// "부대 만들기"를 (플레이어의 지역 객체, 설계의 번호, each, 그 칸, 0, 1, NULL, 1, 0, 0) 으로 한 번 부른다. domain 이 0 이상이면 그 영역(design_domain)의 병과만.
// each 는 1 … BEST_EACH_MAX. *made 는 부른 설계(병과)의 수. 함수 안에서 예외가 나면 Wrote::Crashed — 그때까지 만든 부대는 남는다.
Wrote game_best(int domain, int each, unsigned long *code, int *made);
// 게임의 지도에서 고른 칸(가로 · 세로). 고르지 않았거나 지도 밖이거나 읽을 수 없으면 거짓. 읽기만 한다.
bool game_map_hex(int *x, int *y);
// 게임의 지도에서 고른 칸에 플레이어의 부대를 만든다(내장 치트 spawnunit 이 하는 그대로): 설계마다 each 번, 게임의 "부대 만들기"를
// (플레이어의 지역 객체, 설계의 번호, 1, 그 칸, 0, 0, NULL, 0, 0, 0) 으로 부른다 — 모두 한 구조적 예외 가드 안에서.
// 부르기 전에 본다: 진행 중 · 멀티플레이가 아니다 / 칸을 골랐고 지도 안이다(아니면 Wrote::NoTarget) /
// 설계가 모두 표에 있다(번호 1 … SPAWN_MAX_EQUIP, 빈 자리가 아니다) · 설계의 수 × each 가 SPAWN_LIMIT 이하(아니면 Wrote::BadValue — 하나도 만들지 않는다).
// 함수 안에서 예외가 나면 Wrote::Crashed — 그때까지 만든 부대는 남는다(*made). *x · *y 는 그 칸.
// stats 를 주면(STAT_COUNT 칸 — STAT_* 의 순서, 0 … 1. 음수인 칸은 그대로) 만든 부대의 경험 · 사기 · 효율에 쓴다: 부르기 전과 뒤의 "내 부대"
// (세계의 부대 목록에서 주인이 플레이어이고 시설이 아닌 것)를 견줘 새로 생긴 부대를 찾는다 — 게임의 함수가 돌려주는 값은 쓰지 않는다.
// *stated 는 상태를 쓴 부대의 수. 부대 목록을 읽을 수 없거나 새 부대를 못 찾았으면 부대는 만든 채로 -1.
Wrote game_spawn(const int *designs, int n, int each, const float *stats, unsigned long *code, int *made, int *stated, int *x, int *y);
// 이번 판에 있는 나라마다 게임이 지금 쓰는 이름(지역 번호, UTF-8) — 지역 객체의 이름 글을 읽는다(읽기만). 이름은 판마다 다르다
// (역사 시나리오의 1188 은 독일, 202 는 다호메이)이고 게임의 언어를 따른다. 그 자리를 못 찾았거나 진행 중이 아니면 빈 목록,
// 읽을 수 없는 지역은 빠진다.
std::vector<std::pair<int, std::string>> game_region_names();
// 테스트: 부르는 함수의 자리를 준다(함수는 그 자리에 둘 함수, layout 의 자료는 "게임"의 그 자리, 상수는 그 값). nullptr 이면 못 찾은 것으로.
void game_set_acts_for_test(const ActLayout *layout, void *colonize, void *fight, void *become, void *elect, void *news, void *treaty,
                            void *annex, void *spawn, void *deploy);

// 그 기술(design 이 거짓) · 부대 설계를 보유한 다른 나라(플레이어는 뺀다)의 이름을 모두, ", " 로 이어서(읽기만 — 목록의 풍선말).
// 읽을 수 없거나 아무도 없으면 빈 글. 이름표에 없는 지역은 "#인덱스".
std::string game_owner_names(bool design, int id);
// 연구를 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글. 다른 묶음과 따로다.
std::string game_research_off();
// 지금의 연구의 표(read_research). 연구 묶음을 못 찾았으면 false 와 그 까닭.
bool game_research(int picked, ResearchTables *out, std::string *why);
// 연구를 완료 · 미완료로 바꾼다(write_research). 쓰기가 실패하면(Wrote::Failed) 값 쓰기 전체를 끄고 로그에 적는다 — 그 뒤로는 Wrote::Off 다.
// Wrote::Crashed 는 부른 쪽이 처리한다(오류 가드를 건다 — runner_win.h).
Wrote game_write_research(Research action, const ResearchWhat &what, ResearchDone *done);

// 배율(묶음 7 — docs/superpowers/specs/2026-10-11-toybox-stage7-design.md). 게임의 함수는 부르지 않는다.
// 쓰는 곳은 둘이다: 플레이어의 곱 보정 줄의 칸(game_write_mult), 플레이어의 연구 목록에서 진행 중인 노드의 남은 기간(game_research_push).
// 세금 · 국방비 · 사회 지출은 keeper 가 game_daily 로 하루치를 읽어 국고에 쓴다(game_write_treasury).
const int MULT_PART_CELLS = 0, MULT_PART_MONEY = 1, MULT_PART_SPEED = 2, MULT_PART_SOCIAL = 3;   // game_mult_off 의 "무엇을"
// 그 배율을 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글. 물자의 생산 · 소비는 곱 보정 표를,
// 세금 · 국방비는 날의 칸 · 예산 칸과 국고 칸(값 묶음)을, 사회 지출은 날의 칸 · 사회 지출의 두 칸과 국고 칸을,
// 연구 속도는 날의 칸 · 하루치 칸과 연구 목록(연구 묶음)을 찾아야 한다.
std::string game_mult_off(int part);
// 플레이어의 곱 보정 줄(MUL_CELLS 칸)을 읽는다. *index 에 플레이어의 지역 인덱스. 진행 중이 아니거나 읽을 수 없으면 거짓. 읽기만 한다.
bool game_mult_row(float *cells, int *index);
// 플레이어의 곱 보정 줄의 그 칸(1 … MUL_CELLS - 1)에 value 를 쓴다. 0 보다 크고 MUL_VALUE_MAX 보다 작은 유한한 수만(아니면 BadValue).
// 쓰기가 실패하면 값 쓰기 전체를 끈다(Wrote::Failed).
const float MUL_VALUE_MAX = 1000.0f;
// max 는 완료 대기(keeper 의 "다음 자정에 완료")가 칸 16 에만 RUSH_VALUE_MAX 로 넘긴다 — 100000 을 넘을 수 없다.
Wrote game_write_mult(int cell, float value, float max = MUL_VALUE_MAX);
// 날마다의 일에 쓰는 값. 못 찾은 묶음의 값은 0 이다.
struct Daily {
    bool ok = false;           // 읽었다: 게임을 진행 중이고(멀티플레이가 아니다) 날의 칸을 읽었다
    int index = 0;             // 플레이어의 지역 인덱스
    uint32_t day = 0;          // 날의 칸
    double tax = 0;            // 하루치 세수(달러) = 연간 세수의 합 ÷ 365. 유한한 수가 아니거나 음수면 0
    double defense = 0;        // 하루치 국방 지출(달러) = (인건비 수준 × 인건비 + 유지비 수준 × 유지비) ÷ 365 + 어제의 부대 생산비. 〃
    double social = 0;         // 하루치 사회 지출(달러) = Σ 비용 × 지출 수준 ÷ 365(여덟 항목 — 재무 패널의 "일일 사회 비용"). 〃
    float step = 0;            // 연구의 하루치(일). 유한한 수가 아니거나 0 … DAILY_STEP_MAX 밖이면 0
};
const float DAILY_STEP_MAX = 100.0f;
Daily game_daily();
// 플레이어의 연구 목록에서 진행 중이고(둘째 깃발의 NODE_ACTIVE) 빠지지 않은 노드마다 남은 기간에서 days 를 뺀다(음수면 더한다).
// NODE_LEFT_MIN 아래로는 내리지 않는다 — 끝내는 것은 게임이 한다. 읽은 값이 유한한 수가 아니거나 0 이하이거나 NODE_LEFT_MAX 를 넘는 노드는
// 건드리지 않는다. *nodes 에 고친 노드의 수. 목록을 읽을 수 없으면 Wrote::Unreadable(하나도 쓰지 않는다), 쓰기가 실패하면 Wrote::Failed.
const float NODE_LEFT_MIN = 0.001f, NODE_LEFT_MAX = 100000.0f;
Wrote game_research_push(float days, int *nodes);
// 사회 항목의 품질 등급(docs/superpowers/specs/2026-10-21-toybox-social-grades-design.md). 쓰는 곳은 플레이어 지역 객체의 등급 여덟 칸뿐이다.
// 찾아야 하는 것: 날의 칸 · 등급 칸(game_mult_off(MULT_PART_GRADE)).
const int MULT_PART_GRADE = 4;
const float SOCIAL_GRADE_SANE = 10.0f;      // 0 이상 이보다 작은 유한한 수만 등급으로 친다(읽을 때도 쓸 때도)
const float SOCIAL_GRADE_TOP = 2.0f;        // 게임이 자정마다 자르는 끝(0x76a040)
struct Grades {
    bool ok = false;           // 게임을 진행 중이고(멀티플레이가 아니다) 날의 칸과 등급 여덟을 읽었다
    int index = 0;             // 플레이어의 지역 인덱스
    uint32_t day = 0;          // 날의 칸
    float grade[SOCIAL_COUNT] = {};   // 읽은 그대로(말이 되는지는 부른 쪽이 본다)
};
Grades game_social_grades();
// 플레이어의 품질 등급 item(0 … SOCIAL_COUNT - 1)에 value 를 쓴다. value 와 지금 값이 모두 0 이상 SOCIAL_GRADE_SANE 미만의 유한한 수일 때만
// (아니면 BadValue). 쓰기가 실패하면 값 쓰기 전체를 끈다(Wrote::Failed).
Wrote game_write_social_grade(int item, float value);
// 예비 인원(docs/superpowers/specs/2026-10-22-toybox-reserve-design.md). 쓰는 곳은 플레이어 지역 객체의 예비 인원 칸 하나뿐이다. 찾아야 하는 것: 예비 칸(game_mult_off(MULT_PART_RESERVE)).
const int MULT_PART_RESERVE = 5;
// 충성 · 지지(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md): 군부 지지율 칸 · 묶음 칸(읽기만)
const int MULT_PART_MILAPPROVAL = 6, MULT_PART_GROUP = 7;
const float RESERVE_MAX = 100000000.0f;     // 쓰는 값의 끝(1억 명). 읽은 값도 0 이상 이 아래여야 말이 된다
// 플레이어의 예비 인원을 *out 에. 진행 중이 아니거나 읽을 수 없으면 거짓(값이 말이 되는지는 부른 쪽이 본다).
bool game_reserve(float *out);
// 플레이어의 예비 인원에 value 를 쓴다. value 와 지금 값이 모두 0 이상 RESERVE_MAX 이하의 유한한 수일 때만(아니면 BadValue).
Wrote game_write_reserve(float value);
// 테스트: 배율의 자리를 준다 — 자리가 0 이 아닌 것을 찾은 것으로 친다. nullptr 이면 모두 못 찾은 것으로.
void game_set_mult_for_test(const MultLayout *layout);
// cell 의 qword 가 was 이면 now 로 바꾼다(CAS). 0 바꿨다 · 1 값이 was 가 아니었다(그대로 둔다) · -1 쓸 수 없다. write_research 가 지역 코드 칸에 쓴다.
int swap_qword(uint64_t cell, uint64_t was, uint64_t now);
// cell 의 dword 가 was 이면 now 로 바꾼다(CAS). 돌려주는 값은 swap_qword 와 같다. game_loyalty 가 칸 레코드의 +0 (주인 | 충성 대상)에 쓴다.
int swap_dword(uint64_t cell, uint32_t was, uint32_t now);
// 점령지 충성(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md). 쓰는 곳은 주인이 플레이어인 칸 레코드의 +2(충성 대상, word)뿐이다.
// 지도를 한 번에 읽어(스냅숏) 주인이 플레이어이고 충성 대상 L 이 "내 편"이 아닌 칸을 고른다 — 내 편 = L 이 플레이어 인덱스이거나 L 의 지역 객체의
// 묶음 칸(game_mult_off(MULT_PART_GROUP))이 플레이어의 지역 번호. L = 0 인 칸은 건드리지 않는다. 쓰는 값은 플레이어의 "내 편" 칸에 가장 많이 든 L
// (같으면 작은 인덱스) — *target, 그 지역 번호는 *number. 내 편 칸이 하나도 없으면 BadValue(한 칸도 쓰지 않는다). 지도의 꼴(칸 수가 너비의 배수 ·
// 세로 65535 이하 · 주인과 충성 대상이 모두 지역 표 안)이 말이 안 되면 Unreadable. 칸마다 dword CAS 로 써서 그사이 게임이 바꾼 칸은 건너뛴다. *hexes 에 바꾼 칸의 수.
// 게임의 함수는 부르지 않는다.
Wrote game_loyalty(int *hexes, int *target, int *number);
std::string game_loyalty_off();             // 점령지 충성을 쓸 수 없는 까닭(창에 그대로 보인다). 쓸 수 있으면 빈 글
void game_set_loyalty_hook_for_test(void (*hook)());   // 테스트: 스냅숏 뒤 · 쓰기 전에 부를 함수(없으면 nullptr)
// 지지율 · 여론의 최소 유지(같은 설계서). 쓰는 곳은 플레이어의 국민 지지율(MORE_APPROVAL 의 칸) · 군부 지지율(MULT_PART_MILAPPROVAL) ·
// 세계 시장 여론(MORE_OPINION 의 둘째 칸) — 셋 다 float 0 … 1.
enum class Floor { Civ = 0, Mil = 1, Opinion = 2 };
const int FLOOR_COUNT = 3;
std::string game_floor_off(Floor which);    // 그 칸을 쓸 수 없는 까닭. 쓸 수 있으면 빈 글
// 플레이어의 그 칸을 *out 에. 진행 중이 아니거나 읽을 수 없으면 거짓(값이 말이 되는지는 부른 쪽이 본다).
bool game_floor_value(Floor which, float *out);
// 그 칸에 value 를 쓴다. value 와 지금 값이 모두 0 … 1 의 유한한 수일 때만(아니면 BadValue). 쓰기가 실패하면 값 쓰기 전체를 끈다(Failed).
Wrote game_write_floor_value(Floor which, float value);

// 테스트: 이 프로세스의 "게임"을 가짜 메모리로 바꾼다. handler 는 명령 처리 함수 자리에 둘 함수(없으면 nullptr).
// 값의 자리는 비워 두고 값 쓰기의 실패 표시도 지운다 — 자리는 game_set_values_for_test 로 따로 준다.
void game_set_for_test(const uint8_t *base, const GameAddresses *at);
void game_set_values_for_test(const ValueLayout *layout);
// 더 쓰는 값의 자리를 준다 — 자리가 0 이 아닌 묶음을 찾은 것으로 친다. nullptr 이면 모두 못 찾은 것으로.
void game_set_more_for_test(const MoreLayout *layout);
// 연구의 자리와 "다시 셈" 자리에 둘 함수를 준다(layout 의 recompute 는 보지 않는다). nullptr 이면 못 찾은 것으로.
void game_set_research_for_test(const ResearchLayout *layout, void *recompute);
