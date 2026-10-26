"""ToyBox 의 충성 · 지지(docs/superpowers/specs/2026-10-26-toybox-loyalty-design.md): 점령지 충성, 지지율 · 여론의 최소 유지.

가짜 게임(tests/toybox_fake_game.py)에 지도 객체와 칸 레코드(16바이트 — +0 주인, +2 충성 대상)를 붙여 본다.
"""
import ctypes
import struct

import pytest

from srkit import toybox
from toybox_fake_game import FakeGame

MAP_GLOBAL = 0x3F00                      # 가짜 게임 안의 "지도 객체의 포인터가 든 전역"
PARENT = 0x20C                           # 지역 객체 안의 위 나라 칸(가짜)
GERMANY, WEST, EAST, DENMARK, SWEDEN = 176, 171, 172, 157, 160
DONE, NOT_IN_GAME, BAD_VALUE, OFF = 0, 1, 3, 5   # game.h 의 Wrote
WIDTH = 8


@pytest.fixture(scope="module")
def lib(cfg):
    if not toybox.output(cfg).is_file():
        pytest.skip("ToyBox DLL 미빌드 (srkit toybox-build)")
    lib = toybox.library(cfg)
    lib.srtoybox_test_game.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    for name in ("srtoybox_test_values", "srtoybox_test_more", "srtoybox_test_mult"):
        getattr(lib, name).argtypes = [ctypes.c_void_p]
    lib.srtoybox_test_acts.argtypes = [ctypes.c_void_p] * 10
    lib.srtoybox_loyalty.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    lib.srtoybox_loyalty_off.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_test_loyalty_hook.argtypes = [ctypes.c_void_p]
    return lib


class Map:
    """가짜 지도: 지도 객체(+0 칸 레코드의 포인터, +0x80 너비, +0x88 칸 수)와 칸 레코드."""

    def __init__(self, fake: FakeGame, hexes: list[tuple[int, int]], width: int = WIDTH):
        self.records = (ctypes.c_ubyte * (16 * len(hexes)))()
        for i, (owner, loyal) in enumerate(hexes):
            struct.pack_into("<HH12s", self.records, 16 * i, owner, loyal, bytes([i % 251] * 12))   # 뒤의 12바이트는 그대로여야 한다
        self.head = (ctypes.c_ubyte * 0x100)()
        struct.pack_into("<Q", self.head, 0, ctypes.addressof(self.records))
        struct.pack_into("<i", self.head, 0x80, width)
        struct.pack_into("<i", self.head, 0x88, len(hexes))
        fake.poke(MAP_GLOBAL, "<Q", ctypes.addressof(self.head))

    def hex(self, i: int) -> tuple[int, int]:
        return struct.unpack_from("<HH", self.records, 16 * i)

    def tail(self, i: int) -> bytes:
        return bytes(self.records[16 * i + 4:16 * i + 16])


def mainland() -> list[tuple[int, int]]:
    """독일 본토(서독 다섯 · 동독 둘 · 독일 하나), 옛 덴마크 셋(주인 독일 · 충성 덴마크), 스웨덴 칸, 주인 없는 칸."""
    hexes = [(GERMANY, WEST)] * 5 + [(GERMANY, EAST)] * 2 + [(GERMANY, GERMANY)] + [(GERMANY, DENMARK)] * 3
    hexes += [(SWEDEN, DENMARK), (SWEDEN, SWEDEN), (GERMANY, 0), (0, 0), (0, 0)]
    return hexes + [(0, 0)] * (2 * WIDTH - len(hexes))


@pytest.fixture
def game(lib):
    fake = FakeGame()
    for index, number in ((GERMANY, 1499), (WEST, 1401), (EAST, 1402), (DENMARK, 1201), (SWEDEN, 1301)):
        fake.region(index, number)
    for sub in (WEST, EAST):
        fake._write(sub, PARENT, "<H", GERMANY)
    fake.play(GERMANY)
    lib.srtoybox_test_game(fake.base, ctypes.byref(fake.at))
    lib.srtoybox_test_values(ctypes.byref(fake.layout))
    lib.srtoybox_test_more(ctypes.byref(fake.more))
    lib.srtoybox_test_mult(ctypes.byref(toybox.MultLayout(parent=PARENT)))
    lib.srtoybox_test_acts(ctypes.byref(toybox.ActLayout(map=MAP_GLOBAL)), *([None] * 9))
    lib.srtoybox_test_loyalty_hook(None)
    fake.map = Map(fake, mainland())
    yield fake
    lib.srtoybox_test_loyalty_hook(None)
    lib.srtoybox_test_game(None, None)


