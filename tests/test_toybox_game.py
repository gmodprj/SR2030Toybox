"""ToyBox: 주소 찾기(새 찾기 = 서명, 옛 찾기 = 치트 닻) · 게임 상태 읽기 · 나라 이름표 (native/srtoybox 의 locate · game · regions)."""
import ctypes
import re
import struct
from ctypes import wintypes

import pytest

import toybox_fake_exe
from toybox_fake_game import INDEX, MODE, MULTIPLAYER, OPTIONS, POINTER, PROGRAM, TABLE, FakeGame
from srkit import toybox

FAKE = toybox_fake_exe
# build 21347933 (게임 12.1.1360, PE TimeDateStamp 0x695377b6) 의 주소. docs/11-game-internals.md 의 표와 같다.
BUILD_21347933 = {"handler": 0x522330, "context": 0x1764310, "multiplayer": 0xF19634, "options": 0x1EB556C,
                  "program_state": 0x1EED11C, "mode_state": 0xE7BE30, "player_index": 0x18294E0, "player_pointer": 0x18295F8,
                  "region_table": 0x1AF78C0, "region_count": 0x18294D8}
BUILD_21347933_STAMP = 0x695377B6
BUILD_STATE = {name: BUILD_21347933[name] for name in toybox.STATE_FIELDS}
BUILD_LEGACY = {name: BUILD_21347933[name] for name in toybox.LEGACY_FIELDS}
BUILD_VALUES = {"world_pointer": 0x1AF5868, "treasury": 0x14B88, "stock_first": 0x14DA4, "stock_step": 0x150,
                "used_first": 0x18, "used_step": 0x84}
BUILD_MORE = {"tech": 0x14CD0, "opinion0": 0x14AF4, "opinion1": 0x14AF8, "opinion2": 0x14B10, "relation0": 0x15F10,
              "relation1": 0x16F10, "casus": 0x17F10, "people0": 0x14B48, "people1": 0x14B58, "approval": 0x14B04}
BUILD_RESEARCH = {"tech_table": 0x1829620, "tech_count": 0x1829098, "design_table": 0x1829610, "design_count": 0x182909C,
                  "world": 0x17A9020, "lists": 0x3568C0, "recompute": 0xBDD380}
# 연구의 표와 목록의 꼴(native/srtoybox/locate.h 의 상수)이 박힌 서명: (찾을 것, 몇째 서명, 그 명령의 바이트).
# {이름:b} · {이름:d} 자리에 DLL 이 아는 그 상수가 1바이트 · 4바이트로 들어간다({이름+2:b} 는 2 를 더한 값).
# 게임이 업데이트되어 서명을 다시 뽑을 때 이 표도 함께 고친다 — 꼴의 상수가 서명에서 빠지면 여기서 걸린다.
RESEARCH_PINS = [
    ("tech_table", 0, "48 69 F0 {tech_size:d}"),                       # imul rsi,rax,<기술 레코드>
    ("tech_table", 0, "48 8D 7A {tech_owners:b}"),                     # lea rdi,[rdx+<보유 묶음>]
    ("tech_table", 0, "B9 {owners_bytes:d}"),                          # mov ecx,<묶음의 크기> (게임이 새 묶음을 받는 곳)
    ("tech_table", 1, "4C 69 CE {tech_size:d} 48 83 C1 {tech_owners:b}"),
    ("tech_table", 2, "44 38 24 03"),                                  # cmp byte ptr [rbx+rax],r12b — 빈 자리는 +0 의 바이트로 가린다
    ("tech_table", 2, "0F B6 4C 03 {tech_level:b}"),                   # movzx ecx,byte ptr [rbx+rax+<수준>]
    ("tech_count", 0, "80 39 00"),                                     # cmp byte ptr [rcx],0
    ("tech_count", 0, "0F BF 41 {tech_needs:b} 3B C6 74 08 0F BF 41 {tech_needs+2:b}"),   # movsx eax,word ptr [rcx+<선행>] 둘
    ("tech_count", 1, "0F BF 44 39 {tech_needs:b}"),
    ("tech_count", 1, "0F BF 44 39 {tech_needs+2:b}"),
    ("tech_count", 2, "49 69 D6 {tech_size:d}"),
    ("tech_count", 2, "0F BF 42 {tech_needs:b} 89 44 24 60 0F BF 42 {tech_needs+2:b}"),
    ("design_table", 0, "49 8D 9F {design_owners:d}"),                 # lea rbx,[r15+<보유 묶음>]
    ("design_table", 0, "48 69 C8 {design_size:d}"),                   # imul rcx,rax,<부대 설계 레코드>
    ("design_table", 0, "B9 {owners_bytes:d}"),
    ("design_table", 1, "49 39 38"),                                   # cmp qword ptr [r8],rdi — 빈 자리는 +0 의 포인터로 가린다
    ("design_table", 1, "41 F7 80 {design_hold_b:d} {design_hold_b_bit:d}"),     # test dword ptr [r8+<깃발 B>],<비트>
    ("design_table", 1, "41 F6 80 {design_hold_a:d} {design_hold_a_bit:b}"),     # test byte ptr [r8+<깃발 A>],<비트>
    ("design_table", 1, "66 41 39 78 {design_open:b}"),                # cmp word ptr [r8+<연구 대상>],di
    ("design_table", 2, "48 83 3C 2B 00"),                             # cmp qword ptr [rbx+rbp],0
    ("design_table", 2, "F7 84 2B {design_hold_b:d} {design_hold_b_bit:d}"),
    ("design_table", 2, "F6 84 2B {design_hold_a:d} {design_hold_a_bit:b}"),
    ("design_table", 2, "66 83 7C 2B {design_open:b} 00"),
    ("design_count", 0, "48 69 CB {design_size:d}"),
    ("design_count", 0, "66 42 83 7C 39 {design_open:b} 00"),
    ("design_count", 0, "4D 8D 4F {design_needs:b}"),                  # lea r9,[r15+<선행>]
    ("design_count", 1, "49 69 DE {design_size:d}"),
    ("design_count", 1, "0F B7 44 0B {design_needs:b}"),               # movzx eax,word ptr [rbx+rcx+<선행>]
    ("design_count", 2, "49 81 C2 {design_size:d}"),                   # add r10,<부대 설계 레코드>
    ("lists", 0, "48 8D 0C 40 48 8B 94 CA"),                           # lea rcx,[rax+rax*2] / mov rdx,[rdx+rcx*8+…] — 칸 하나가 24바이트
    ("lists", 0, "80 79 {node_kind:b} 02"),                            # cmp byte ptr [rcx+<종류>],2
    ("lists", 0, "44 3B 59 {node_id:b}"),                              # cmp r11d,[rcx+<번호>]
    ("lists", 0, "48 8B 41 {node_next:b}"),                            # mov rax,[rcx+<다음>]
    ("lists", 0, "F7 41 {node_flags:b} 00 00 00 88"),                  # test dword ptr [rcx+<깃발>],88000000h
    ("lists", 1, "80 79 {node_kind:b} 01"),
    ("lists", 1, "44 3B 49 {node_id:b}"),
    ("lists", 1, "48 8B 40 {node_next:b}"),
    ("lists", 1, "F7 41 {node_flags+4:b} 00 00 00 88"),                # 둘째 쪽의 깃발
    ("lists", 2, "48 8D 0C 40 49 8B 94 CC"),
    ("lists", 2, "80 7B {node_kind:b} 01"),
    ("lists", 2, "3B 7B {node_id:b}"),
    ("lists", 2, "48 8B 40 {node_next:b}"),
    ("recompute", 0, "BA FF FF FF FF 49 8B C9 E8"),                    # mov edx,-1 / mov rcx,r9 / call — 인자는 (세계 객체, 지역 인덱스)
    ("recompute", 1, "BA FF FF FF FF 49 8B CA E8"),
    ("recompute", 2, "BA FF FF FF FF 49 8B CC E8"),
]
# 서명에 박혀 있어야 하는 상수(쓰기를 가른다). 박지 못한 것: design_class · design_year(표시에만 쓴다), 선행의 개수(tech_need_count 는
# 선행 둘의 자리로 드러난다. design_need_count 는 드러나지 않는다), tech_kind · design_name(자리가 0 이라 명령에 상수가 없다 — 명령의 꼴로 박았다)
RESEARCH_PINNED = {"tech_size", "tech_level", "tech_needs", "tech_owners", "design_size", "design_open", "design_needs", "design_hold_a",
                   "design_hold_a_bit", "design_hold_b", "design_hold_b_bit", "design_owners", "owners_bytes", "node_next", "node_id",
                   "node_kind", "node_flags"}
