"""ToyBox 의 값 쓰기: 값 계산(values) · 게임의 값 읽기와 쓰기(game) · 요청의 대기열(keeper). 게임은 띄우지 않는다."""
import ctypes
import struct

import pytest

import toybox_fake_exe
from toybox_fake_game import LAYOUT, MORE, MULTIPLAYER, OBJECT_SIZE, OPTIONS, POINTER, RELATION_TABLES, TABLE, WORLD_POINTER, FakeGame
from srkit import toybox

ADD, SET, FLOOR = 0, 1, 2                 # native/srtoybox/values.h 의 Change
WRITE, NOTHING, REFUSE = 0, 1, 2          # 〃 Verdict
NAN, INF = float("nan"), float("inf")
DONE, NOT_IN_GAME, NOT_USED, BAD_VALUE, FAILED, OFF, NO_TARGET = range(7)   # native/srtoybox/game.h 의 Wrote
TREASURY, ALL_STOCK = -1, -2                                        # 쓰기의 대상: 국고 / 쓰는 물자 모두. 0 … 11 은 그 칸의 재고
READS, WRITES = 1, 4                                                # srtoybox_game_flags 의 비트(2 는 없어졌다 — 명령 처리 함수를 부르지 않는다)
CAN_TECH, CAN_OPINION, CAN_RELATIONS, CAN_PEOPLE, CAN_APPROVAL = 8, 16, 32, 128, 256   # 〃 더 쓰는 값의 묶음마다(64 는 연구)
TECH_TO, OPINION_BEST, RELATION_TO, PEOPLE_ADD, APPROVAL_BEST = 0, 1, 2, 3, 4   # srtoybox_more_write 의 what
MORE_TECH, MORE_OPINION, MORE_RELATIONS = 1, 2, 4                   # native/srtoybox/locate.h 의 묶음의 비트
TECH, OPINION, RELATION = -3, -4, -5                                # native/srtoybox/keeper.h 의 Request.slot(더 쓰는 값의 요청)
BEST, NEUTRAL = 1.0, 0.0                                            # 관계의 수준
UNREAD = "게임 상태를 읽을 수 있을 때만 씁니다."
FAILED_OFF = "값 쓰기가 실패해 껐습니다. 게임을 다시 시작하면 다시 시도합니다."
LEFT_GAME = "게임이 진행 중이 아니어서 쓰지 않았습니다."


@pytest.fixture(scope="module")
def lib(cfg):
    path = toybox.output(cfg)
    if not path.is_file():
        pytest.skip("ToyBox DLL 미빌드 (srkit toybox-build)")
    lib = toybox.library(cfg)
    lib.srtoybox_value.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.POINTER(ctypes.c_double)]
    lib.srtoybox_short_number.argtypes = [ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_short_amount.argtypes = [ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    pointer = ctypes.POINTER
    lib.srtoybox_values_read.argtypes = [ctypes.c_void_p, pointer(toybox.GameAddresses), pointer(toybox.ValueLayout), ctypes.c_char_p,
                                         ctypes.c_int]
    lib.srtoybox_values_write.argtypes = [ctypes.c_void_p, pointer(toybox.GameAddresses), pointer(toybox.ValueLayout), ctypes.c_int,
                                          ctypes.c_double]
    lib.srtoybox_test_game.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.srtoybox_test_values.argtypes = [ctypes.c_void_p]
    lib.srtoybox_more_read.argtypes = [ctypes.c_void_p, pointer(toybox.GameAddresses), pointer(toybox.MoreLayout), ctypes.c_char_p,
                                       ctypes.c_int]
    lib.srtoybox_relation_read.argtypes = [ctypes.c_void_p, pointer(toybox.GameAddresses), pointer(toybox.MoreLayout), ctypes.c_int,
                                           ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_more_write.argtypes = [ctypes.c_void_p, pointer(toybox.GameAddresses), pointer(toybox.MoreLayout), ctypes.c_int,
                                        ctypes.c_int, ctypes.c_double, pointer(ctypes.c_int)]
    lib.srtoybox_test_more.argtypes = [ctypes.c_void_p]
    lib.srtoybox_more_off.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_test_init.argtypes = [ctypes.c_char_p, ctypes.c_ulonglong]
    lib.srtoybox_values_off.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_keeper_request.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_double]
    lib.srtoybox_keeper_more.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_double]
    lib.srtoybox_keeper_text.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_keeper_tick_at.argtypes = [ctypes.c_ulonglong]
    lib.srtoybox_keeper_keep.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_double]
    lib.srtoybox_keep_text.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    return lib


def text(call, *args, size: int = 4096) -> str | None:
    """글을 돌려주는 내보내기 함수를 부른다. 함수가 -1 을 주면 None."""
    buf = ctypes.create_string_buffer(size)
    n = call(*args, buf, size)
    return None if n < 0 else buf.raw[:n].decode("utf-8")


def value(lib, stock: bool, now: float, change: int, amount: float) -> tuple[int, float | None]:
    """(판정, 쓸 값). 쓰지 않으면 쓸 값은 None."""
    out = ctypes.c_double(-1.0)
    verdict = lib.srtoybox_value(int(stock), now, change, amount, ctypes.byref(out))
    return verdict, (out.value if verdict == WRITE else None)


def test_treasury_values(lib):
    assert value(lib, False, 14.43e9, ADD, 10e9) == (WRITE, 24.43e9)
    assert value(lib, False, 14.43e9, ADD, -100e9) == (WRITE, 14.43e9 - 100e9)    # 국고는 음수가 될 수 있다
    assert value(lib, False, 14.43e9, SET, 0.0) == (WRITE, 0.0)
    assert value(lib, False, 5.0, SET, 5.0) == (NOTHING, None)                    # 이미 그 값이다
    assert value(lib, False, 1e9, FLOOR, 5e9) == (WRITE, 5e9)
    assert value(lib, False, 9e9, FLOOR, 5e9) == (NOTHING, None)                  # 바닥 위다
    assert value(lib, False, 9.99e14, ADD, 1e14) == (WRITE, 1e15)                 # ±$1,000 T 안으로 자른다
    assert value(lib, False, -9.99e14, ADD, -1e14) == (WRITE, -1e15)
    assert value(lib, False, 5e15, FLOOR, 1e9) == (NOTHING, None)                 # 바닥은 값을 내리지 않는다 — 한도 밖의 값이어도


def test_a_value_already_beyond_the_limit_is_not_pulled_back(lib):
    """한도는 "여기까지만 민다"는 뜻이다. 게임이 한도 밖으로 만든 값을 더하기 · 바닥이 한도 쪽으로 끌어오면, 올리라는 요청이 값을 내린다."""
    assert value(lib, False, 1.5e15, FLOOR, 2e15) == (NOTHING, None)              # 바닥이 한도 위여도 내리지 않는다
    assert value(lib, False, 1.5e15, ADD, 1.0) == (NOTHING, None)                 # 더 밀지 않을 뿐이다
    assert value(lib, False, 1.5e15, ADD, -1e14) == (WRITE, 1.4e15)               # 한도 쪽으로는 청한 만큼만 간다
    assert value(lib, False, -1.5e15, ADD, -1.0) == (NOTHING, None)
    assert value(lib, False, -1.5e15, ADD, 1e14) == (WRITE, -1.4e15)
    assert value(lib, False, 1.5e15, SET, 2e15) == (WRITE, 1e15)                  # "이 값으로"만 한도 안으로 자른다
    assert value(lib, True, 2e9, FLOOR, 3e9) == (NOTHING, None)
    assert value(lib, True, 2e9, ADD, 1e6) == (NOTHING, None)
    assert value(lib, True, 2e9, ADD, -1e9) == (WRITE, 1e9)
    assert value(lib, True, 2e9, SET, 3e9) == (WRITE, 1e9)


def test_stock_values(lib):
    assert value(lib, True, 1000.0, ADD, 1e6) == (WRITE, 1001000.0)
    assert value(lib, True, 1000.0, ADD, -1e8) == (WRITE, 0.0)                    # 0 아래로 내려가지 않는다
    assert value(lib, True, 0.0, ADD, -5.0) == (NOTHING, None)
    assert value(lib, True, 1000.0, SET, -3.0) == (WRITE, 0.0)
    assert value(lib, True, 9.99e8, ADD, 1e8) == (WRITE, 1e9)                     # 10억 이하로 자른다
    assert value(lib, True, 800.0, FLOOR, 1000.0) == (WRITE, 1000.0)
    assert value(lib, True, 1200.0, FLOOR, 1000.0) == (NOTHING, None)
    assert value(lib, True, 16777216.0, ADD, 1.0) == (NOTHING, None)              # float 의 눈금보다 작은 변화는 쓸 것이 없다


@pytest.mark.parametrize("now, amount", [(NAN, 1.0), (INF, 1.0), (-INF, 1.0), (1.0, NAN), (1.0, INF)],
                         ids=["now-nan", "now-inf", "now-minus-inf", "amount-nan", "amount-inf"])