def loyalty(lib) -> tuple[int, int, int]:
    hexes, target = ctypes.c_int(-1), ctypes.c_int(-1)
    wrote = lib.srtoybox_loyalty(ctypes.byref(hexes), ctypes.byref(target))
    return wrote, hexes.value, target.value


def test_conquered_hexes_become_loyal_to_the_players_side(lib, game):
    assert loyalty(lib) == (DONE, 3, WEST)                     # 본토의 "내 편" 칸에 가장 많은 것(서독 다섯)
    assert [game.map.hex(i) for i in (8, 9, 10)] == [(GERMANY, WEST)] * 3
    assert all(game.map.tail(i) == bytes([i % 251] * 12) for i in range(2 * WIDTH))   # 레코드의 나머지는 그대로
    assert loyalty(lib) == (DONE, 0, WEST)                     # 바꿀 칸이 없으면 0


def test_mainland_hexes_loyal_to_a_subregion_are_left_alone(lib, game):
    loyalty(lib)
    assert [game.map.hex(i) for i in range(8)] == [(GERMANY, WEST)] * 5 + [(GERMANY, EAST)] * 2 + [(GERMANY, GERMANY)]


def test_other_countries_hexes_are_left_alone(lib, game):
    loyalty(lib)
    assert game.map.hex(11) == (SWEDEN, DENMARK) and game.map.hex(12) == (SWEDEN, SWEDEN)


def test_hexes_loyal_to_nobody_are_left_alone(lib, game):
    loyalty(lib)
    assert game.map.hex(13) == (GERMANY, 0) and game.map.hex(14) == (0, 0)


def test_nothing_is_written_without_a_loyal_hex(lib, game):
    game.map = Map(game, [(GERMANY, DENMARK)] * WIDTH)        # 내 칸이 모두 덴마크에 충성한다 — 판정이 틀렸을 수 있다
    assert loyalty(lib) == (BAD_VALUE, 0, 0)
    assert all(game.map.hex(i) == (GERMANY, DENMARK) for i in range(WIDTH))


@pytest.mark.parametrize("broken", ["width", "loyal_index"])
def test_nothing_is_written_when_the_map_makes_no_sense(lib, game, broken):
    hexes = mainland()
    if broken == "loyal_index":
        hexes[15] = (0, 1024)                                  # 지역 표 밖의 충성 대상
    game.map = Map(game, hexes, width=WIDTH if broken != "width" else 5)   # 칸 수가 너비의 배수가 아니다
    assert loyalty(lib)[0] == BAD_VALUE
    assert [game.map.hex(i) for i in (8, 9, 10)] == [(GERMANY, DENMARK)] * 3


def test_a_hex_the_game_changed_is_skipped(lib, game):
    """스냅숏 뒤 · 쓰기 전에 게임이 칸 하나를 바꿨다 — 그 칸은 게임이 쓴 값 그대로다."""
    def changed():
        struct.pack_into("<HH", game.map.records, 16 * 9, GERMANY, SWEDEN)
    hook = ctypes.CFUNCTYPE(None)(changed)
    lib.srtoybox_test_loyalty_hook(ctypes.cast(hook, ctypes.c_void_p))
    assert loyalty(lib) == (DONE, 2, WEST)
    assert game.map.hex(8) == (GERMANY, WEST) and game.map.hex(9) == (GERMANY, SWEDEN) and game.map.hex(10) == (GERMANY, WEST)


def test_loyalty_is_off_without_the_parent_cell(lib, game):
    lib.srtoybox_test_mult(ctypes.byref(toybox.MultLayout()))
    buf = ctypes.create_string_buffer(512)
    lib.srtoybox_loyalty_off(buf, len(buf))
    assert buf.value.decode("utf-8") != ""
    assert loyalty(lib)[0] == OFF and game.map.hex(8) == (GERMANY, DENMARK)


def test_loyalty_is_off_without_the_map(lib, game):
    lib.srtoybox_test_acts(ctypes.byref(toybox.ActLayout()), *([None] * 9))
    assert loyalty(lib)[0] == OFF


@pytest.mark.parametrize("where", ["menu", "multiplayer"])
def test_nothing_is_written_in_the_menu_or_in_multiplayer(lib, game, where):
    if where == "menu":
        game.menu()
    else:
        game.poke(0x10, "<B", 1)
    assert loyalty(lib)[0] == NOT_IN_GAME and game.map.hex(8) == (GERMANY, DENMARK)