GARBAGE =[b"", b"MZ", bytes(0x1000), b"MZ" + bytes(0x3A) + struct.pack("<I", 0x7FFFFFF0) + bytes(0x100)]
GARBAGE_IDS = ["empty", "two-bytes", "zeros", "header-far-outside"]


@pytest.fixture(scope="module")
def lib(cfg):
    path = toybox.output(cfg)
    if not path.is_file():
        pytest.skip("ToyBox DLL 미빌드 (srkit toybox-build)")
    lib = toybox.library(cfg)
    lib.srtoybox_game_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(toybox.GameAddresses), ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_region_label.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_region_view.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
                                         ctypes.c_char_p, ctypes.c_int]
    lib.srtoybox_dbcs.argtypes = [ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_uint]
    return lib


@pytest.fixture(scope="module")
def sigs(lib):
    """DLL 에 든 서명 표: [(찾을 것, 서명 글)] — 찾을 것마다 셋, 표의 순서대로."""
    return [(row.name, row.text) for row in toybox.state_of(lib, b"")[2]]


@pytest.fixture(scope="module")
def value_sigs(lib):
    """DLL 에 든 값 묶음의 서명 표: [(찾을 것, 서명 글)] — 찾을 것마다 셋, 표의 순서대로."""
    return [(row.name, row.text) for row in toybox.values_of(lib, b"")[2]]


@pytest.fixture(scope="module")
def more_sigs(lib):
    """DLL 에 든 "더 쓰는 값"의 서명 표: [(찾을 것, 서명 글)] — 찾을 것마다 셋, 표의 순서대로."""
    return [(row.name, row.text) for row in toybox.more_of(lib, b"")[2]]


@pytest.fixture(scope="module")
def research_sigs(lib):
    """DLL 에 든 연구의 서명 표: [(찾을 것, 서명 글)] — 찾을 것마다 셋, 표의 순서대로."""
    return [(row.name, row.text) for row in toybox.research_of(lib, b"", None)[2]]


@pytest.fixture(scope="module")
def act_sigs(lib):
    """DLL 에 든 "부르는 게임의 함수"의 서명 표: [(찾을 것, 서명 글)] — 찾을 것마다 셋."""
    return [(row.name, row.text) for row in toybox.acts_of(lib, b"")[2]]


BUILD_ACTS = {"colonize": 0x771FB0, "fight": 0x6FEB60, "become": 0x6E8D50, "elect": 0x6E7AC0, "news": 0x6EF3C0, "map_pick": 0x1764320,
              "player_index2": 0x18294EC, "player_pointer2": 0x1829600, "leader_table": 0x1B058D8, "leader_id": 0xAC,
              "treaty": 0x70CBD0, "treaties": 0x1A3B0, "annex": 0x741530, "map": 0x1829020, "spawn": 0x79E2F0, "map_hex": 0x1764338, "picked_units": 0x17697F0, "upgrades": 0x368A24, "game_over": 0x1829508, "winners": 0x1829510, "ui_flags": 0xF1AF30, "gui": 0xF14EB8, "gui_vtable": 0xC57190,
              "deploy_hex": 0x6EC490, "units": 0x1AF5850, "region_name": 0x1BCF8}


def test_the_called_functions_are_found_by_their_call_sites(lib, act_sigs, game_dir):
    """3단계 4: 부르는 게임의 함수(식민지화)는 그것을 부르는 자리의 서명 셋이 모두 한 번씩 맞고 같은 주소를 내야 찾은 것이다 —
    그 주소가 함수 표에 있는 함수의 시작이어야 한다. 하나라도 어긋나면 못 찾은 것으로 친다(엉뚱한 함수를 부르지 않는다)."""
    assert [name for name, _ in act_sigs] == [name for name in toybox.ACT_FIELDS for _ in range(3)] and len({t for _, t in act_sigs}) == 78
    acts = toybox_fake_exe.ACTS
    assert toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs))[:2] == (acts, [""] * 26)
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, broken={1}))
    assert found == {**acts, "colonize": 0} and "식민지화 함수" in why[0] and "3개 가운데 2개" in why[0]   # 셋 가운데 둘로는 찾지 않는다
    assert why[1:] == [""] * 25                                                                          # 저마다 따로 찾는다
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, stray={5}))
    assert found == {**acts, "fight": 0} and "전쟁 함수" in why[1] and "서로 다른" in why[1]
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, targets={"colonize": toybox_fake_exe.COLONIZE + 4}))
    assert found == {**acts, "colonize": 0} and why[0] == "식민지화 함수: 찾은 주소가 함수의 시작이 아닙니다"
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, targets={"map_pick": toybox_fake_exe.FIGHT}))
    assert found == {**acts, "map_pick": 0} and why[5] == "지도에서 고른 지역: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다"
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, targets={"player_pointer2": toybox_fake_exe.DATA + 0x7C}))
    assert found == {**acts, "player_pointer2": 0} and "플레이어 포인터의 둘째 사본" in why[7]           # 포인터는 8의 배수 자리에 있다
    found, why, _ = toybox.acts_of(lib, toybox_fake_exe.sig_image(act_sigs, targets={"leader_id": 0}))
    assert found == {**acts, "leader_id": 0} and why[9] == "지도자 번호의 자리: 찾은 자리가 범위 밖입니다"
    found, why, rows = toybox.acts_of(lib, installed_image(game_dir))                                    # build 21347933
    assert found == BUILD_ACTS and why == [""] * 26 and [row.count for row in rows] == [1] * 78


def installed_image(game_dir) -> bytes:
    """설치된 게임의 실행 파일을 펼친 것. 아는 빌드(21347933)가 아니면 건너뛴다."""
    exe = (game_dir / "SupremeRuler2030.exe").read_bytes()
    stamp = struct.unpack_from("<I", exe, struct.unpack_from("<I", exe, 0x3C)[0] + 8)[0]
    if stamp != BUILD_21347933_STAMP:
        pytest.skip(f"다른 빌드(TimeDateStamp {stamp:#x})")
    return toybox.image_of(exe)


def test_the_signature_table_has_three_signatures_per_address(sigs):
    assert [name for name, _ in sigs] == [name for name in toybox.STATE_FIELDS for _ in range(3)]
    assert len({text for _, text in sigs}) == 21


def test_state_is_found_in_an_image_without_any_cheat_string(lib, sigs):
    """새 찾기는 치트 문자열에 기대지 않는다 — 게임이 치트를 없애도 상태를 읽는다."""
    image = toybox_fake_exe.sig_image(sigs)
    assert b"cheat" not in image
    found, why, rows = toybox.state_of(lib, image)
    assert found == toybox_fake_exe.STATE, why
    assert [row.count for row in rows] == [1] * 21


@pytest.mark.parametrize("which", range(7))
def test_state_survives_one_broken_signature_per_address(lib, sigs, which):
    """업데이트로 서명 하나가 깨져도 나머지 둘이 같은 주소를 내면 찾은 것이다."""
    found, why, rows = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, broken={3 * which}))
    assert found == toybox_fake_exe.STATE, why
    assert rows[3 * which].count == 0


def test_state_is_not_found_when_two_signatures_of_one_address_break(lib, sigs):
    found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, broken={0, 1}))
    assert found is None and "멀티플레이 표시" in why and "3개 가운데 1개" in why


def test_a_signature_that_matches_twice_does_not_count(lib, sigs):
    found, why, rows = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, twice={0}))
    assert found == toybox_fake_exe.STATE and rows[0].count == 2, why        # 나머지 둘로 찾는다
    found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, twice={0}, broken={1}))
    assert found is None and "3개 가운데 1개" in why                          # 두 번 맞은 것은 표가 아니다


def test_signatures_that_disagree_are_refused(lib, sigs):
    """셋이 모두 맞았는데 하나가 다른 주소를 낸다 — 다수결로 고르지 않고 못 찾은 것으로 친다."""
    found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, stray={12}))
    assert found is None and "플레이어 포인터" in why and "서로 다른 주소" in why


def test_a_state_address_outside_the_image_is_refused(lib, sigs):
    targets = {"region_table": toybox_fake_exe.SIZE - 0x100}      # 지역 표(8바이트 × 1024칸)가 이미지의 끝을 넘는다
    found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, targets=targets))
    assert found is None and "지역 표" in why and "실행 파일 밖" in why


@pytest.mark.parametrize("image", GARBAGE, ids=GARBAGE_IDS)
def test_state_survives_garbage(lib, image):
    found, why, _ = toybox.state_of(lib, image)
    assert found is None and why