def test_values_refuse_what_is_not_a_number(lib, now, amount):
    """지금 값이 수가 아니면 구조가 바뀐 것일 수 있다 — "이 값으로"조차 쓰지 않는다."""
    for stock in (False, True):
        for change in (ADD, SET, FLOOR):
            assert value(lib, stock, now, change, amount) == (REFUSE, None)


def test_short_numbers_look_like_the_games(lib):
    """게임의 재무 패널과 같은 줄임 표기($ 14.43 B, $50.00 K)."""
    short = lambda v: text(lib.srtoybox_short_number, v)
    assert [short(v) for v in (0, 7, 999.4, 1000, 50000, 14.43e9, 1.1e6, 999999, 1e12, -1e12, -24.5e9, 0.3, -0.3)] == \
        ["0", "7", "999", "1.00 K", "50.00 K", "14.43 B", "1.10 M", "1.00 M", "1.00 T", "-1.00 T", "-24.50 B", "0", "0"]
    assert short(NAN) == "?" and short(INF) == "?"


def test_short_amounts_look_like_the_resource_bar(lib):
    """물자의 수량은 게임의 위쪽 자원 표시줄처럼: 100 아래는 소수 첫째 자리까지(1.8 M · 23.7 M), 그 위는 정수(129 K · 779 M)."""
    short = lambda v: text(lib.srtoybox_short_amount, v)
    assert [short(v) for v in (0, 7, 999.4, 1000, 2500, 129000, 1.8e6, 23.7e6, 779e6, 1e9, -5, 0.3, -0.3)] == \
        ["0", "7", "999", "1.0 K", "2.5 K", "129 K", "1.8 M", "23.7 M", "779 M", "1.0 B", "-5", "0", "0"]
    assert [short(v) for v in (99.94e3, 99.96e3, 999.4e3, 999.6e3, 5e15)] == ["99.9 K", "100 K", "999 K", "1.0 M", "5000 T"]
    assert short(NAN) == "?" and short(INF) == "?"


def germany() -> FakeGame:
    """독일(인덱스 176, 번호 1499)로 진행 중인 게임. 국고 $14.43 B. 물자 0 · 3 · 7 을 쓰고 재고가 1000 · 2500 · 0. 폴란드(141)의 국고는 $5 B.

    기술 수준은 독일 130 · 폴란드 128, 독일의 여론 세 칸은 0.5 · 0.25 · 0.75, 독일 → 폴란드의 관계는 0.25 · -0.5(전쟁 명분 0.75),
    폴란드 → 독일은 0.125 · 0.5(전쟁 명분 1)."""
    fake = FakeGame()
    fake.region(141, 1106, alive=3)
    fake.region(176, 1499)
    fake.play(176)
    fake.set_treasury(176, 14.43e9)
    fake.set_treasury(141, 5e9)
    fake.set_tech(176, 130.0)
    fake.set_tech(141, 128.0)
    fake.set_opinion(176, (0.5, 0.25, 0.75))
    fake.set_relation(176, 141, (0.25, -0.5, 0.75))
    fake.set_relation(141, 176, (0.125, 0.5, 1.0))
    for slot, amount in ((0, 1000.0), (3, 2500.0), (7, 0.0)):
        fake.use(slot)
        fake.set_stock(176, slot, amount)
    return fake


def read(lib, fake: FakeGame) -> dict[str, str]:
    out = ctypes.create_string_buffer(1024)
    assert lib.srtoybox_values_read(fake.base, ctypes.byref(fake.at), ctypes.byref(fake.layout), out, len(out)) >= 0
    return dict(pair.split("=", 1) for pair in out.value.decode().split())


def write(lib, fake: FakeGame, slot: int, amount: float) -> int:
    return lib.srtoybox_values_write(fake.base, ctypes.byref(fake.at), ctypes.byref(fake.layout), slot, amount)


def everything(fake: FakeGame) -> bytes:
    """가짜 게임의 메모리 전부(전역 · 세계 자료 · 지역 객체들) — 쓰기가 다른 곳을 건드리지 않았는지 볼 때 쓴다."""
    return bytes(fake.mem) + bytes(fake.world) + b"".join(fake.snapshot(index) for index in sorted(fake.objects))


def test_values_are_read_from_the_players_region(lib):
    assert read(lib, germany()) == {"ok": "1", "treasury": "14430000000", "used": "100100010000",
                                    "stock": "1000,0,0,2500,0,0,0,0,0,0,0,0"}


@pytest.mark.parametrize("flaw", ["menu", "no-world", "unreadable-world", "stock-nan"])
def test_values_are_not_read_when_the_game_does_not_add_up(lib, flaw):
    fake = germany()
    if flaw == "menu":
        fake.menu()
    elif flaw == "no-world":
        fake.poke(WORLD_POINTER, "<Q", 0)
    elif flaw == "unreadable-world":
        fake.poke(WORLD_POINTER, "<Q", 0x00007FFFFFFF0000)
    else:
        fake.set_stock(176, 9, float("nan"))                 # 쓰지 않는 물자의 칸이어도: 구조가 바뀐 것일 수 있다
    assert read(lib, fake)["ok"] == "0"


def test_a_write_changes_only_that_slot(lib):
    """쓰는 곳은 플레이어 지역 객체의 그 칸뿐이다. 둘레의 바이트 · 다른 지역 · 전역 · 치트 허용 비트는 그대로다."""
    fake = germany()
    before = everything(fake)
    assert write(lib, fake, TREASURY, 24.43e9) == DONE
    assert fake.treasury(176) == 24.43e9
    after = everything(fake)
    changed = [i for i in range(len(before)) if before[i] != after[i]]
    assert changed and set(changed) <= set(range(len(before) - OBJECT_SIZE + LAYOUT["treasury"],
                                                 len(before) - OBJECT_SIZE + LAYOUT["treasury"] + 8))
    assert fake.treasury(141) == 5e9 and fake.peek(OPTIONS, "<I") == 0
    assert write(lib, fake, 3, 9999.0) == DONE
    assert fake.stock(176, 3) == 9999.0
    last = everything(fake)
    slot = len(before) - OBJECT_SIZE + LAYOUT["stock_first"] + LAYOUT["stock_step"] * 3
    assert set(i for i in range(len(after)) if after[i] != last[i]) <= set(range(slot, slot + 4))
    assert write(lib, fake, TREASURY, -1e12) == DONE and fake.treasury(176) == -1e12      # 국고는 음수가 된다


@pytest.mark.parametrize("flaw", ["menu", "multiplayer", "inconsistent", "unreadable"])
def test_nothing_is_written_outside_a_game_it_can_trust(lib, flaw):
    fake = germany()
    if flaw == "menu":
        fake.menu()
    elif flaw == "multiplayer":
        fake.poke(MULTIPLAYER, "<B", 1)
    elif flaw == "inconsistent":
        struct.pack_into("<H", fake.objects[176], 4, 175)                 # 객체가 아는 자기 인덱스가 다르다
    else:
        fake.poke(POINTER, "<Q", 0x00007FFFFFFF0000)
        fake.poke(TABLE + 8 * 176, "<Q", 0x00007FFFFFFF0000)
    before = everything(fake)
    assert write(lib, fake, TREASURY, 1.0) == NOT_IN_GAME
    assert write(lib, fake, 3, 1.0) == NOT_IN_GAME
    assert everything(fake) == before


def test_a_value_or_a_slot_that_makes_no_sense_is_not_written(lib):
    fake = germany()
    before = everything(fake)
    for amount in (float("nan"), float("inf"), float("-inf")):
        assert write(lib, fake, TREASURY, amount) == BAD_VALUE
        assert write(lib, fake, 3, amount) == BAD_VALUE
    assert write(lib, fake, 3, -1.0) == BAD_VALUE                         # 재고는 음수가 되지 않는다
    assert write(lib, fake, 12, 1.0) == BAD_VALUE and write(lib, fake, -2, 1.0) == BAD_VALUE   # 없는 칸
    assert write(lib, fake, 5, 1.0) == NOT_USED                           # 이번 판에서 쓰지 않는 물자
    struct.pack_into("<f", fake.world, LAYOUT["used_first"] + LAYOUT["used_step"] * 3, float("nan"))
    assert write(lib, fake, 3, 1.0) == NOT_USED                           # "쓰는가"가 수가 아니다
    fake.poke(WORLD_POINTER, "<Q", 0)
    assert write(lib, fake, 0, 1.0) == NOT_USED                           # 세계 자료를 읽을 수 없다
    objects = len(fake.mem) + len(fake.world)
    assert everything(fake)[objects:] == before[objects:]                 # 지역 객체들은 그대로다(전역과 세계 자료는 방금 테스트가 고쳤다)


@pytest.mark.parametrize("protection", [0x02, 0x20], ids=["read-only", "execute-read"])
def test_a_slot_on_a_page_that_is_not_read_write_is_not_written(lib, protection):
    """낡은 포인터가 읽을 수는 있지만 쓰면 안 되는 곳을 가리킨다. WriteProcessMemory 는 실행 쪽이면 보호를 풀고 쓴다 —
    그 전에 가려야 한다. 죽지 않고 실패로 돌아온다."""
    fake = germany()
    fake.lock(176, protection)
    fake.play(176)
    assert read(lib, fake)["ok"] == "1"                                   # 읽기는 된다
    before = fake.snapshot(176)
    assert write(lib, fake, TREASURY, 1.0) == FAILED
    assert write(lib, fake, 3, 1.0) == FAILED
    assert fake.snapshot(176) == before