def test_state_survives_an_image_with_a_page_it_cannot_read(lib, sigs):
    """올라와 있는 실행 파일에 읽을 수 없는 쪽이 있어도(보호된 구역) 죽지 않고 "못 찾았다"로 친다."""
    image = toybox_fake_exe.sig_image(sigs)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.VirtualAlloc.restype = ctypes.c_void_p
    kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
    kernel32.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    kernel32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
    memory = kernel32.VirtualAlloc(None, len(image), 0x3000, 0x04)            # 예약+확정, 읽기 · 쓰기
    ctypes.memmove(memory, image, len(image))
    old = wintypes.DWORD()
    assert kernel32.VirtualProtect(memory + toybox_fake_exe.PLANT, 0x1000, 0x01, ctypes.byref(old))   # 서명이 든 쪽: PAGE_NOACCESS
    found, error = toybox.GameAddresses(), ctypes.create_string_buffer(256)
    try:
        result = lib.srtoybox_locate_state(ctypes.c_char_p(memory), len(image), ctypes.byref(found), error, len(error), None, 0)
    finally:
        kernel32.VirtualFree(memory, 0, 0x8000)
    assert result == -1 and "읽을 수 없는 곳" in error.value.decode("utf-8")


def test_state_on_the_installed_game(lib, game_dir):
    """build 21347933: 서명 21개가 저마다 실행 구역에 정확히 한 번 맞고, 읽어 낸 주소가 docs/11 의 표와 같다."""
    found, why, rows = toybox.state_of(lib, installed_image(game_dir))
    assert found == BUILD_STATE, why
    assert [row.count for row in rows] == [1] * 21
    assert all(row.value == BUILD_STATE[row.name] for row in rows)


def test_signatures_lie_outside_the_cheat_code(lib, game_dir):
    """요구 3: 서명은 치트 코드(명령 처리 함수와, 치트 코드에서만 불리는 함수들)에서 뽑지 않는다."""
    sigmine = pytest.importorskip("srkit.sigmine", reason="capstone 이 없다 (uv sync)")
    image = installed_image(game_dir)
    cheats = sigmine.cheat_ranges(sigmine.Image(image))
    assert len(cheats) == 12
    for row in (toybox.state_of(lib, image)[2] + toybox.values_of(lib, image)[2] + toybox.more_of(lib, image)[2]
                + toybox.research_of(lib, image, BUILD_STATE)[2] + toybox.acts_of(lib, image)[2]):
        assert not any(begin <= row.at < end for begin, end in cheats), row


def test_each_item_takes_its_signatures_from_different_functions(lib, game_dir):
    image = installed_image(game_dir)
    rows = (toybox.state_of(lib, image)[2] + toybox.values_of(lib, image)[2] + toybox.more_of(lib, image)[2]
            + toybox.research_of(lib, image, BUILD_STATE)[2] + toybox.acts_of(lib, image)[2])
    for i in range(0, len(rows), 3):
        roots = {lib.srtoybox_function_root(image, len(image), row.at) or -row.at for row in rows[i:i + 3]}   # 함수 표에 없으면 0
        assert len(roots) == 3, rows[i].name


def test_state_agrees_with_what_the_cheat_code_says(lib, game_dir):
    """치트 코드가 남아 있는 빌드에서의 대조: 새 찾기(서명)의 값 == 2단계의 방식(치트 닻)으로 읽은 값."""
    oracle = pytest.importorskip("toybox_cheat_oracle", reason="capstone 이 없다 (uv sync)")
    image = installed_image(game_dir)
    assert toybox.state_of(lib, image)[0] == oracle.state(image)


def test_an_address_outside_the_writable_data_is_refused(lib, sigs, value_sigs):
    """서명들이 서로 맞아도, 읽어 낸 주소가 전역 변수가 있을 수 없는 구역(코드 · 읽기 전용 자료)이면 엉뚱한 것을 읽은 것이다."""
    for target in (toybox_fake_exe.RDATA + 0x100, toybox_fake_exe.TEXT + 0x100):
        found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, targets={"player_index": target}))
        assert found is None and "플레이어 인덱스" in why and "자료 구역" in why, why
        found, why, _ = toybox.values_of(lib, toybox_fake_exe.sig_image(value_sigs, targets={"world_pointer": target}))
        assert found is None and "세계 자료 포인터" in why and "자료 구역" in why, why


def test_state_addresses_that_overlap_are_refused(lib, sigs):
    targets = {"region_count": toybox_fake_exe.STATE["player_pointer"] + 4}     # 지역 수(4바이트)가 플레이어 포인터(8바이트) 안이다
    found, why, _ = toybox.state_of(lib, toybox_fake_exe.sig_image(sigs, targets=targets))
    assert found is None and "지역 수" in why and "겹칩니다" in why, why


def test_the_value_table_has_three_signatures_per_item(lib, value_sigs):
    assert [name for name, _ in value_sigs] == [name for name in ("world_pointer", "treasury", "stock", "used") for _ in range(3)]
    assert len({text for _, text in value_sigs}) == 12
    assert lib.srtoybox_stock_slots() == toybox.STOCK_SLOTS == 12


def test_values_are_found_in_an_image_without_any_cheat_string(lib, value_sigs):
    """값의 자리도 치트 문자열에 기대지 않고 찾는다."""
    image = toybox_fake_exe.sig_image(value_sigs)
    assert b"cheat" not in image
    found, why, rows = toybox.values_of(lib, image)
    assert found == toybox_fake_exe.VALUE_LAYOUT, why
    assert [row.count for row in rows] == [1] * 12
    assert (rows[6].value, rows[6].value2) == (0x20, 0x2000)                    # 재고의 서명은 (간격, 첫 칸)을 함께 읽는다
    assert (rows[9].value, rows[9].value2) == (0x44, 0x28)


@pytest.mark.parametrize("which", range(4))
def test_values_survive_one_broken_signature_per_item(lib, value_sigs, which):
    found, why, rows = toybox.values_of(lib, toybox_fake_exe.sig_image(value_sigs, broken={3 * which + 1}))
    assert found == toybox_fake_exe.VALUE_LAYOUT, why
    assert rows[3 * which + 1].count == 0


def test_values_are_not_found_when_two_signatures_of_one_item_break(lib, value_sigs):
    found, why, _ = toybox.values_of(lib, toybox_fake_exe.sig_image(value_sigs, broken={3, 4}))
    assert found is None and "국고 칸" in why and "3개 가운데 1개" in why


def test_value_signatures_that_disagree_are_refused(lib, value_sigs):
    """셋이 모두 맞았는데 하나가 다른 자리를 낸다 — 다수결로 고르지 않는다(틀린 칸에 쓰느니 쓰지 않는다)."""
    found, why, _ = toybox.values_of(lib, toybox_fake_exe.sig_image(value_sigs, stray={7}))
    assert found is None and "재고 칸" in why and "서로 다른 값" in why


@pytest.mark.parametrize("targets, reason", [
    ({"treasury": 0x2040}, "국고 칸과 재고 칸이 겹칩니다"),      # 국고 8바이트가 재고의 셋째 칸(0x2040)을 덮는다
    ({"stock": (0x22, 0x2000)}, "재고 칸"),                     # 간격이 4 의 배수가 아니다
    ({"stock": (0x20, 0x100000)}, "재고 칸"),                   # 자리가 터무니없이 멀다
    ({"used": (0, 0x28)}, "쓰는 물자 표"),                      # 간격이 0
    ({"treasury": 0}, "국고 칸"),                               # 자리가 0
    ({"world_pointer": toybox_fake_exe.DATA + 0x44}, "세계 자료 포인터: 찾은 주소가 8 의 배수가 아닙니다"),
    ({"treasury": 0x1234}, "국고 칸: 찾은 자리가 8 의 배수가 아닙니다"),          # double 이 놓일 수 없는 자리
    ({"stock": (0x20, 0x2002)}, "재고 칸: 찾은 자리가 4 의 배수가 아닙니다"),
    ({"used": (0x44, 0x2A)}, "쓰는 물자 표: 찾은 자리가 4 의 배수가 아닙니다"),
], ids=["overlap", "odd-step", "far", "zero-step", "zero-offset", "pointer-unaligned", "treasury-unaligned", "stock-unaligned",
        "used-unaligned"])
def test_values_that_do_not_add_up_are_refused(lib, value_sigs, targets, reason):
    """서명들이 서로 맞아도 읽어 낸 자리가 말이 안 되면 못 찾은 것이다."""
    found, why, _ = toybox.values_of(lib, toybox_fake_exe.sig_image(value_sigs, targets=targets))
    assert found is None and reason in why, why


def test_the_world_pointer_must_not_sit_on_a_state_global(lib, sigs, value_sigs):
    """두 묶음을 저마다 찾았어도, 세계 자료 포인터가 상태 전역과 겹치면 둘 가운데 하나는 엉뚱한 것을 읽은 것이다 — 값 묶음을 버린다."""
    clash = {"world_pointer": toybox_fake_exe.STATE["player_pointer"]}
    image = toybox_fake_exe.sig_image(sigs + value_sigs, targets=clash)
    state, why, _ = toybox.state_of(lib, image)
    assert state == toybox_fake_exe.STATE, why
    values, why, _ = toybox.values_of(lib, image)
    assert values == {**toybox_fake_exe.VALUE_LAYOUT, **clash}, why          # 저마다는 말이 된다
    assert toybox.fits(lib, state, values) == "세계 자료 포인터: 찾은 주소가 플레이어 포인터 의 자리와 겹칩니다"
    assert toybox.fits(lib, state, toybox_fake_exe.VALUE_LAYOUT) == ""
    inside = {**toybox_fake_exe.VALUE_LAYOUT, "world_pointer": toybox_fake_exe.STATE["region_table"] + 0x800}
    assert "지역 표" in toybox.fits(lib, state, inside)                       # 지역 표(8바이트 × 1024칸)의 한가운데
    edge = {**toybox_fake_exe.VALUE_LAYOUT, "world_pointer": toybox_fake_exe.STATE["player_pointer"] + 8}
    assert toybox.fits(lib, state, edge) == ""                               # 바로 옆은 겹침이 아니다


@pytest.mark.parametrize("image", GARBAGE, ids=GARBAGE_IDS)
def test_values_survive_garbage(lib, image):
    found, why, _ = toybox.values_of(lib, image)
    assert found is None and why


def test_values_on_the_installed_game(lib, game_dir):
    """build 21347933: 값 묶음의 서명 12개가 저마다 실행 구역에 정확히 한 번 맞고, 읽어 낸 값이 docs/11 의 표와 같다."""
    found, why, rows = toybox.values_of(lib, installed_image(game_dir))
    assert found == BUILD_VALUES, why
    assert [row.count for row in rows] == [1] * 12


def test_values_agree_with_what_the_cheat_code_says(lib, game_dir):
    """치트 코드가 남아 있는 빌드에서의 대조: 서명으로 읽은 값 == 치트 treasury · products 의 본문에서 읽은 값. 칸 수도 거기서 센다."""
    oracle = pytest.importorskip("toybox_cheat_oracle", reason="capstone 이 없다 (uv sync)")
    image = installed_image(game_dir)
    said = oracle.values(image)
    assert said.pop("slots") == toybox.STOCK_SLOTS
    assert toybox.values_of(lib, image)[0] == said


def more_without(group: int) -> dict[str, int]:
    """가짜 이미지의 "더 쓰는 값"에서 그 묶음(0 지식, 1 여론, 2 관계)만 못 찾았을 때의 결과."""
    return {**toybox_fake_exe.MORE, **dict.fromkeys(toybox.MORE_GROUPS[group][1], 0)}


def test_the_more_table_has_three_signatures_per_item(more_sigs):
    assert [name for name, _ in more_sigs] == [name for name in toybox.MORE_FIELDS for _ in range(3)]
    assert len({text for _, text in more_sigs}) == 30
    assert [field for _, fields in toybox.MORE_GROUPS for field in fields] == toybox.MORE_FIELDS


def test_more_is_found_in_an_image_without_any_cheat_string(lib, more_sigs):
    """기술 수준 · 여론 · 관계의 자리도 치트 문자열에 기대지 않고 찾는다."""
    image = toybox_fake_exe.sig_image(more_sigs)
    assert b"cheat" not in image
    found, why, rows = toybox.more_of(lib, image)
    assert found == toybox_fake_exe.MORE and why == [""] * 5
    assert [row.count for row in rows] == [1] * 30
    assert all(row.value == toybox_fake_exe.MORE[row.name] for row in rows)


@pytest.mark.parametrize("which", range(7))
def test_more_survives_one_broken_signature_per_item(lib, more_sigs, which):
    found, why, rows = toybox.more_of(lib, toybox_fake_exe.sig_image(more_sigs, broken={3 * which + 2}))
    assert found == toybox_fake_exe.MORE and why == [""] * 5
    assert rows[3 * which + 2].count == 0


@pytest.mark.parametrize("item, group, label", [(0, 0, "기술 수준 칸"), (2, 1, "여론 칸 2"), (6, 2, "전쟁 명분 표")],
                         ids=["tech", "opinion", "relations"])
def test_a_group_that_cannot_find_one_of_its_items_is_dropped_alone(lib, more_sigs, item, group, label):
    """묶음마다 따로 찾는다: 한 묶음의 한 자리를 못 찾으면 그 묶음만 통째로 버리고(반쪽 묶음은 없다) 나머지 묶음은 그대로 찾는다."""
    found, why, _ = toybox.more_of(lib, toybox_fake_exe.sig_image(more_sigs, broken={3 * item, 3 * item + 1}))
    assert found == more_without(group)
    assert [bool(text) for text in why] == [g == group for g in range(5)]
    assert label in why[group] and "3개 가운데 1개" in why[group]


def test_more_signatures_that_disagree_drop_that_group(lib, more_sigs):
    """셋이 모두 맞았는데 하나가 다른 자리를 낸다 — 다수결로 고르지 않는다(틀린 칸에 쓰느니 쓰지 않는다)."""
    found, why, _ = toybox.more_of(lib, toybox_fake_exe.sig_image(more_sigs, stray={13}))      # 관계 표 1 의 둘째 서명
    assert found == more_without(2)
    assert why[:2] == ["", ""] and "관계 표 1" in why[2] and "서로 다른 값" in why[2]


@pytest.mark.parametrize("targets, dropped, reason", [
    ({"tech": 0x3122}, [0], "기술 수준 칸: 찾은 자리가 4 의 배수가 아닙니다"),
    ({"opinion1": 0}, [1], "여론 칸 2: 찾은 자리가 범위 밖입니다"),
    ({"casus": 0xFFFFC}, [2], "전쟁 명분 표: 찾은 자리가 범위 밖입니다"),                    # 표의 끝이 범위를 넘는다
    ({"opinion2": 0x3004}, [1], "여론 칸 3: 찾은 자리가 여론 칸 1 의 자리와 겹칩니다"),       # 세 칸은 서로 달라야 한다
    ({"relation1": 0x4800}, [2], "관계 표 2: 찾은 자리가 관계 표 1 의 자리와 겹칩니다"),      # 표 하나는 4바이트 × 1024칸이다
    ({"tech": 0x4010}, [0, 2], "관계 표 1: 찾은 자리가 기술 수준 칸 의 자리와 겹칩니다"),     # 어느 쪽이 틀렸는지 모른다 — 둘 다 버린다
], ids=["unaligned", "zero", "table-runs-out", "same-cell", "tables-too-close", "cell-inside-a-table"])
def test_more_that_does_not_add_up_is_dropped(lib, more_sigs, targets, dropped, reason):
    """서명들이 서로 맞아도 읽어 낸 자리가 말이 안 되면 그 묶음은 못 찾은 것이다."""
    found, why, _ = toybox.more_of(lib, toybox_fake_exe.sig_image(more_sigs, targets=targets))
    expected = {**toybox_fake_exe.MORE, **targets}
    for group in dropped:
        expected.update(dict.fromkeys(toybox.MORE_GROUPS[group][1], 0))
    assert found == expected
    assert [text for text in why if text] == [reason] * len(dropped) and [bool(text) for text in why] == [g in dropped for g in range(5)]


def test_more_must_not_sit_on_the_treasury_or_a_stock_slot(lib, more_sigs):
    """값 묶음(돈 · 물자)을 찾았으면 그 칸들과 겹치는 새 묶음은 버린다 — 둘 가운데 하나는 엉뚱한 것을 읽었다. 값 묶음은 그대로 둔다."""
    targets = {"tech": 0x1234, "opinion0": 0x2040}         # 국고 칸(0x1230, 8바이트)의 뒤쪽 절반 / 재고의 셋째 칸
    image = toybox_fake_exe.sig_image(more_sigs, targets=targets)
    found, why, _ = toybox.more_of(lib, image)
    assert found == {**toybox_fake_exe.MORE, **targets} and why == [""] * 5      # 값 묶음을 모르면 저마다는 말이 된다
    found, why, _ = toybox.more_of(lib, image, toybox_fake_exe.VALUE_LAYOUT)
    assert found == {**more_without(0), **dict.fromkeys(toybox.MORE_GROUPS[1][1], 0)}
    assert why == ["기술 수준 칸: 찾은 자리가 국고 칸이나 재고 칸과 겹칩니다", "여론 칸 1: 찾은 자리가 국고 칸이나 재고 칸과 겹칩니다", "", "", ""]
    assert toybox.more_of(lib, toybox_fake_exe.sig_image(more_sigs), toybox_fake_exe.VALUE_LAYOUT)[1] == [""] * 5