def more(lib, fake: FakeGame, layout: toybox.MoreLayout | None = None) -> dict[str, str]:
    out = ctypes.create_string_buffer(1024)
    assert lib.srtoybox_more_read(fake.base, ctypes.byref(fake.at), ctypes.byref(layout or fake.more), out, len(out)) >= 0
    return dict(pair.split("=", 1) for pair in out.value.decode().split())


def relation(lib, fake: FakeGame, number: int) -> dict[str, str]:
    out = ctypes.create_string_buffer(1024)
    assert lib.srtoybox_relation_read(fake.base, ctypes.byref(fake.at), ctypes.byref(fake.more), number, out, len(out)) >= 0
    return dict(pair.split("=", 1) for pair in out.value.decode().split())


def write_more(lib, fake: FakeGame, what: int, number: int = 0, value: float = 0.0,
               layout: toybox.MoreLayout | None = None) -> tuple[int, int]:
    """(Wrote, 쓴 칸의 수)."""
    done = ctypes.c_int(-1)
    wrote = lib.srtoybox_more_write(fake.base, ctypes.byref(fake.at), ctypes.byref(layout or fake.more), what, number, value,
                                    ctypes.byref(done))
    return wrote, done.value


def where(fake: FakeGame, index: int, offset: int, size: int = 4) -> set[int]:
    """everything(fake) 안에서 그 지역 객체의 그 자리가 차지하는 바이트들."""
    start = len(fake.mem) + len(fake.world) + OBJECT_SIZE * sorted(fake.objects).index(index) + offset
    return set(range(start, start + size))


def changed(before: bytes, after: bytes) -> set[int]:
    return {i for i in range(len(before)) if before[i] != after[i]}


def with_denmark() -> FakeGame:
    """germany() 에 덴마크(인덱스 150, 번호 1201)를 더한 것. 독일 ↔ 덴마크, 폴란드 ↔ 덴마크의 관계도 0 이 아니다."""
    fake = germany()
    fake.region(150, 1201, alive=3)
    for a, b in ((176, 150), (150, 176), (141, 150), (150, 141)):
        fake.set_relation(a, b, (0.5, 0.5, 0.5))
    return fake


def test_more_is_read_from_the_players_region(lib):
    fake = germany()
    assert more(lib, fake) == {"ok": "1", "tech": "130", "opinion": "0.5,0.25,0.75", "people": "0,0", "approval": "0"}
    assert relation(lib, fake, 1106) == {"ok": "1", "mine": "0.25,-0.5,0.75", "theirs": "0.125,0.5,1"}
    assert relation(lib, fake, 1499)["ok"] == "0" and relation(lib, fake, 9999)["ok"] == "0"      # 자기 자신, 없는 번호
    fake.menu()
    assert more(lib, fake)["ok"] == "0" and relation(lib, fake, 1106)["ok"] == "0"


def test_a_tech_write_changes_only_that_cell(lib):
    """쓰는 곳은 플레이어 지역 객체의 그 칸뿐이다. 둘레의 바이트 · 다른 지역 · 전역 · 치트 허용 비트는 그대로다."""
    fake = germany()
    before = everything(fake)
    assert write_more(lib, fake, TECH_TO, value=131.0) == (DONE, 1)
    assert fake.tech(176) == 131.0 and fake.tech(141) == 128.0
    assert changed(before, everything(fake)) <= where(fake, 176, MORE["tech"])
    assert fake.peek(OPTIONS, "<I") == 0


def test_an_opinion_write_changes_only_the_three_cells(lib):
    fake = germany()
    before = everything(fake)
    assert write_more(lib, fake, OPINION_BEST) == (DONE, 3)
    assert fake.opinion(176) == [1.0, 1.0, 1.0] and fake.opinion(141) == [0.0, 0.0, 0.0]
    cells = set().union(*(where(fake, 176, MORE[name]) for name in ("opinion0", "opinion1", "opinion2")))
    assert changed(before, everything(fake)) <= cells


def test_a_relation_write_changes_only_the_six_cells(lib):
    """관계는 치트처럼 양쪽에 쓴다: 플레이어 객체의 표 셋에서 그 나라의 칸, 그 나라 객체의 표 셋에서 플레이어의 칸.
    같은 표의 이웃 칸, 다른 나라와의 관계, 다른 나라의 객체는 그대로다."""
    fake = with_denmark()
    before = everything(fake)
    assert write_more(lib, fake, RELATION_TO, 1106, 1.0) == (DONE, 6)
    assert fake.relation(176, 141) == [1.0, 1.0, 0.0] and fake.relation(141, 176) == [1.0, 1.0, 0.0]   # 관계 둘은 최고, 전쟁 명분은 0
    six = set().union(*(where(fake, 176, MORE[name] + 4 * 141) | where(fake, 141, MORE[name] + 4 * 176) for name in RELATION_TABLES))
    assert changed(before, everything(fake)) <= six
    assert fake.relation(176, 150) == [0.5, 0.5, 0.5] and fake.relation(150, 176) == [0.5, 0.5, 0.5]
    assert write_more(lib, fake, RELATION_TO, 1106, 0.0) == (DONE, 6)                                  # 중립: 여섯 칸 모두 0
    assert fake.relation(176, 141) == [0.0, 0.0, 0.0] and fake.relation(141, 176) == [0.0, 0.0, 0.0]
    assert changed(before, everything(fake)) <= six and fake.peek(OPTIONS, "<I") == 0


@pytest.mark.parametrize("flaw", ["menu", "multiplayer", "inconsistent", "unreadable"])
def test_more_is_not_written_outside_a_game_it_can_trust(lib, flaw):
    fake = germany()
    if flaw == "menu":
        fake.menu()
    elif flaw == "multiplayer":
        fake.poke(MULTIPLAYER, "<B", 1)
    elif flaw == "inconsistent":
        struct.pack_into("<H", fake.objects[176], 4, 175)                 # 객체가 아는 자기 인덱스가 다르다
    else:
        fake.poke(POINTER, "<Q", 0x00007FFFFFFF0000)
        fake.poke(TABLE + 8 * 176, "<Q", 0x00007FFFFFFF0000)
    before = everything(fake)
    assert write_more(lib, fake, TECH_TO, value=131.0) == (NOT_IN_GAME, 0)
    assert write_more(lib, fake, OPINION_BEST) == (NOT_IN_GAME, 0)
    assert write_more(lib, fake, RELATION_TO, 1106, 1.0) == (NOT_IN_GAME, 0)
    assert everything(fake) == before


@pytest.mark.parametrize("flaw", ["no-such-number", "self", "not-in-play", "inconsistent", "unreadable"])
def test_a_relation_is_written_only_with_a_country_that_is_in_this_game(lib, flaw):
    """고른 나라가 쓰기 직전에 없어졌거나(다른 판, 병합됨) 플레이어 자신이면 쓰지 않는다 — 엉뚱한 객체를 건드리지 않는다."""
    fake = with_denmark()
    number = 1106
    if flaw == "no-such-number":
        number = 9999
    elif flaw == "self":
        number = 1499
    elif flaw == "not-in-play":
        struct.pack_into("<I", fake.objects[141], 0, 5)                   # 사람도 AI 도 맡지 않은 지역이 됐다
    elif flaw == "inconsistent":
        struct.pack_into("<H", fake.objects[141], 4, 140)                 # 객체가 아는 자기 인덱스가 표에서의 자리와 다르다
    else:
        fake.poke(TABLE + 8 * 141, "<Q", 0x00007FFFFFFF0000)
    before = everything(fake)
    assert write_more(lib, fake, RELATION_TO, number, 1.0) == (NO_TARGET, 0)
    assert everything(fake) == before


def test_more_values_that_make_no_sense_are_not_written(lib):
    fake = germany()
    before = everything(fake)
    for value in (NAN, INF, -INF, -1.0):
        assert write_more(lib, fake, TECH_TO, value=value) == (BAD_VALUE, 0)
    for level in (NAN, INF, 1.5, -1.5):
        assert write_more(lib, fake, RELATION_TO, 1106, level) == (BAD_VALUE, 0)
    assert everything(fake) == before
    assert write_more(lib, fake, RELATION_TO, 1106, -1.0) == (DONE, 6)        # 범위의 끝은 된다(최저)


def test_a_group_whose_place_is_not_known_is_not_written(lib):
    """묶음마다 따로다: 못 찾은 묶음(자리 0)에는 쓰지 않고, 찾은 묶음은 그대로 쓴다."""
    fake = germany()
    before = everything(fake)
    assert write_more(lib, fake, TECH_TO, value=131.0, layout=toybox.MoreLayout(**{**MORE, "tech": 0})) == (OFF, 0)
    assert write_more(lib, fake, OPINION_BEST, layout=toybox.MoreLayout(**{**MORE, "opinion1": 0})) == (OFF, 0)
    assert write_more(lib, fake, RELATION_TO, 1106, 1.0, layout=toybox.MoreLayout(**{**MORE, "casus": 0})) == (OFF, 0)
    assert everything(fake) == before
    no_tech = toybox.MoreLayout(**{**MORE, "tech": 0})
    assert more(lib, fake, no_tech) == {"ok": "1", "tech": "0", "opinion": "0.5,0.25,0.75", "people": "0,0", "approval": "0"}
    assert write_more(lib, fake, OPINION_BEST, layout=no_tech) == (DONE, 3)


@pytest.mark.parametrize("protection", [0x02, 0x20], ids=["read-only", "execute-read"])
def test_more_on_a_page_that_is_not_read_write_is_not_written(lib, protection):
    """쓸 수 없는 쪽이면 실패로 돌아온다. 여러 칸을 쓰는 것은 먼저 모든 칸을 보고 — 한 칸도 쓰지 않는다."""
    fake = germany()
    fake.lock(176, protection)
    fake.play(176)
    theirs = fake.snapshot(141)
    assert write_more(lib, fake, TECH_TO, value=131.0) == (FAILED, 0)
    assert write_more(lib, fake, OPINION_BEST) == (FAILED, 0)
    assert write_more(lib, fake, RELATION_TO, 1106, 1.0) == (FAILED, 0)
    assert fake.tech(176) == 130.0 and fake.snapshot(141) == theirs          # 폴란드 쪽 칸도 쓰지 않았다
    fake = germany()
    fake.lock(141, protection)                                                # 이번에는 고른 나라의 객체가 쓸 수 없는 쪽에 있다
    mine = fake.snapshot(176)
    assert write_more(lib, fake, RELATION_TO, 1106, 1.0) == (FAILED, 0)
    assert fake.snapshot(176) == mine                                         # 플레이어 쪽 칸도 쓰지 않았다


def test_more_follows_the_country_being_played(lib):
    """요구 2: 쓰는 것은 지금 플레이하는 나라의 값이다. 나라를 바꾸면 새 플레이어의 칸에 쓴다."""
    fake = with_denmark()
    fake.play(141)                                                            # 폴란드로 플레이한다
    assert write_more(lib, fake, TECH_TO, value=129.0) == (DONE, 1)
    assert write_more(lib, fake, OPINION_BEST) == (DONE, 3)
    assert fake.tech(141) == 129.0 and fake.tech(176) == 130.0
    assert fake.opinion(141) == [1.0, 1.0, 1.0] and fake.opinion(176) == [0.5, 0.25, 0.75]
    assert write_more(lib, fake, RELATION_TO, 1201, 1.0) == (DONE, 6)         # 폴란드 ↔ 덴마크
    assert fake.relation(141, 150) == [1.0, 1.0, 0.0] and fake.relation(150, 141) == [1.0, 1.0, 0.0]
    assert fake.relation(176, 150) == [0.5, 0.5, 0.5] and fake.relation(176, 141) == [0.25, -0.5, 0.75]   # 독일의 것은 그대로다


@pytest.fixture(scope="module")
def all_sigs(lib):
    """DLL 에 든 서명 표 둘: ([상태 묶음 21개], [값 묶음 12개]) — 저마다 (찾을 것, 서명 글)."""
    return ([(row.name, row.text) for row in toybox.state_of(lib, b"")[2]],
            [(row.name, row.text) for row in toybox.values_of(lib, b"")[2]])


def init(lib, image: bytes, tmp_path, monkeypatch) -> tuple[int, str, str]:
    """게임이 뜰 때의 찾기(game_init_from)를 그 이미지에 돌린다: (아는 것의 비트, 값을 쓸 수 없는 까닭, 로그)."""
    monkeypatch.setenv("SRTOYBOX_HOME", str(tmp_path))
    try:
        lib.srtoybox_test_init(image, len(image))
        flags, off = lib.srtoybox_game_flags(), text(lib.srtoybox_values_off)
    finally:
        lib.srtoybox_test_game(None, None)              # 이미지를 놓기 전에 이 프로세스의 "게임"을 비운다
    log = tmp_path / "toybox.log"
    return flags, off, log.read_text(encoding="utf-8") if log.is_file() else ""


@pytest.fixture(scope="module")
def more_sigs(lib):
    """DLL 에 든 "더 쓰는 값"의 서명 21개 — (찾을 것, 서명 글)."""
    return [(row.name, row.text) for row in toybox.more_of(lib, b"")[2]]


def init_more(lib, image: bytes, tmp_path, monkeypatch) -> tuple[int, list[str], str]:
    """init 과 같되 더 쓰는 값을 본다: (아는 것의 비트, 묶음마다(지식 · 여론 · 관계) 쓸 수 없는 까닭, 로그)."""
    monkeypatch.setenv("SRTOYBOX_HOME", str(tmp_path))
    try:
        lib.srtoybox_test_init(image, len(image))
        flags = lib.srtoybox_game_flags()
        off = [text(lib.srtoybox_more_off, group) for group in (MORE_TECH, MORE_OPINION, MORE_RELATIONS)]
    finally:
        lib.srtoybox_test_game(None, None)
    log = tmp_path / "toybox.log"
    return flags, off, log.read_text(encoding="utf-8") if log.is_file() else ""


FOUND_MORE = ("기술 수준 +0x3120 · 세계 시장 여론 +0x3004 +0x3008 +0x3020 · 관계 +0x4000 +0x5000 전쟁 명분 +0x6000"
              " · 인구 +0x3200 풀 +0x3210 · 지지율 +0x31F0")


def test_startup_finds_more_without_any_cheat(lib, all_sigs, more_sigs, tmp_path, monkeypatch):
    state, values = all_sigs
    flags, off, log = init_more(lib, toybox_fake_exe.sig_image(state + values + more_sigs), tmp_path, monkeypatch)
    assert flags == READS | WRITES | CAN_TECH | CAN_OPINION | CAN_RELATIONS | CAN_PEOPLE | CAN_APPROVAL and off == ["", "", ""]
    assert f"값을 더 씁니다 ({FOUND_MORE})" in log
    unwritable = [line.split(" ", 2)[2] for line in log.splitlines() if "쓸 수 없습니다" in line]
    assert unwritable == ["식민지화를 쓸 수 없습니다 (식민지화 함수: 서명 3개 가운데 0개)",     # 이 이미지에 부르는 함수 · 연구의 서명은 넣지 않았다
                          "전쟁 붙이기를 쓸 수 없습니다 (전쟁 함수: 서명 3개 가운데 0개)",
                          "이 나라로 플레이를 쓸 수 없습니다 (플레이 지역을 바꾸는 함수: 서명 3개 가운데 0개)",
                          "지도자 제거를 쓸 수 없습니다 (새 지도자를 세우는 함수: 서명 3개 가운데 0개)",
                          "동맹 맺기를 쓸 수 없습니다 (조약 함수: 서명 3개 가운데 0개)",
                          "병합을 쓸 수 없습니다 (칸의 주인을 바꾸는 함수: 서명 3개 가운데 0개)",
                          "부대 생성을 쓸 수 없습니다 (부대를 만드는 함수: 서명 3개 가운데 0개)",
                          "보급 채우기를 쓸 수 없습니다 (고른 부대의 목록: 서명 3개 가운데 0개)",
                          "즉시 승리를 쓸 수 없습니다 (판이 끝난 꼴: 서명 3개 가운데 0개)",
                          "내 부대 전부에 쓰기를 쓸 수 없습니다 (세계의 부대 목록: 서명 3개 가운데 0개)",
                          "GUI 숨기기를 쓸 수 없습니다 (화면의 깃발: 서명 3개 가운데 0개)",
                          "최강 부대 묶음을 쓸 수 없습니다 (배치 칸을 찾는 함수: 서명 3개 가운데 0개)",
                          "배율의 일부를 쓸 수 없습니다 (곱 보정 표: 서명 3개 가운데 0개 외 14개)",   # 군부 지지율 · 위 나라 칸이 더해졌다(2026-10-26)
                          "연구를 쓸 수 없습니다 (기술 표: 서명 3개 가운데 0개)"]
    assert "맞지 않은 서명" not in log


def test_startup_drops_only_the_group_it_cannot_find(lib, all_sigs, more_sigs, tmp_path, monkeypatch):
    """한 묶음의 서명이 깨지면 그 단추만 꺼진다. 까닭이 로그와 창에 같은 글로 남는다. 돈 · 물자와 다른 묶음은 그대로다."""
    state, values = all_sigs
    first = len(state) + len(values) + 3 * 4                  # 관계 표 1 의 서명 셋 가운데 앞의 둘
    image = toybox_fake_exe.sig_image(state + values + more_sigs, broken={first, first + 1})
    flags, off, log = init_more(lib, image, tmp_path, monkeypatch)
    assert flags == READS | WRITES | CAN_TECH | CAN_OPINION | CAN_PEOPLE | CAN_APPROVAL
    assert off == ["", "", "이 게임 판에서는 쓸 수 없습니다 (관계 표 1: 서명 3개 가운데 1개)"]
    assert "값을 더 씁니다 (기술 수준 +0x3120 · 세계 시장 여론 +0x3004 +0x3008 +0x3020 · 인구 +0x3200 풀 +0x3210 · 지지율 +0x31F0)" in log
    assert "관계를 쓸 수 없습니다 (관계 표 1: 서명 3개 가운데 1개)" in log
    assert "맞지 않은 서명" not in log                         # 못 찾은 묶음의 서명을 줄줄이 적지 않는다