def test_more_is_found_beside_the_other_tables(lib, sigs, value_sigs, more_sigs):
    """세 서명 표가 한 이미지에 있어도 저마다 찾는다(서명이 서로의 자리에 맞지 않는다)."""
    image = toybox_fake_exe.sig_image(sigs + value_sigs + more_sigs)
    assert toybox.state_of(lib, image)[0] == toybox_fake_exe.STATE
    assert toybox.values_of(lib, image)[0] == toybox_fake_exe.VALUE_LAYOUT
    assert toybox.more_of(lib, image, toybox_fake_exe.VALUE_LAYOUT)[:2] == (toybox_fake_exe.MORE, [""] * 5)


@pytest.mark.parametrize("image", GARBAGE, ids=GARBAGE_IDS)
def test_more_survives_garbage(lib, image):
    found, why, _ = toybox.more_of(lib, image)
    assert found == dict.fromkeys(toybox.MORE_FIELDS, 0) and all(why)


def test_more_on_the_installed_game(lib, game_dir):
    """build 21347933: 서명 30개가 저마다 실행 구역에 정확히 한 번 맞고, 읽어 낸 자리가 docs/11 의 표와 같다."""
    found, why, rows = toybox.more_of(lib, installed_image(game_dir), BUILD_VALUES)
    assert found == BUILD_MORE and why == [""] * 5
    assert [row.count for row in rows] == [1] * 30
    assert all(row.value == BUILD_MORE[row.name] for row in rows)


def test_more_agrees_with_what_the_cheat_code_says(lib, game_dir):
    """치트 코드가 남아 있는 빌드에서의 대조: 서명으로 읽은 자리 == 치트 finalexam · shelovesme · love · neutral 의 본문이 쓰는 자리."""
    oracle = pytest.importorskip("toybox_cheat_oracle", reason="capstone 이 없다 (uv sync)")
    image = installed_image(game_dir)
    found, cheat = toybox.more_of(lib, image)[0], oracle.more(image)
    assert {name: found[name] for name in cheat} == cheat      # 인구의 풀 칸은 치트가 쓰지 않는 칸이라 댈 것이 없다


def research(lib, image: bytes, state: dict | None = toybox_fake_exe.STATE):
    """연구의 찾기를 그 이미지에 돌린다. state 는 이미 찾은 상태 묶음(가짜 이미지의 것) — 서명을 심지 않아도 값만 넘기면 된다."""
    return toybox.research_of(lib, image, state)


def test_the_research_table_has_three_signatures_per_item(research_sigs):
    assert [name for name, _ in research_sigs] == [name for name in toybox.RESEARCH_FIELDS for _ in range(3)]
    assert len({text for _, text in research_sigs}) == 21


def test_research_is_found_in_an_image_without_any_cheat_string(lib, research_sigs):
    """연구의 표 · 목록 · 다시 셈 함수도 치트 문자열에 기대지 않고 찾는다."""
    image = toybox_fake_exe.sig_image(research_sigs)
    assert b"cheat" not in image
    found, why, rows = research(lib, image)
    assert found == toybox_fake_exe.RESEARCH_LAYOUT, why
    assert [row.count for row in rows] == [1] * 21
    assert [(row.value, row.value2) for row in rows if row.name == "world"] == [toybox_fake_exe.RESEARCH["world"]] * 3   # 주소 · 지역 표까지의 거리
    assert all(row.value == toybox_fake_exe.RESEARCH_LAYOUT[row.name] for row in rows)


@pytest.mark.parametrize("which", range(21))
def test_research_needs_all_three_signatures_of_every_item(lib, research_sigs, which):
    """다른 묶음과 다르다: 서명 하나만 깨져도 못 찾은 것이다 — 표와 목록의 꼴(레코드의 크기 · 칸의 자리)이 서명마다 박혀 있어,
    하나라도 맞지 않으면 꼴이 바뀐 것일 수 있다. 틀린 꼴로 모든 나라가 함께 쓰는 표에 쓰느니 쓰지 않는다."""
    found, why, rows = research(lib, toybox_fake_exe.sig_image(research_sigs, broken={which}))
    assert found is None and "3개 가운데 2개" in why
    assert rows[which].count == 0 and sum(row.count for row in rows) == 20


def test_a_research_signature_that_matches_twice_does_not_count(lib, research_sigs):
    found, why, rows = research(lib, toybox_fake_exe.sig_image(research_sigs, twice={6}))     # 부대 설계 표의 첫 서명
    assert found is None and rows[6].count == 2 and "부대 설계 표: 서명 3개 가운데 2개" in why


def test_research_signatures_that_disagree_are_refused(lib, research_sigs):
    found, why, _ = research(lib, toybox_fake_exe.sig_image(research_sigs, stray={13}))       # 세계 객체의 둘째 서명
    assert found is None and why == "세계 객체: 서명들이 서로 다른 값을 냅니다"



@pytest.mark.parametrize("targets, reason", [
    ({"tech_table": FAKE.DATA + 0x4C}, "기술 표: 찾은 주소가 8 의 배수가 아닙니다"),
    ({"tech_table": FAKE.SIZE + 0x100}, "기술 표: 찾은 주소가 실행 파일 밖입니다"),
    ({"design_table": FAKE.RDATA + 0x100}, "부대 설계 표: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다"),
    ({"tech_count": FAKE.DATA + 0x48}, "기술 수: 찾은 주소가 기술 표 의 자리와 겹칩니다"),
    ({"design_count": FAKE.STATE["player_index"]}, "부대 설계 수: 찾은 주소가 플레이어 인덱스 의 자리와 겹칩니다"),
    ({"world": (FAKE.WORLD, 0x88)}, "세계 객체: 지역 표까지의 거리가 맞지 않습니다"),
    ({"world": (FAKE.RDATA + 0x100, FAKE.STATE["region_table"] - FAKE.RDATA - 0x100)}, "세계 객체: 찾은 주소가 쓸 수 있는 자료 구역이 아닙니다"),
    ({"lists": 0x2104}, "연구 목록: 찾은 자리가 범위 밖입니다"),                           # 포인터가 든 칸은 8 의 배수 자리다
    ({"lists": 0x9000}, "연구 목록: 찾은 자리가 범위 밖입니다"),                           # 24바이트 × 1024칸이 자료 구역을 넘는다
    ({"lists": 0x1000}, "연구 목록: 찾은 자리가 지역 표 의 자리와 겹칩니다"),
    ({"recompute": FAKE.RECOMPUTE + 4}, "다시 셈 함수: 찾은 주소가 함수의 시작이 아닙니다"),
    ({"recompute": FAKE.DATA + 0x400}, "다시 셈 함수: 찾은 주소가 함수의 시작이 아닙니다"),   # 코드가 아니다
], ids=["unaligned", "outside", "read-only", "on-another-item", "on-a-state-global", "wrong-distance", "world-not-data",
        "list-unaligned", "list-runs-out", "list-on-the-region-table", "inside-a-function", "not-code"])
def test_research_that_does_not_add_up_is_refused(lib, research_sigs, targets, reason):
    """서명 셋이 모두 맞아도 읽어 낸 값이 말이 안 되면 못 찾은 것이다."""
    found, why, _ = research(lib, toybox_fake_exe.sig_image(research_sigs, targets=targets))
    assert found is None and why == reason


def test_research_checks_the_world_object_against_the_region_table_found_elsewhere(lib, research_sigs):
    """세계 객체의 서명은 "지역 표까지의 거리"도 읽는다. 세계 객체 + 그 거리가 상태 묶음이 따로 찾은 지역 표가 아니면 엉뚱한 객체다."""
    image = toybox_fake_exe.sig_image(research_sigs)
    moved = {**toybox_fake_exe.STATE, "region_table": toybox_fake_exe.STATE["region_table"] + 8}
    assert research(lib, image, moved)[:2] == (None, "세계 객체: 지역 표까지의 거리가 맞지 않습니다")
    assert research(lib, image, None)[:2] == (None, "세계 객체: 지역 표까지의 거리가 맞지 않습니다")     # 상태 묶음을 못 찾았다


def test_research_is_found_beside_the_state_table(lib, sigs, research_sigs):
    """상태의 서명과 한 이미지에 있어도 저마다 찾는다. 연구의 서명 하나가 깨지면 연구만 못 찾는다."""
    image = toybox_fake_exe.sig_image(sigs + research_sigs)
    state = toybox.state_of(lib, image)[0]
    assert state == toybox_fake_exe.STATE and research(lib, image, state)[0] == toybox_fake_exe.RESEARCH_LAYOUT
    image = toybox_fake_exe.sig_image(sigs + research_sigs, broken={21 + 20})
    assert toybox.state_of(lib, image)[0] == toybox_fake_exe.STATE and research(lib, image)[0] is None