def test_startup_without_any_of_more_keeps_money_and_stock(lib, all_sigs, tmp_path, monkeypatch):
    state, values = all_sigs
    flags, off, log = init_more(lib, toybox_fake_exe.sig_image(state + values), tmp_path, monkeypatch)
    assert flags == READS | WRITES and all(off)
    assert "값을 씁니다 (국고 +0x1230" in log and "값을 더 씁니다" not in log
    for line in ("기술 수준을 쓸 수 없습니다 (기술 수준 칸: 서명 3개 가운데 0개)", "세계 시장 여론을 쓸 수 없습니다 (여론 칸 1: 서명 3개 가운데 0개)",
                 "관계를 쓸 수 없습니다 (관계 표 1: 서명 3개 가운데 0개)"):
        assert line in log


def test_startup_finds_more_when_money_and_stock_are_not_found(lib, all_sigs, more_sigs, tmp_path, monkeypatch):
    """묶음은 서로 기대지 않는다: 값 묶음(돈 · 물자)을 못 찾아도 더 쓰는 값은 찾아서 쓴다."""
    state, _values = all_sigs
    flags, off, log = init_more(lib, toybox_fake_exe.sig_image(state + more_sigs), tmp_path, monkeypatch)
    assert flags == READS | CAN_TECH | CAN_OPINION | CAN_RELATIONS | CAN_PEOPLE | CAN_APPROVAL and off == ["", "", ""]
    assert "값을 쓸 수 없습니다 (세계 자료 포인터" in log and f"값을 더 씁니다 ({FOUND_MORE})" in log


def test_startup_names_an_unmatched_signature_of_a_group_it_found(lib, all_sigs, more_sigs, tmp_path, monkeypatch):
    state, values = all_sigs
    image = toybox_fake_exe.sig_image(state + values + more_sigs, broken={len(state) + len(values) + 2})
    flags, _off, log = init_more(lib, image, tmp_path, monkeypatch)
    assert flags & CAN_TECH and "맞지 않은 서명: tech #3 (안 맞음)" in log


def test_startup_drops_a_group_that_sits_on_the_treasury(lib, all_sigs, more_sigs, tmp_path, monkeypatch):
    """새 묶음의 칸이 국고 칸과 겹치면 그 묶음을 버린다(값 묶음은 그대로 쓴다)."""
    state, values = all_sigs
    image = toybox_fake_exe.sig_image(state + values + more_sigs, targets={"tech": 0x1234})
    flags, off, log = init_more(lib, image, tmp_path, monkeypatch)
    assert flags == READS | WRITES | CAN_OPINION | CAN_RELATIONS | CAN_PEOPLE | CAN_APPROVAL
    assert off[0] == "이 게임 판에서는 쓸 수 없습니다 (기술 수준 칸: 찾은 자리가 국고 칸이나 재고 칸과 겹칩니다)"
    assert "기술 수준을 쓸 수 없습니다 (기술 수준 칸: 찾은 자리가 국고 칸이나 재고 칸과 겹칩니다)" in log


def test_startup_finds_the_state_and_the_values_without_any_cheat(lib, all_sigs, tmp_path, monkeypatch):
    """치트가 없는 빌드: 상태를 읽고 값을 쓴다. 치트의 문자열 · 명령 처리 함수는 찾지도 않는다."""
    state, values = all_sigs
    flags, off, log = init(lib, toybox_fake_exe.sig_image(state + values), tmp_path, monkeypatch)
    assert flags == READS | WRITES and off == ""
    assert "게임 상태를 읽습니다 (서명 21개 가운데 21개" in log
    assert "값을 씁니다 (국고 +0x1230, 재고 +0x2000 간격 0x20 × 12)" in log
    assert "명령 처리 함수" not in log and "내장 치트" not in log and "맞지 않은 서명" not in log


def test_startup_without_the_values_keeps_everything_else(lib, all_sigs, tmp_path, monkeypatch):
    """값 묶음만 못 찾으면 돈 탭만 꺼진다. 까닭이 로그와 창에 같은 글로 남는다."""
    state, _values = all_sigs
    flags, off, log = init(lib, toybox_fake_exe.build(state), tmp_path, monkeypatch)
    assert flags == READS
    assert off == "이 게임 판에서는 쓸 수 없습니다 (세계 자료 포인터: 서명 3개 가운데 0개)"
    assert "값을 쓸 수 없습니다 (세계 자료 포인터: 서명 3개 가운데 0개)" in log
    assert "명령 처리 함수" not in log and "내장 치트" not in log      # 치트의 닻이 든 이미지에서도 옛 찾기를 돌리지 않는다


def test_startup_names_the_signatures_that_did_not_match(lib, all_sigs, tmp_path, monkeypatch):
    """셋 가운데 둘로 찾았을 때도, 맞지 않은 서명이 무엇인지 로그에 남는다 — 다음 업데이트에서 깨질 것을 미리 안다."""
    state, values = all_sigs
    flags, _off, log = init(lib, toybox_fake_exe.sig_image(state + values, broken={0, 22}), tmp_path, monkeypatch)
    assert flags == READS | WRITES
    assert "게임 상태를 읽습니다 (서명 21개 가운데 20개" in log
    assert "맞지 않은 서명: multiplayer #1 (안 맞음)" in log and "맞지 않은 서명: world_pointer #2 (안 맞음)" in log


def test_startup_drops_the_values_when_the_two_searches_clash(lib, all_sigs, tmp_path, monkeypatch):
    """두 묶음을 저마다 찾았어도 세계 자료 포인터가 상태 전역과 겹치면 값은 쓰지 않는다. 상태 읽기는 그대로다."""
    state, values = all_sigs
    image = toybox_fake_exe.sig_image(state + values, targets={"world_pointer": toybox_fake_exe.STATE["player_pointer"]})
    flags, off, log = init(lib, image, tmp_path, monkeypatch)
    clash = "세계 자료 포인터: 찾은 주소가 플레이어 포인터 의 자리와 겹칩니다"
    assert flags == READS and off == f"이 게임 판에서는 쓸 수 없습니다 ({clash})"
    assert f"값을 쓸 수 없습니다 ({clash})" in log and "게임 상태를 읽습니다 (서명 21개 가운데 21개" in log


def test_startup_with_everything(lib, all_sigs, tmp_path, monkeypatch):
    state, values = all_sigs
    flags, off, _log = init(lib, toybox_fake_exe.build(state + values), tmp_path, monkeypatch)
    assert flags == READS | WRITES and off == ""


@pytest.mark.parametrize("image", [b"", bytes(0x1000)], ids=["empty", "zeros"])
def test_startup_in_something_that_is_not_the_game(lib, image, tmp_path, monkeypatch):
    flags, off, log = init(lib, image, tmp_path, monkeypatch)
    assert flags == 0 and off == UNREAD
    assert "게임 상태를 읽을 수 없습니다" in log and "맞지 않은 서명" not in log     # 스물한 줄을 쏟아 내지 않는다


@pytest.fixture
def game(lib, tmp_path, monkeypatch):
    """이 프로세스의 "게임"을 독일로 진행 중인 가짜 게임으로 바꾼다. 로그는 tmp_path 에 남는다."""
    monkeypatch.setenv("SRTOYBOX_HOME", str(tmp_path))
    fake = germany()
    lib.srtoybox_test_game(fake.base, ctypes.byref(fake.at))
    lib.srtoybox_test_values(ctypes.byref(fake.layout))
    lib.srtoybox_test_more(ctypes.byref(fake.more))
    lib.srtoybox_keeper_reset()
    yield fake
    lib.srtoybox_keeper_reset()
    lib.srtoybox_test_game(None, None)


def ask(lib, slot: int, change: int, amount: float) -> bool:
    return lib.srtoybox_keeper_request(slot, change, amount) == 1


def told(lib) -> tuple[str, str]:
    """(마지막으로 쓴 것, 알림)."""
    last, notice = text(lib.srtoybox_keeper_text).split("\t")
    return last, notice