def test_research_must_not_sit_on_the_world_pointer_of_the_values(lib):
    """연구 묶음과 값 묶음을 저마다 찾았어도, 연구의 전역이 세계 자료 포인터와 겹치면 어느 쪽이 엉뚱한 것을 읽었는지 알 수 없다 —
    연구 묶음을 버린다(값 묶음은 그대로 쓴다). 바로 옆은 겹침이 아니다."""
    found, values = toybox_fake_exe.RESEARCH_LAYOUT, toybox_fake_exe.VALUE_LAYOUT
    assert toybox.research_fits(lib, found, values) == ""
    for name, label in (("tech_table", "기술 표"), ("tech_count", "기술 수"), ("design_table", "부대 설계 표")):
        clash = {**values, "world_pointer": found[name]}
        assert toybox.research_fits(lib, found, clash) == f"{label}: 찾은 주소가 세계 자료 포인터 의 자리와 겹칩니다"
    # 가짜 이미지에서 부대 설계 수(4바이트) 바로 뒤가 부대 설계 표다 — 8바이트 포인터는 둘에 걸치고, 표의 순서로 먼저인 것을 든다
    assert toybox.research_fits(lib, found, {**values, "world_pointer": found["design_count"]}).endswith("세계 자료 포인터 의 자리와 겹칩니다")
    assert toybox.research_fits(lib, found, {**values, "world_pointer": found["tech_table"] - 4}) != ""          # 끝이 걸친다
    assert toybox.research_fits(lib, found, {**values, "world_pointer": found["tech_table"] + 0x100}) == ""


@pytest.mark.parametrize("image", GARBAGE, ids=GARBAGE_IDS)
def test_research_survives_garbage(lib, image):
    found, why, _ = research(lib, image)
    assert found is None and why


def test_research_on_the_installed_game(lib, game_dir):
    """build 21347933: 서명 21개가 저마다 실행 구역에 정확히 한 번 맞고, 읽어 낸 값이 docs/11 의 표와 같다."""
    found, why, rows = toybox.research_of(lib, installed_image(game_dir), BUILD_STATE)
    assert found == BUILD_RESEARCH, why
    assert [row.count for row in rows] == [1] * 21
    assert all(row.value == BUILD_RESEARCH[row.name] for row in rows)
    assert {row.value2 for row in rows if row.name == "world"} == {BUILD_STATE["region_table"] - BUILD_RESEARCH["world"]}


def test_the_shape_of_the_research_tables_is_pinned_in_the_signatures(lib, research_sigs):
    """표와 목록의 꼴은 읽어 내지 않고 상수로 둔다(locate.h). 그 상수가 서명의 명령에 그대로 박혀 있어야 한다 —
    꼴이 바뀐 빌드에서 서명이 맞지 않게. DLL 의 상수를 고치면서 서명을 그대로 두면 여기서 걸린다."""
    shape = toybox.research_shape(lib)
    assert shape["need"] == 3 and shape["list_step"] == 24 and shape["tech_kind"] == 0 and shape["design_name"] == 0
    assert shape["tech_need_count"] == 2 and shape["node_gone"] | shape["node_ended"] == 0x88000000
    texts = {(name, i % 3): text for i, (name, text) in enumerate(research_sigs)}
    pinned = set()

    def fill(m) -> str:
        pinned.add(m[1])
        value = shape[m[1]] + int(m[2] or 0)
        return " ".join(f"{b:02X}" for b in value.to_bytes(1 if m[3] == "b" else 4, "little"))

    for item, which, template in RESEARCH_PINS:
        wanted = re.sub(r"\{([a-z_]+)(\+\d+)?:([bd])\}", fill, template)
        assert wanted in texts[item, which], (item, which, wanted)
    assert pinned == RESEARCH_PINNED
    assert {item for item, _, _ in RESEARCH_PINS} == set(toybox.RESEARCH_FIELDS) - {"world"}     # 세계 객체는 거리를 읽어 견준다


def test_research_agrees_with_what_the_cheat_code_says(lib, game_dir):
    """치트 코드가 남아 있는 빌드에서의 대조: 서명으로 찾은 것과 DLL 이 아는 꼴 == 치트 technology 의 본문이 쓰는 것."""
    oracle = pytest.importorskip("toybox_cheat_oracle", reason="capstone 이 없다 (uv sync)")
    image = installed_image(game_dir)
    said = oracle.research(image)
    found, shape = toybox.research_of(lib, image, BUILD_STATE)[0], toybox.research_shape(lib)
    assert {name: found[name] for name in ("world", "tech_count", "tech_table", "lists", "recompute")} == \
        {name: said[name] for name in ("world", "tech_count", "tech_table", "lists", "recompute")}
    names = ("tech_size", "tech_level", "tech_owners", "owners_bytes", "node_kind", "node_id", "node_next")
    assert {name: shape[name] for name in names} == {name: said[name] for name in names}


def test_legacy_finds_the_handler_from_the_cheat_anchor(lib, sigs):
    """옛 찾기(검증 도구 · 대조용): 명령 처리 함수와 그 둘레의 셋만 치트 문자열을 닻으로 찾는다."""
    assert toybox.legacy_of(lib, toybox_fake_exe.build(sigs)) == (toybox_fake_exe.LEGACY, "")


@pytest.mark.parametrize("flaw, reason", [
    ("extra_anchor", "닻 문자열"),                   # 같은 문자열이 둘이면 어느 것이 진짜인지 모른다
    ("second_call", "부르는 곳"),                    # 부르는 곳이 둘이면 this 를 어디서 얻을지 모른다
    ("no_options", "치트 허용 비트"),                # 함수 머리의 모양이 다르다 = 다른 함수이거나 바뀐 빌드
    ("outside", "실행 파일 밖"),                     # 찾은 주소가 이미지 밖이다
])
def test_legacy_refuses_an_image_that_does_not_add_up(lib, flaw, reason):
    found, why = toybox.legacy_of(lib, toybox_fake_exe.build(**{flaw: True}))
    assert found is None and reason in why, why


def test_an_image_without_cheats_gives_the_state_but_not_the_handler(lib, sigs):
    """게임이 치트를 없앤 빌드: 상태는 읽고(새 찾기) 명령 처리 함수는 못 찾는다(옛 찾기) — 옮기지 않은 기능만 글쇠 방식이 된다."""
    image = toybox_fake_exe.sig_image(sigs)
    assert toybox.state_of(lib, image)[0] == toybox_fake_exe.STATE
    found, why = toybox.legacy_of(lib, image)
    assert found is None and "닻 문자열" in why


def test_an_image_with_both_gives_both(lib, sigs):
    """치트의 닻과 서명이 함께 든 이미지(지금의 게임): 두 찾기가 서로를 방해하지 않는다."""
    image = toybox_fake_exe.build(sigs)
    assert toybox.state_of(lib, image)[0] == toybox_fake_exe.STATE
    assert toybox.legacy_of(lib, image)[0] == toybox_fake_exe.LEGACY


@pytest.mark.parametrize("image", GARBAGE, ids=GARBAGE_IDS)
def test_legacy_survives_garbage(lib, image):
    found, why = toybox.legacy_of(lib, image)
    assert found is None and why


def test_legacy_survives_an_image_with_a_page_it_cannot_read(lib):
    image = toybox_fake_exe.build()
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.VirtualAlloc.restype = ctypes.c_void_p
    kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
    kernel32.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    kernel32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
    memory = kernel32.VirtualAlloc(None, len(image), 0x3000, 0x04)            # 예약+확정, 읽기 · 쓰기
    ctypes.memmove(memory, image, len(image))
    old = wintypes.DWORD()
    assert kernel32.VirtualProtect(memory + toybox_fake_exe.TEXT, 0x1000, 0x01, ctypes.byref(old))   # 코드의 첫 쪽: PAGE_NOACCESS
    found, error = toybox.GameAddresses(), ctypes.create_string_buffer(256)
    try:
        result = lib.srtoybox_locate_legacy(ctypes.c_char_p(memory), len(image), ctypes.byref(found), error, len(error))
    finally:
        kernel32.VirtualFree(memory, 0, 0x8000)
    assert result == -1 and "읽을 수 없는 곳" in error.value.decode("utf-8")


def test_legacy_on_the_installed_game(lib, game_dir):
    assert toybox.legacy_of(lib, installed_image(game_dir)) == (BUILD_LEGACY, "")