def test_requests_are_written_in_order_on_the_next_tick(lib, game, tmp_path):
    assert ask(lib, TREASURY, SET, 100.0) and ask(lib, TREASURY, ADD, 50.0) and ask(lib, TREASURY, ADD, -200.0)
    assert game.treasury(176) == 14.43e9                       # 누른 것만으로는 쓰지 않는다 — 창 스레드의 틱에서 쓴다
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == -50.0
    assert told(lib) == ("국고 150 -> -50", "")
    log = (tmp_path / "toybox.log").read_text(encoding="utf-8")
    assert [line.split(" ", 2)[2] for line in log.splitlines()] == ["값 쓰기: 국고 14.43 B -> 100", "값 쓰기: 국고 100 -> 150",
                                                                    "값 쓰기: 국고 150 -> -50"]
    assert game.treasury(141) == 5e9 and game.peek(OPTIONS, "<I") == 0     # 다른 나라와 치트 허용 비트는 그대로다


def test_stock_requests_reach_only_products_in_use(lib, game):
    assert ask(lib, 3, ADD, 1e6) and ask(lib, 0, ADD, -1e8) and ask(lib, 5, ADD, 1e6)
    lib.srtoybox_keeper_tick()
    assert game.stock(176, 3) == 1002500.0 and game.stock(176, 0) == 0.0    # 0 아래로 내려가지 않는다
    assert game.stock(176, 5) == 0.0                                        # 쓰지 않는 물자의 칸에는 쓰지 않는다
    assert told(lib) == ("농산물 1.0 K -> 0", "이번 판에서 쓰지 않는 물자입니다.")      # 물자는 이름으로, 자원 표시줄의 표기로


def test_a_request_for_all_products_reaches_every_product_in_use(lib, game, tmp_path):
    """"모든 물자" 줄의 요청은 쓸 때 이번 판에서 쓰는 물자마다의 요청으로 풀린다. 쓰지 않는 칸 · 다른 나라는 그대로다."""
    game.set_stock(141, 3, 777.0)
    assert ask(lib, ALL_STOCK, ADD, 1e6)
    lib.srtoybox_keeper_tick()
    assert [game.stock(176, slot) for slot in range(12)] == [1001000.0, 0, 0, 1002500.0, 0, 0, 0, 1000000.0, 0, 0, 0, 0]
    assert told(lib) == ("모든 물자 3개 +1.0 M", "")                         # 무엇을 했는지도(2026-10-19)
    assert ask(lib, ALL_STOCK, SET, 0.0)
    lib.srtoybox_keeper_tick()
    assert [game.stock(176, slot) for slot in (0, 3, 7)] == [0.0, 0.0, 0.0] and told(lib) == ("모든 물자 3개 -> 0", "")
    assert ask(lib, ALL_STOCK, ADD, -1e8) and ask(lib, 7, ADD, 5.0)
    lib.srtoybox_keeper_tick()
    assert told(lib) == ("전력 0 -> 5", "")                    # 바꿀 것이 없던 "모든 물자"는 마지막으로 쓴 것을 덮지 않는다
    assert game.stock(141, 3) == 777.0 and game.treasury(176) == 14.43e9
    log = (tmp_path / "toybox.log").read_text(encoding="utf-8")
    assert [line.split(" ", 2)[2] for line in log.splitlines()][:3] == ["값 쓰기: 농산물 1.0 K -> 1.0 M", "값 쓰기: 석유 2.5 K -> 1.0 M",
                                                                        "값 쓰기: 전력 0 -> 1.0 M"]


def test_requests_are_dropped_outside_a_game(lib, game):
    """누른 뒤 쓰기 전에 게임에서 나가거나 멀티플레이가 되면 쓰지 않고 버린다. 돌아오면 다시 된다."""
    assert ask(lib, TREASURY, ADD, 1e9)
    game.menu()
    lib.srtoybox_keeper_tick()
    game.play(176)
    lib.srtoybox_keeper_tick()                                 # 버린 요청이 돌아온 뒤에 쓰이지 않는다
    assert game.treasury(176) == 14.43e9 and told(lib) == ("", LEFT_GAME)
    assert ask(lib, TREASURY, ADD, 1e9)
    game.poke(MULTIPLAYER, "<B", 1)
    lib.srtoybox_keeper_tick()
    game.poke(MULTIPLAYER, "<B", 0)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 14.43e9
    assert ask(lib, TREASURY, ADD, 1e9)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 15.43e9 and told(lib) == ("국고 14.43 B -> 15.43 B", "")     # 알림은 다음에 쓸 때 지워진다


def test_a_value_that_is_not_a_number_is_left_alone(lib, game):
    game.set_treasury(176, float("nan"))
    assert ask(lib, TREASURY, SET, 5.0)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) != game.treasury(176)            # 아직 NaN 이다
    assert told(lib) == ("", "지금 값이 수가 아니어서 쓰지 않았습니다.")
    game.set_treasury(176, 1.0)
    game.set_stock(176, 9, float("inf"))                       # 재고 칸 하나가 수가 아니면 값을 통째로 믿지 않는다
    assert ask(lib, TREASURY, SET, 5.0)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 1.0 and told(lib) == ("", "게임의 값을 읽을 수 없어 쓰지 않았습니다.")


def test_the_queue_takes_thirty_two(lib, game):
    assert all(ask(lib, TREASURY, ADD, 1.0) for _ in range(32))
    assert not ask(lib, TREASURY, ADD, 1.0)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 14.43e9 + 32
    assert ask(lib, TREASURY, ADD, 1.0)


def test_a_failed_write_turns_value_writing_off_for_this_run(lib, game, tmp_path):
    game.lock(176)                                             # 플레이어 객체가 읽기 전용 쪽에 있다
    game.play(176)
    assert ask(lib, TREASURY, ADD, 1e9) and ask(lib, TREASURY, ADD, 1e9)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 14.43e9
    assert text(lib.srtoybox_values_off) == FAILED_OFF
    assert not ask(lib, TREASURY, ADD, 1e9)                    # 그 뒤로는 받지 않는다
    log = (tmp_path / "toybox.log").read_text(encoding="utf-8")
    assert log.count("값 쓰기 실패 (국고) — 값 쓰기를 끕니다") == 1 and "값 쓰기: " not in log


def test_a_failed_stock_write_names_the_product(lib, game, tmp_path):
    game.lock(176)
    game.play(176)
    assert ask(lib, ALL_STOCK, ADD, 1e6)
    lib.srtoybox_keeper_tick()
    assert text(lib.srtoybox_values_off) == FAILED_OFF and told(lib)[0] == ""
    log = (tmp_path / "toybox.log").read_text(encoding="utf-8")
    assert log.count("값 쓰기 실패 (농산물) — 값 쓰기를 끕니다") == 1 and "값 쓰기: " not in log   # 첫 칸에서 멈춘다


def keep(lib, slot: int, floor: float, on: bool = True) -> None:
    assert lib.srtoybox_keeper_keep(slot, int(on), floor) == 0


def kept_lines(tmp_path) -> list[str]:
    """로그의 "유지: …" 줄들(때를 뗀 것)."""
    log = tmp_path / "toybox.log"
    lines = [line.split(" ", 2)[2] for line in log.read_text(encoding="utf-8").splitlines()] if log.is_file() else []
    return [line for line in lines if line.startswith("유지: ")]


def test_keep_raises_what_falls_below_its_floor(lib, game, tmp_path):
    """최소 유지: 켠 다음 틱에 바로 올리고, 그 뒤로는 0.5초마다 본다. 바닥 위의 값은 건드리지 않는다. 창의 "마지막으로 쓴 값"은 그대로다."""
    keep(lib, TREASURY, 20e9)
    keep(lib, 3, 5000.0)
    keep(lib, 0, 500.0)                                        # 농산물은 1000 — 바닥 위다
    assert game.treasury(176) == 14.43e9                       # 켠 것만으로는 쓰지 않는다 — 창 스레드의 틱에서 쓴다
    lib.srtoybox_keeper_tick_at(100_000)
    assert game.treasury(176) == 20e9 and game.stock(176, 3) == 5000.0 and game.stock(176, 0) == 1000.0
    assert told(lib) == ("", "")
    game.set_treasury(176, 1e9)                                # 게임이 그 사이에 썼다
    game.set_stock(176, 3, 100.0)
    lib.srtoybox_keeper_tick_at(100_010)
    lib.srtoybox_keeper_tick_at(100_499)
    assert game.treasury(176) == 1e9 and game.stock(176, 3) == 100.0     # 아직 0.5초가 안 됐다
    lib.srtoybox_keeper_tick_at(100_500)
    assert game.treasury(176) == 20e9 and game.stock(176, 3) == 5000.0
    assert game.treasury(141) == 5e9 and game.peek(OPTIONS, "<I") == 0   # 다른 나라와 치트 허용 비트는 그대로다
    assert kept_lines(tmp_path) == ["유지: 국고 14.43 B -> 20.00 B", "유지: 석유 2.5 K -> 5.0 K"]   # 항목마다 첫 번째만 적는다


def test_keep_leaves_products_this_game_does_not_use(lib, game):
    keep(lib, 5, 1000.0)                                       # 이번 판에서 쓰지 않는 물자(금속 광석)
    keep(lib, 7, 1000.0)
    lib.srtoybox_keeper_tick_at(1000)
    assert game.stock(176, 5) == 0.0 and game.stock(176, 7) == 1000.0
    assert lib.srtoybox_keeper_keep(12, 1, 5.0) == -1 and lib.srtoybox_keeper_keep(-2, 1, 5.0) == -1     # 없는 칸