def test_srkit_locate_reports_every_search(lib, cfg, game_dir):
    located = toybox.locate(cfg)
    assert located.state is not None and set(located.state) == set(toybox.STATE_FIELDS), located.state_why
    assert len(located.rows) == 21 and located.ms >= 0
    assert located.values is not None and set(located.values) == set(toybox.VALUE_FIELDS), located.values_why
    assert len(located.value_rows) == 12
    assert located.legacy is not None and set(located.legacy) == set(toybox.LEGACY_FIELDS), located.legacy_why
    assert set(located.more) == set(toybox.MORE_FIELDS) and all(located.more.values()) and located.more_why == [""] * 5
    assert len(located.more_rows) == 30
    assert located.research is not None and set(located.research) == set(toybox.RESEARCH_FIELDS), located.research_why
    assert len(located.research_rows) == 21


def state(lib, fake: FakeGame) -> dict[str, str]:
    out = ctypes.create_string_buffer(8192)
    assert lib.srtoybox_game_state(fake.base, ctypes.byref(fake.at), out, len(out)) >= 0
    return dict(pair.split("=", 1) for pair in out.value.decode().split())


def germany() -> FakeGame:
    """독일(인덱스 176, 번호 1499)로 진행 중인 게임. 폴란드(141, 1106)와 덴마크(150, 1201)가 있다."""
    fake = FakeGame()
    fake.region(141, 1106, alive=3)
    fake.region(150, 1201)
    fake.region(176, 1499)
    fake.play(176)
    return fake


def test_game_state_in_a_game(lib):
    assert state(lib, germany()) == {"known": "1", "in_game": "1", "multiplayer": "0", "player": "1499",
                                     "regions": "1106,1201,1499"}


def test_game_state_outside_a_game(lib):
    """메뉴 · 로비: 플레이어 포인터가 비어 있다. 인덱스에 낡은 값이 남아 있어도 진행 중이 아니다."""
    fake = germany()
    fake.menu()
    assert state(lib, fake) == {"known": "1", "in_game": "0", "multiplayer": "0", "player": "0", "regions": ""}


def test_game_state_in_a_lobby_that_already_has_a_player_pointer(lib):
    """캠페인의 로비에서는 메뉴 상태(프로그램 3 / 모드 1)인데 플레이어 포인터가 이미 차 있다(샌드박스의 로비에서는 비어 있다)
    [확인: 실행, build 21347933]. 포인터만 보고 "진행 중"이라고 하면 안 된다 — 게임의 함수는 그때 불러도 되는지 검사하지 않는다."""
    fake = germany()
    fake.poke(PROGRAM, "<i", 3)
    fake.poke(MODE, "<i", 1)
    assert state(lib, fake) == {"known": "1", "in_game": "0", "multiplayer": "0", "player": "0", "regions": ""}


def test_game_state_reads_the_multiplayer_flag_and_not_the_cheat_bit(lib):
    """멀티플레이 표시는 읽는다. 치트 허용 비트는 읽지 않는다 — ToyBox 는 내장 치트를 쓰지 않는다(그 비트가 어떻든 상태는 같다)."""
    fake = germany()
    fake.poke(MULTIPLAYER, "<B", 1)
    got = state(lib, fake)
    assert got["multiplayer"] == "1" and "cheats" not in got
    fake.poke(OPTIONS, "<I", 0x6072B0C1)            # 치트 허용(비트 0x40)이 켜진 값
    assert state(lib, fake) == got


def test_game_state_without_the_legacy_addresses(lib):
    """상태를 읽는 데에 옛 찾기의 주소(옵션 묶음 · 명령 처리 함수)는 쓰이지 않는다 — 실행 중인 ToyBox 는 옛 찾기를 돌리지 않는다."""
    fake = germany()
    fake.at.options = 0
    fake.at.handler = 0
    fake.at.context = 0
    fake.poke(OPTIONS, "<I", 0x40)
    assert state(lib, fake) == {"known": "1", "in_game": "1", "multiplayer": "0", "player": "1499",
                                "regions": "1106,1201,1499"}


def test_game_state_survives_a_pointer_it_cannot_read(lib):
    """낡은 포인터를 만나도 죽지 않는다. 읽을 수 없으면 모르는 것으로 친다(→ 글쇠 방식)."""
    fake = germany()
    for address in (0x00007FFFFFFF0000, 0x8000000000000000, 8):
        fake.poke(POINTER, "<Q", address)
        fake.poke(TABLE + 8 * 176, "<Q", address)
        assert state(lib, fake)["known"] == "0"


@pytest.mark.parametrize("flaw", ["table", "number", "dead", "index", "self_index"])
def test_game_state_that_does_not_add_up_is_unknown(lib, flaw):
    """게임 안인데 읽은 값이 서로 맞지 않으면 구조가 바뀐 빌드일 수 있다 — 모르는 것으로 친다."""
    fake = germany()
    if flaw == "table":
        fake.poke(TABLE + 8 * 176, "<Q", fake.peek(TABLE + 8 * 141, "<Q"))    # 표[인덱스] 가 플레이어 포인터와 다르다
    elif flaw == "number":
        struct.pack_into("<H", fake.objects[176], 8, 0)                       # 지역 번호가 0
    elif flaw == "dead":
        struct.pack_into("<I", fake.objects[176], 0, 0)                       # 죽은 지역
    elif flaw == "index":
        fake.poke(INDEX, "<i", 2000)                                          # 표 밖의 인덱스
    else:
        struct.pack_into("<H", fake.objects[176], 4, 175)                     # 객체가 아는 자기 인덱스가 다르다
    got = state(lib, fake)
    assert got["known"] == "0" and got["in_game"] == "0"


def test_regions_skip_empty_dead_and_inconsistent_slots(lib):
    fake = germany()
    fake.region(10, 500, alive=0)                                             # 죽은 지역
    fake.region(11, 501)
    struct.pack_into("<H", fake.objects[11], 4, 12)                           # 자기 인덱스가 칸과 다르다
    fake.region(12, 0)                                                        # 번호가 없다
    fake.poke(TABLE + 8 * 13, "<Q", 0x00007FFFFFFF0000)                       # 읽을 수 없는 포인터
    fake.region(20, 1403, alive=5)                                            # 이번 판에 없는 지역(남독일 같은 것)
    fake.region(21, 2922, alive=5)
    fake.region(22, 7, alive=1)                                               # 유엔 같은 특수한 것
    assert state(lib, fake)["regions"] == "1106,1201,1499"                    # 사람(2)과 AI(3)가 맡은 나라만


def label(lib, number: int) -> str:
    out = ctypes.create_string_buffer(256)
    assert lib.srtoybox_region_label(number, out, len(out)) >= 0
    return out.value.decode("utf-8")


def test_region_names(lib):
    """이름표는 저장소의 번역 테이블에서 온다. 표에 없는 번호는 #번호 로 보인다."""
    assert label(lib, 1499) == "독일" and label(lib, 1106) == "폴란드"
    assert label(lib, 109) == "수에즈 운하 지대"          # localtext-regions.extra.csv 에만 있는 지역
    assert label(lib, 12345) == "#12345" and label(lib, -7) == "#-7"


def product(lib, slot: int) -> str:
    out = ctypes.create_string_buffer(256)
    assert lib.srtoybox_product_label(slot, out, len(out)) >= 0
    return out.value.decode("utf-8")


def test_product_names(lib):
    """물자 이름표도 저장소의 번역 테이블에서 온다(재고 칸의 순서 = 게임의 물자 목록의 순서). 표에 없는 칸은 "물자 #칸" 으로 보인다."""
    assert [product(lib, slot) for slot in range(11)] == ["농산물", "고무", "목재", "석유", "석탄", "금속 광석", "우라늄", "전력", "소비재",
                                                           "산업재", "군수품"]
    assert product(lib, 11) == "물자 #11" and product(lib, 12) == "물자 #12" and product(lib, -1) == "물자 #-1"   # 열두째 칸에는 이름이 없다
    out = ctypes.create_string_buffer(256)
    assert lib.srtoybox_product_names(3, out, len(out)) > 0 and out.value.decode("utf-8") == "석유\tPetroleum"
    assert lib.srtoybox_product_names(11, out, len(out)) == -1


def test_product_rows_come_from_the_translation_table(cfg):
    rows = toybox.product_rows(cfg)
    assert [slot for slot, _, _ in rows] == list(range(11))                  # 같은 목록의 11(전체) · 12(금융) · 13(인구) … 는 물자가 아니다
    assert rows[3] == (3, "석유", "Petroleum") and rows[10] == (10, "군수품", "Military Goods")
    assert toybox.products_inc(rows[:1]) == '{0, "농산물", "Agriculture"},\n'


def test_region_rows_come_from_the_translation_table(cfg):
    rows = toybox.region_rows(cfg)
    assert [n for n, _, _ in rows] == sorted({n for n, _, _ in rows}) and len(rows) > 300
    assert (1499, "독일", "Germany") in rows
    assert (109, "수에즈 운하 지대", "Suez Canal Zone") in rows      # 덧붙인 표(.extra)의 지역도 들어간다
    assert all(ko and en for _, ko, en in rows)


def test_alias_rows_know_the_names_the_2030_world_uses(cfg):
    """2030 - 세계에서 게임은 몇 나라를 지역 데이터의 영문 이름으로 부른다(번역 테이블의 영문과 다르다 — 2026-10-19 게임에서 봤다).
    그 이름도 한글로 바꾼다(mods/korean/region-aliases.csv)."""
    rows = dict(toybox.alias_rows(cfg))
    assert rows["Dem Rep Yemen"] == "남예멘" and rows["RS Forces"] == "신속지원군"
    assert rows["Virgin Isles"] == "버진아일랜드" and rows["Western Sahara"] == "서사하라"
    assert rows["Germany"] == "독일"                              # 번역 테이블의 이름이 그대로 앞선다


def test_region_table_escapes_names():
    """이름에 따옴표나 역슬래시가 있어도 생성물이 C++ 문자열로 컴파일되어야 한다."""
    assert toybox.c_string('Côte d\'Ivoire') == '"Côte d\'Ivoire"'
    assert toybox.c_string('a"b') == '"a\\"b"'
    assert toybox.c_string("a\\b") == '"a\\\\b"'
    assert toybox.regions_inc([(7, 'x"y', "z\\")]) == '{7, "x\\"y", "z\\\\"},\n'


def view(lib, numbers: list[int], player: int, picked: int, search: str = "") -> tuple[int, list[tuple[int, str]]]:
    """창의 나라 목록: (유지된 선택, [(번호, 이름) …])."""
    out = ctypes.create_string_buffer(1 << 16)
    array = (ctypes.c_int * len(numbers))(*numbers)
    assert lib.srtoybox_region_view(array, len(numbers), player, picked, search.encode("utf-8"), out, len(out)) >= 0
    head, *rows = out.value.decode("utf-8").splitlines()
    return int(head.removeprefix("picked=")), [(int(n), name) for n, name in (row.split("\t") for row in rows)]


GAME = [1106, 1201, 1499, 12345]      # 폴란드, 덴마크, 독일, 이름표에 없는 지역


def test_region_view_lists_this_games_regions(lib):
    """이번 게임에 있는 나라만, 플레이어 자신은 빼고, 한글 이름순으로. 이름표에 없는 번호는 #번호 로 보이고 고를 수 있다."""
    picked, rows = view(lib, GAME, player=1499, picked=12345)
    assert rows == [(12345, "#12345"), (1201, "덴마크"), (1106, "폴란드")]
    assert picked == 12345


def test_region_view_filters_by_name_and_number(lib):
    assert [n for n, _ in view(lib, GAME, 1499, 0, "폴란")[1]] == [1106]
    assert [n for n, _ in view(lib, GAME, 1499, 0, "DEN")[1]] == [1201]            # 영문 이름, 대소문자를 가리지 않는다
    assert [n for n, _ in view(lib, GAME, 1499, 0, "12")[1]] == [12345, 1201]       # 번호의 일부
    assert view(lib, GAME, 1499, 0, "없는이름")[1] == []
    assert view(lib, GAME, 1499, 1106, "덴마")[0] == 1106                           # 검색으로 가려져도 선택은 그대로다


def test_region_view_drops_a_pick_that_is_gone(lib):
    """다른 판을 불러와 고른 나라가 없어졌거나, 그 나라로 플레이하게 됐으면 선택을 지운다."""
    assert view(lib, [1201, 1499], player=1499, picked=1106)[0] == 0
    assert view(lib, GAME, player=1106, picked=1106)[0] == 0
    assert view(lib, [], player=0, picked=1106) == (0, [])


def test_two_byte_characters_are_put_back_together(lib):
    """게임 창은 ANSI 창이라 한글 한 글자가 WM_CHAR 두 번(CP949 의 앞 · 뒤 바이트)으로 온다. 합쳐서 한 글자로 넘긴다."""
    lead, trail = "독".encode("cp949")
    assert lib.srtoybox_dbcs(lead, trail, 949) == ord("독")
    assert lib.srtoybox_dbcs(lead, 0x20, 949) == 0          # 앞 바이트 뒤에 엉뚱한 것이 왔다
    assert lib.srtoybox_dbcs(0x41, 0x42, 949) == 0          # 2바이트 글자가 아니다


def test_region_labels_follow_the_names_the_game_uses(lib):
    """이름은 판마다 다르다(DLC 의 역사 시나리오): 게임이 지금 쓰는 이름을 받아 두면 그것을 따른다.
    이름표의 그 번호가 같은 나라면 이름표의 한글 이름, 다른 나라면 게임의 이름을 영문 → 한글의 표에서 찾고, 없으면 게임의 이름 그대로."""
    lib.srtoybox_test_region_names.argtypes = [ctypes.c_char_p]
    name = lambda number: label(lib, number)
    plain = name(1001)
    try:
        assert name(1499) == "독일" and name(1188) == "#1188"          # 받아 둔 것이 없다 — 이름표만
        lib.srtoybox_test_region_names("1499\tGermany\n1188\tGermany\n1106\t폴란드\n202\tZzz Unknown Land\n1201\tPoland\n".encode("utf-8"))
        assert name(1499) == "독일"                                     # 이름표의 그 번호와 같은 나라(영문 이름이 같다)
        assert name(1106) == "폴란드"                                   # 〃(한글 이름이 같다 — 게임이 한글로 돌고 있다)
        assert name(1188) == "독일"                                     # 이름표에 없는 번호: 게임의 이름(영문)을 한글로
        assert name(1201) == "폴란드"                                   # 이름표의 그 번호(덴마크)와 다른 나라: 게임의 이름을 따른다
        assert name(202) == "Zzz Unknown Land"                          # 어느 표에도 없는 이름: 게임의 이름 그대로
        assert name(1001) == plain              # 게임이 이름을 주지 않은 번호: 이름표
        got = view(lib, [1499, 1188, 202], 0, 0, "zzz")                  # 찾기는 게임의 이름으로도 된다
        assert got[1] == [(202, "Zzz Unknown Land")]
    finally:
        lib.srtoybox_test_region_names(b"")
    assert name(1188) == "#1188"


def test_the_games_region_names_are_read_from_the_region_objects(lib):
    """game_region_names: 이번 판에 있는 나라마다 지역 객체의 이름 글을 읽는다(읽기만). 포인터가 빈 지역 · 판에 없는 지역은 빠진다."""
    import struct
    from toybox_fake_game import FakeGame
    lib.srtoybox_test_game.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.srtoybox_test_acts.argtypes = [ctypes.c_void_p] * 10
    lib.srtoybox_region_names.argtypes = [ctypes.c_char_p, ctypes.c_int]
    fake = FakeGame()
    fake.region(141, 1106, alive=3)
    fake.region(150, 1201, alive=5)                                      # 이번 판에 없는 지역
    fake.region(160, 1301, alive=3)                                      # 이름이 없는 지역
    fake.region(176, 1188)
    fake.play(176)
    kept = [ctypes.create_string_buffer(b"Germany"), ctypes.create_string_buffer("\uc624\uc2a4\ud2b8\ub9ac\uc544".encode("utf-8")),
            ctypes.create_string_buffer(b"Gone")]
    for index, buffer in ((176, kept[0]), (141, kept[1]), (150, kept[2])):
        fake._write(index, 0x300, "<Q", ctypes.addressof(buffer))
    lib.srtoybox_test_game(fake.base, ctypes.byref(fake.at))
    try:
        out = ctypes.create_string_buffer(4096)
        assert lib.srtoybox_region_names(out, len(out)) >= 0 and out.value == b""     # 그 자리를 모른다
        lib.srtoybox_test_acts(ctypes.byref(toybox.ActLayout(region_name=0x300)), *[None] * 9)
        assert lib.srtoybox_region_names(out, len(out)) >= 0
        assert out.value.decode("utf-8") == "1106\t오스트리아\n1188\tGermany\n"
        fake.menu()
        assert lib.srtoybox_region_names(out, len(out)) >= 0 and out.value == b""     # 게임 밖
    finally:
        lib.srtoybox_test_acts(None, *[None] * 9)
        lib.srtoybox_test_game(None, None)