def test_keep_rests_outside_a_game_and_says_nothing(lib, game, tmp_path):
    """게임 밖 · 멀티플레이에서는 쉰다(알림도 없다 — 0.5초마다 온다). 돌아오면 다시 한다."""
    keep(lib, TREASURY, 20e9)
    game.menu()
    lib.srtoybox_keeper_tick_at(1000)
    game.play(176)
    game.poke(MULTIPLAYER, "<B", 1)
    lib.srtoybox_keeper_tick_at(1500)
    assert game.treasury(176) == 14.43e9 and told(lib) == ("", "") and kept_lines(tmp_path) == []
    game.poke(MULTIPLAYER, "<B", 0)
    lib.srtoybox_keeper_tick_at(1600)                          # 쉰 것도 본 것이다 — 다음은 0.5초 뒤
    assert game.treasury(176) == 14.43e9
    lib.srtoybox_keeper_tick_at(2000)
    assert game.treasury(176) == 20e9


def test_keep_follows_the_country_being_played(lib, game):
    """플레이하는 나라가 바뀌면 그 뒤로는 새 나라의 값을 본다. 앞의 나라는 건드리지 않는다."""
    keep(lib, TREASURY, 20e9)
    lib.srtoybox_keeper_tick_at(1000)
    assert game.treasury(176) == 20e9 and game.treasury(141) == 5e9
    game.set_treasury(176, 1e9)
    game.play(141)                                             # 폴란드로 바꿨다(이 나라로 플레이)
    lib.srtoybox_keeper_tick_at(1500)
    assert game.treasury(141) == 20e9 and game.treasury(176) == 1e9


def test_a_button_and_keep_work_on_the_same_tick(lib, game):
    """단추의 요청을 먼저 쓰고, 때가 됐으면 이어서 유지를 본다 — 방금 내린 값이 바닥 아래면 올린다."""
    keep(lib, TREASURY, 5e9)
    lib.srtoybox_keeper_tick_at(1000)                          # 14.43 B 는 바닥 위다
    assert ask(lib, TREASURY, SET, 0.0)
    lib.srtoybox_keeper_tick_at(1100)
    assert game.treasury(176) == 0.0 and told(lib) == ("국고 14.43 B -> 0", "")   # 아직 때가 아니다
    lib.srtoybox_keeper_tick_at(1500)
    assert game.treasury(176) == 5e9 and told(lib) == ("국고 14.43 B -> 0", "")   # 유지는 "마지막으로 쓴 값"을 덮지 않는다
    assert ask(lib, TREASURY, SET, 1.0)
    lib.srtoybox_keeper_tick_at(2000)                          # 요청과 유지가 같은 틱에
    assert game.treasury(176) == 5e9 and told(lib) == ("국고 5.00 B -> 1", "")


def test_keep_leaves_a_value_that_is_not_a_number(lib, game):
    keep(lib, TREASURY, 5e9)
    game.set_treasury(176, float("nan"))
    lib.srtoybox_keeper_tick_at(1000)
    assert game.treasury(176) != game.treasury(176) and told(lib) == ("", "")      # 아직 NaN 이고, 알리지도 않는다


def test_keep_stops_when_a_write_fails(lib, game, tmp_path):
    game.lock(176)
    game.play(176)
    keep(lib, TREASURY, 20e9)
    keep(lib, 3, 5000.0)
    lib.srtoybox_keeper_tick_at(1000)
    lib.srtoybox_keeper_tick_at(2000)
    assert game.treasury(176) == 14.43e9 and game.stock(176, 3) == 2500.0
    assert text(lib.srtoybox_values_off) == FAILED_OFF
    log = (tmp_path / "toybox.log").read_text(encoding="utf-8")
    assert log.count("값 쓰기 실패") == 1 and "값 쓰기 실패 (국고) — 값 쓰기를 끕니다" in log     # 한 번 실패하면 더 시도하지 않는다


def test_keep_logs_the_first_raise_again_after_its_setting_changes(lib, game, tmp_path):
    keep(lib, 3, 5000.0)
    lib.srtoybox_keeper_tick_at(1000)
    game.set_stock(176, 3, 0.0)
    lib.srtoybox_keeper_tick_at(1500)
    assert kept_lines(tmp_path) == ["유지: 석유 2.5 K -> 5.0 K"]
    keep(lib, 3, 9000.0)                                       # 바닥을 고쳤다
    lib.srtoybox_keeper_tick_at(1501)                          # 고친 다음 틱에 바로 본다
    assert game.stock(176, 3) == 9000.0
    assert kept_lines(tmp_path) == ["유지: 석유 2.5 K -> 5.0 K", "유지: 석유 5.0 K -> 9.0 K"]
    keep(lib, 3, 9000.0, on=False)
    game.set_stock(176, 3, 0.0)
    lib.srtoybox_keeper_tick_at(5000)
    assert game.stock(176, 3) == 0.0                           # 껐다


def test_the_status_line_says_what_is_being_kept(lib, game):
    """상태 줄의 뒤에 붙는 글. 게임 안에서는 지금 유지되는 것의 이름(넷을 넘으면 첫 이름과 나머지의 수), 쉬는 동안에는 켜진 수와 까닭."""
    active = lambda used: text(lib.srtoybox_keep_text, used.encode(), None)
    resting = lambda why: text(lib.srtoybox_keep_text, None, why.encode("utf-8"))
    assert active("1" * 12) == "" and resting("게임에 들어가면 적용") == ""          # 켠 것이 없다
    keep(lib, 3, 1.0)
    keep(lib, 7, 1.0)
    assert active("1" * 12) == " · 유지 중: 석유, 전력"
    assert active("000100000000") == " · 유지 중: 석유"                              # 이번 판에서 쓰지 않는 물자는 유지되지 않는다
    assert active("0" * 12) == ""
    assert resting("게임에 들어가면 적용") == " · 유지 2개 켜짐(게임에 들어가면 적용)"
    keep(lib, TREASURY, 1.0)
    keep(lib, 0, 1.0)
    assert active("1" * 12) == " · 유지 중: 국고, 농산물, 석유, 전력"                 # 국고가 먼저, 물자는 칸의 순서로
    keep(lib, 11, 1.0)
    keep(lib, 10, 1.0)
    assert active("1" * 12) == " · 유지 중: 국고 외 5개"                             # 넷을 넘으면
    assert active("000100000001") == " · 유지 중: 국고, 석유, 물자 #11"
    assert resting("멀티플레이에서는 쉽니다") == " · 유지 6개 켜짐(멀티플레이에서는 쉽니다)"


def test_nothing_is_asked_when_values_cannot_be_written(lib, game):
    lib.srtoybox_test_values(None)                             # 값의 자리를 찾지 못한 게임
    assert not ask(lib, TREASURY, ADD, 1e9)
    lib.srtoybox_keeper_tick()
    assert game.treasury(176) == 14.43e9


def ask_more(lib, slot: int, region: int = 0, amount: float = 0.0) -> bool:
    """더 쓰는 값의 요청(기술 수준 +1 · 세계 시장 여론 최고 · 관계)."""
    return lib.srtoybox_keeper_more(slot, region, amount) == 1


def logged(tmp_path) -> list[str]:
    """로그의 줄들(때를 뗀 것)."""
    log = tmp_path / "toybox.log"
    return [line.split(" ", 2)[2] for line in log.read_text(encoding="utf-8").splitlines()] if log.is_file() else []


def test_a_tech_request_adds_one_on_the_next_tick(lib, game, tmp_path):
    assert ask_more(lib, TECH) and ask_more(lib, TECH)
    assert game.tech(176) == 130.0                             # 누른 것만으로는 쓰지 않는다 — 창 스레드의 틱에서 쓴다
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 132.0 and game.tech(141) == 128.0
    assert told(lib) == ("기술 수준 131 -> 132", "")
    assert logged(tmp_path) == ["값 쓰기: 기술 수준 130 -> 131", "값 쓰기: 기술 수준 131 -> 132"]
    assert game.peek(OPTIONS, "<I") == 0                       # 치트 허용 비트는 그대로다


def test_tech_stops_at_its_limit_and_leaves_what_it_cannot_read(lib, game):
    game.set_tech(176, 998.0)
    assert ask_more(lib, TECH)
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 999.0 and told(lib) == ("기술 수준 998 -> 999", "")
    assert ask_more(lib, TECH)
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 999.0 and told(lib)[1] == "기술 수준이 한도(999)라 쓰지 않았습니다."
    game.set_tech(176, 130.5)                                  # 정수가 아니어도 1 을 더하고, 있는 그대로 적는다
    assert ask_more(lib, TECH)
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 131.5 and told(lib) == ("기술 수준 130.5 -> 131.5", "")
    for odd in (NAN, INF, -5.0):
        game.set_tech(176, odd)
        before = game.snapshot(176)
        assert ask_more(lib, TECH)
        lib.srtoybox_keeper_tick()
        assert game.snapshot(176) == before and told(lib)[1] == "기술 수준을 읽을 수 없어 쓰지 않았습니다."


def test_an_opinion_request_sets_the_three_cells(lib, game, tmp_path):
    assert ask_more(lib, OPINION)
    lib.srtoybox_keeper_tick()
    assert game.opinion(176) == [1.0, 1.0, 1.0] and game.opinion(141) == [0.0, 0.0, 0.0]
    assert told(lib) == ("세계 시장 여론 최고", "") and logged(tmp_path) == ["값 쓰기: 세계 시장 여론 최고"]


def test_relation_requests_write_both_sides(lib, game, tmp_path):
    assert ask_more(lib, RELATION, 1106, BEST)
    lib.srtoybox_keeper_tick()
    assert game.relation(176, 141) == [1.0, 1.0, 0.0] and game.relation(141, 176) == [1.0, 1.0, 0.0]
    assert told(lib) == ("관계 최고 — 폴란드 (1106)", "")
    assert ask_more(lib, RELATION, 1106, NEUTRAL)
    lib.srtoybox_keeper_tick()
    assert game.relation(176, 141) == [0.0, 0.0, 0.0] and game.relation(141, 176) == [0.0, 0.0, 0.0]
    assert told(lib) == ("관계 중립 — 폴란드 (1106)", "")
    assert logged(tmp_path) == ["값 쓰기: 관계 최고 — 폴란드 (1106)", "값 쓰기: 관계 중립 — 폴란드 (1106)"]
    assert game.treasury(176) == 14.43e9 and game.peek(OPTIONS, "<I") == 0


def test_a_relation_request_for_a_country_that_is_gone_is_dropped(lib, game, tmp_path):
    """누른 뒤 쓰기 전에 그 나라가 없어졌다(다른 판, 병합됨) — 쓰지 않고 알린다. 자기 나라 · 없는 번호도 같다."""
    gone = "그 나라는 이번 판에 없어 쓰지 않았습니다."
    assert ask_more(lib, RELATION, 1106, BEST)
    struct.pack_into("<I", game.objects[141], 0, 5)            # 폴란드가 이번 판에 없는 지역이 됐다
    before = everything(game)
    lib.srtoybox_keeper_tick()
    assert everything(game) == before and told(lib) == ("", gone)
    for number in (1499, 9999):
        assert ask_more(lib, RELATION, number, BEST)
        lib.srtoybox_keeper_tick()
        assert everything(game) == before and told(lib) == ("", gone)
    assert logged(tmp_path) == []


def test_more_requests_are_dropped_outside_a_game(lib, game):
    assert ask_more(lib, TECH) and ask_more(lib, OPINION) and ask_more(lib, RELATION, 1106, BEST)
    game.menu()
    before = everything(game)
    lib.srtoybox_keeper_tick()
    game.play(176)
    lib.srtoybox_keeper_tick()                                 # 버린 요청이 돌아온 뒤에 쓰이지 않는다
    assert game.tech(176) == 130.0 and game.opinion(176) == [0.5, 0.25, 0.75] and told(lib) == ("", LEFT_GAME)
    assert game.relation(176, 141) == [0.25, -0.5, 0.75] and len(before) == len(everything(game))


def test_money_stock_and_more_requests_are_written_in_the_order_asked(lib, game, tmp_path):
    assert ask(lib, TREASURY, ADD, 1e9) and ask_more(lib, TECH) and ask(lib, 3, ADD, 1.0) and ask_more(lib, OPINION)
    lib.srtoybox_keeper_tick()
    assert logged(tmp_path) == ["값 쓰기: 국고 14.43 B -> 15.43 B", "값 쓰기: 기술 수준 130 -> 131", "값 쓰기: 석유 2.5 K -> 2.5 K",
                                "값 쓰기: 세계 시장 여론 최고"]
    assert told(lib) == ("세계 시장 여론 최고", "")


def test_more_requests_do_not_need_money_and_stock(lib, game):
    """묶음은 서로 기대지 않는다: 국고 · 재고의 자리를 못 찾은 게임에서도 더 쓰는 값은 쓴다. 유지는 쉰다."""
    lib.srtoybox_test_values(None)
    keep(lib, TREASURY, 50e9)
    assert not ask(lib, TREASURY, ADD, 1e9) and ask_more(lib, TECH)
    lib.srtoybox_keeper_tick_at(1000)
    assert game.tech(176) == 131.0 and game.treasury(176) == 14.43e9 and told(lib) == ("기술 수준 130 -> 131", "")


def test_a_request_is_not_taken_for_a_group_that_is_off(lib, game):
    """그 묶음의 자리를 못 찾았으면 그 요청만 받지 않는다. 다른 묶음과 국고 · 재고는 그대로 받는다."""
    lib.srtoybox_test_more(ctypes.byref(toybox.MoreLayout(**{**MORE, "tech": 0})))
    assert not ask_more(lib, TECH)
    assert ask_more(lib, OPINION) and ask_more(lib, RELATION, 1106, NEUTRAL) and ask(lib, TREASURY, ADD, 1.0)
    assert text(lib.srtoybox_more_off, MORE_TECH) == "이 게임 판에서는 쓸 수 없습니다 (값의 자리를 주지 않았습니다)"
    assert text(lib.srtoybox_more_off, MORE_OPINION) == "" and lib.srtoybox_keeper_more(7, 0, 0.0) == -1
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 130.0 and game.opinion(176) == [1.0, 1.0, 1.0]


def test_a_failed_relation_write_turns_all_value_writing_off(lib, game, tmp_path):
    """쓰기가 실패하면 묶음 1 의 규칙 그대로 그 실행에서는 값 쓰기 전체를 끈다(돈 · 물자 · 유지 포함). 반쪽은 남지 않는다."""
    game.lock(141)                                             # 고른 나라의 객체가 읽기 전용 쪽에 있다
    mine = game.snapshot(176)
    assert ask_more(lib, RELATION, 1106, BEST) and ask_more(lib, TECH)
    lib.srtoybox_keeper_tick()
    assert game.snapshot(176) == mine                          # 플레이어 쪽 칸도, 뒤따르던 요청도 쓰지 않았다
    assert text(lib.srtoybox_values_off) == FAILED_OFF
    assert [text(lib.srtoybox_more_off, group) for group in (MORE_TECH, MORE_OPINION, MORE_RELATIONS)] == [FAILED_OFF] * 3
    assert not ask_more(lib, TECH) and not ask(lib, TREASURY, ADD, 1e9)
    assert logged(tmp_path) == ["값 쓰기 실패 (관계 — 폴란드 (1106)) — 값 쓰기를 끕니다"]


def test_a_failed_tech_write_says_so(lib, game, tmp_path):
    game.lock(176)
    game.play(176)
    assert ask_more(lib, TECH) and ask_more(lib, OPINION)
    lib.srtoybox_keeper_tick()
    assert game.tech(176) == 130.0 and game.opinion(176) == [0.5, 0.25, 0.75]
    assert logged(tmp_path) == ["값 쓰기 실패 (기술 수준) — 값 쓰기를 끕니다"] and told(lib)[0] == ""


def test_people_and_approval_are_written_to_the_players_region_only(lib):
    """3단계 4: 인구 +100만은 플레이어의 인구 칸과 인구의 풀 칸에 같은 수를 더하고, 지지율은 한 칸에 1.0 을 쓴다.
    (풀에 더한 것이 자정의 셈에 남는다 — 게임에서 봤다. 인구 칸만 올리면 첫 자정에 되돌아간다.)
    다른 나라의 객체 · 플레이어의 다른 칸은 한 바이트도 바뀌지 않는다."""
    fake = germany()
    fake.set_people(176, (82615760.0, 3632.0), 0.387)
    fake.set_people(141, (38e6, 2500.0), 0.5)
    before = everything(fake)
    assert write_more(lib, fake, PEOPLE_ADD, value=1e6) == (DONE, 2)
    assert more(lib, fake)["people"] == "83615760,1003632"
    assert write_more(lib, fake, APPROVAL_BEST) == (DONE, 1)
    assert more(lib, fake)["approval"] == "1"
    cells = set().union(*(where(fake, 176, MORE[name]) for name in ("people0", "people1", "approval")))
    assert changed(before, everything(fake)) <= cells


def test_people_are_not_written_when_a_cell_is_not_a_number_or_the_place_is_unknown(lib):
    fake = germany()
    fake.set_people(176, (82615760.0, float("nan")), 0.387)
    before = everything(fake)
    assert write_more(lib, fake, PEOPLE_ADD, value=1e6) == (BAD_VALUE, 0)                   # 두 칸 가운데 하나가 수가 아니다 — 하나도 쓰지 않는다
    assert write_more(lib, fake, PEOPLE_ADD, value=1e6, layout=toybox.MoreLayout(**{**MORE, "people1": 0})) == (OFF, 0)
    assert write_more(lib, fake, APPROVAL_BEST, layout=toybox.MoreLayout(**{**MORE, "approval": 0})) == (OFF, 0)
    assert everything(fake) == before
