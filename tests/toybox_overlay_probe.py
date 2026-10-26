"""ToyBox 의 화면 · 입력 끼어들기를 게임 없이 새 프로세스에서 밟아 본다.

실제 Direct3D 장치와 swap chain, 가짜 "게임 창"을 만들고, 훅 DLL 사본을 불러 ToyBox 가 끼어들게 한 뒤 게임이 하듯
Present 를 부르고 창 메시지를 보낸다. Steam 오버레이 같은 다른 훅은 가짜로 흉내 낸다.

    python toybox_overlay_probe.py <훅 DLL 사본> <mode>

다른 훅과 함께 돌 때 (출력 "calls=<가짜 훅이 불린 횟수> hr=<마지막 반환값> plain=<아무 훅도 없을 때의 반환값>"):
    plain            다른 훅이 없다
    before           가상 함수 표의 칸에 다른 훅이 먼저 있다
    rehook           다른 훅이 표의 칸에 ToyBox 위로 다시 끼어든다
    inline           진짜 Present 의 머리에 점프가 심겨 있고, 그 훅이 ToyBox 의 함수를 원래 함수로 부른다
    steam            Steam 오버레이가 실제로 한 배치: 진짜 Present 와 ToyBox 함수 양쪽 머리에 점프, 원본은 ToyBox 함수의 트램펄린
    inline_resize    ResizeBuffers 의 머리에 점프
    inline_present1  Present1 의 머리에 점프
끼어들면 안 될 때 (출력 "hooked" 또는 "nohook"):
    longpatch        진짜 Present 의 머리가 16바이트 고쳐져 있다(깨끗한 진입로가 덮는 14바이트보다 길다)
    longpatch_resize 진짜 ResizeBuffers 의 머리만 그렇게 고쳐져 있다 — Present 에도 끼어들면 안 된다(그리기만 하고 뒷면을 못 놓는다)
    stale            올라와 있는 dxgi.dll 이 디스크의 파일과 다른 판이다
그리기와 입력 (출력은 "이름=값" 들):
    draw_resize      창을 켜고 그린 뒤 ResizeBuffers · Present1 을 부른다
    input            단축키, 창 위 · 밖의 누름, 치트를 넣는 동안의 실제 글쇠, 게임이 받은 글
    scale            그리는 크기가 창의 절반일 때 창 위 · 밖의 누름
    real_key         다른 곳에서 실제 수정키(Ctrl)가 눌렸다 떼인 뒤에 단축키를 누른다 — 그래도 ToyBox 가 받아야 한다
                     (검사 도구가 적어 둔 가짜 수정키가 지워지면 단축키가 맨 T 로 게임에 샌다)
게임 상태에 따른 단추 (출력 "button=<창 위 누름이 게임에 갔는가> text=<게임이 받은 글>"):
    gate_menu        ToyBox 가 게임을 읽을 수 있고 메뉴에 있다 — 단추가 꺼져 있다
    gate_leave       게임 안에서 단추를 누른 직후(실행되기 전) 메뉴로 나갔다 — 실행하지 않는다
    gate_typing      게임 안, 글쇠 방식(SRTOYBOX_DIRECT=0) — 1단계처럼 글쇠가 간다
직접 실행 (출력 "lines=<명령 처리 함수가 받은 줄들. 빈칸은 _, 줄 사이는 |> text=<게임이 받은 글>"):
    direct           게임 안에서 단추를 세 번 — 둘째 뒤에 치트 허용이 꺼진다(메뉴에 나갔다 온 것처럼)
    direct_fault     명령 처리 함수가 잘못된 주소에 쓴다 — ToyBox 가 잡고, 그 뒤로는 아무것도 실행하지 않는다
    direct_off       SRTOYBOX_DIRECT=0 — 명령 처리 함수를 부르지 않고 글쇠를 넣는다
    direct_menu      명령 처리 함수를 부를 수 있지만 게임 밖이다 — 부르지 않는다
    direct_read_off  같은 상황에서 SRTOYBOX_READ=0 — ToyBox 가 게임을 읽지 않는다: 단추가 켜져 있고 글쇠를 넣는다(1단계처럼)
명령 처리 함수가 일하는 도중에 ToyBox 로 되돌아올 때 (출력 "inside=<그 안에서 한 일의 결과> deepest=<명령 처리 함수가 겹쳐 불린 깊이> lines=… text=…"):
    reenter_timer    그 안에서 ToyBox 의 타이머가 다시 온다(게임이 메시지를 돌린다) — 대기열의 다음 명령을 그 안에서 시작하면 안 된다
    reenter_keyup    그 안에서 실제 키보드의 뗌이 온다
    reenter_present  그 안에서 화면을 한 번 내보낸다
ToyBox 가 부른 함수 안에서 난 예외를 위에서 잡을 때 (출력 "caught=<잡았는가> drawn=<그 뒤에 설정 창을 그렸는가>"):
    present_fault    진짜 Present 자리의 함수가 한 번 죽는다
돈 탭 — 내장 치트를 거치지 않고 국고를 고친다 (출력은 JSON 한 줄):
    money            게임 안에서 빠른 단추 다섯과 입력란(1234)의 더하기 · 빼기 · 이 값으로를 누른다
    money_menu       메뉴에 있다 — 단추가 꺼져 있다
    money_leave      단추를 누른 뒤 쓰기 전에 게임에서 나간다 — 쓰지 않고 버린다. 돌아오면 다시 된다
    money_write_off  SRTOYBOX_WRITE=0 — 까닭 한 줄만 보인다
    money_notfound   값의 자리를 찾지 못한 게임 — 〃
    money_unread     SRTOYBOX_READ=0 — 〃
    money_unreadable 게임 안인데 재고 칸 하나가 수가 아니다(보지 못한 구성의 판) — 〃
    money_fail       플레이어 지역 객체가 읽기 전용 쪽에 있다 — 쓰기가 실패하고 탭이 꺼진다
    money_reenter    옮기지 않은 기능의 직접 실행이 게임의 함수 안에 있는 동안 타이머가 다시 온다 — 그 안에서는 쓰지 않는다
    money_fault      옮기지 않은 기능의 직접 실행이 죽는다(오류 가드) — 함께 대기열에 있던 국고 요청을 쓰지 않는다
    money_hint       게임 안에서 돈 · 물자 탭과 내장 치트로 도는 탭의 바닥 안내
물자 탭 — 내장 치트를 거치지 않고 물자 재고를 고친다 (출력은 JSON 한 줄):
    stock            게임 안(물자 0 · 3 · 7 · 11 을 쓰는 판)에서 석유의 단추, "모든 물자"의 단추, 농산물의 단추를 누른다
    stock_menu       메뉴에 있다 — 이름표의 물자가 재고 없이 나오고 단추가 꺼져 있다
최소 유지 — 국고와 물자가 정해 둔 값보다 적어지면 올린다 (출력은 JSON 한 줄):
    keep             석유(500만)와 석탄(이번 판에서 쓰지 않는다)의 유지를 켜 둔 설정으로 게임에 들어간다. 창을 열어 국고의 유지를 켜고 끈다
    keep_menu        같은 설정으로 메뉴에 있다 — 쉬고, 상태 줄에 켜진 수가 보이고, 거기서 끌 수 있다
    keep_type        석유의 유지를 켠 채 수량을 새로 친다 — 입력을 마쳐야(Enter, 다른 곳을 누름) 쓰인다. 치던 채로 창을 닫으면 버려진다
    keep_reenter     국고의 유지를 켠 채, 옮기지 않은 기능의 직접 실행이 게임의 함수 안에 있는 동안 타이머가 다시 온다 — 그 안에서는 올리지 않는다
    keep_write_off   유지를 켜 둔 설정인데 값 쓰기가 꺼져 있다(SRTOYBOX_WRITE=0) — 메뉴에서도 게임 안에서도 상태 줄이 "쓸 수 없어 쉰다"고 한다
직접 쓰는 줄 넷 — 내장 치트를 거치지 않고 기술 수준 · 세계 시장 여론 · 관계를 고친다 (출력은 JSON 한 줄):
    more             게임 안에서 "지식 순위 올리기" 두 번, "세계 시장 여론 최고", (나라를 고르기 전과 뒤에) "관계 최고", "관계 중립"
    more_alone       같은 일을, 국고 · 재고의 자리를 찾지 못한 게임에서 — 묶음은 서로 기대지 않는다
    more_menu        메뉴에 있다 — 단추가 꺼져 있다
    more_leave       단추를 누른 뒤 쓰기 전에 게임에서 나간다 — 쓰지 않고 버린다. 돌아오면 다시 된다
    more_notfound    기술 수준의 자리만 찾지 못한 게임 — 그 줄에만 까닭이 보이고 나머지 줄은 그대로다
    more_write_off   SRTOYBOX_WRITE=0 — 네 줄 모두 까닭만 보인다. 내장 치트로 도는 줄은 그대로다
    more_unread      SRTOYBOX_READ=0 — 〃
    more_repick      폴란드를 골라 "관계 최고"를 누르고, 쓰이기 전에 덴마크로 바꿔 고른다 — 누른 때의 나라(폴란드)에 쓴다
    more_fail        고른 나라의 객체가 읽기 전용 쪽에 있다 — 쓰기가 실패하고, 값 쓰기 전체가 꺼진다
    more_reenter     옮기지 않은 기능의 직접 실행이 게임의 함수 안에 있는 동안 타이머가 다시 온다 — 그 안에서는 쓰지 않는다
    more_fault       옮기지 않은 기능의 직접 실행이 죽는다(오류 가드) — 함께 대기열에 있던 요청을 쓰지 않는다
연구의 두 줄 — 내장 치트를 거치지 않고 기술 · 부대 설계의 보유를 고친다 (출력은 JSON 한 줄. 연구의 판은 toybox_fake_game.standard_lab):
    research           게임 안에서 "대기열의 연구 즉시 완료", 그리고 "기술 수준 N 이하 전부 보유"(입력란은 처음 값 120)
    research_menu      메뉴에 있다 — 단추가 꺼져 있다
    research_leave     단추를 누른 뒤 쓰기 전에 게임에서 나간다 — 쓰지 않고 버린다. 돌아오면 다시 된다
    research_notfound  연구의 자리를 찾지 못한 게임 — 두 줄에만 까닭이 보이고 "지식 순위 올리기"는 그대로다
    research_write_off SRTOYBOX_WRITE=0 — 세 줄 모두 까닭만 보인다
    research_unread    SRTOYBOX_READ=0 — 〃
    research_fault     게임의 "효과를 다시 셈"이 죽는다 — ToyBox 가 잡고 빨간 경고를 띄운다. 그 뒤로는 아무것도 쓰지 않는다
    research_reenter   "효과를 다시 셈" 안에서 타이머가 다시 온다 — 그 안에서는 대기열의 내장 치트를 시작하지 않는다
물자 탭이 창 안에 들어오는가 (출력은 JSON 한 줄):
    layout_full      처음 여는 창에서, 물자 열하나를 쓰는 판 — 바닥에 "마지막으로 쓴 값" 줄이 생긴 뒤에도 모든 줄이 보인다
    layout_saved     이미 써 본 사용자의 창(저장된 크기 500x460) — 최소 유지의 긴 안내 글이 창 밖으로 나가지 않는다
설정 창에 보이는 글 (출력은 JSON 한 줄. 보이지 않는 글은 "-"):
    confirm          "외교·영토" 탭에서 폴란드를 고르고 스크롤을 내려 맨 아래의 "이 나라로 플레이"를 두 번 누른다
    confirm_fault    같은 탭에서 직접 실행("동맹 맺기")이 죽는다 — 경고가 스크롤을 내려도 보인다
    hints            메뉴에 있을 때와 게임 안에 있을 때의 바닥 안내
    leave            직접 실행: 단추를 누른 뒤 실행되기 전에 게임에서 나간다 — 부르지 않고, 돌아오면 다시 된다
    multiplayer      직접 실행: 단추를 누른 뒤 실행되기 전에 멀티플레이 표시가 선다
    pick_gone        폴란드를 골라 "관계 최고"를 누른 뒤 폴란드가 이번 판에서 없어진다 — 고른 것이 풀리고 단추가 꺼진다
    pick_become      폴란드를 골라 "관계 최고"를 누른 뒤 플레이하는 나라가 폴란드가 된다 — 〃
    pick_again       폴란드에 대해 "한 번 더"를 기다리는 중에 덴마크를 고른다 — 처음부터 다시 묻는다
    ansi_search      게임 창이 ANSI 창일 때 검색란에 한글 한 글자(두 바이트)를 넣는다

장치를 만들 수 없으면 "nodevice", 흉내를 만들 수 없으면 "skip <이유>".
tests/test_toybox.py 가 부른다(pytest 가 직접 모으는 테스트 파일이 아니다).
"""
import ctypes
import json
import os
import struct
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
from toybox_fake_game import DESIGN, MORE, MULTIPLAYER, OPTIONS, TECH, FakeGame, standard_lab  # noqa: E402  (이 파일과 같은 폴더)

SLOT_PRESENT, SLOT_RESIZE, SLOT_PRESENT1 = 8, 13, 22
LIMIT = 50      # 가짜 훅이 이만큼 불리면 맴도는 것이다. 여기서 끊어 프로세스가 죽지 않게 한다
WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x100, 0x101, 0x102, 0x200, 0x201, 0x202
WM_TIMER, WM_MOUSEWHEEL = 0x113, 0x20A
TIMER_ID = 0x7B0C0001       # ToyBox 의 실행기가 게임 창에 건 타이머(native/srtoybox/input.cpp)
REAL_KEY = 0x00140001       # 실제 키보드의 메시지처럼 스캔 코드가 든 lParam (ToyBox 의 실행기가 보내는 것은 스캔 코드가 0 이다)
# 설정 창의 자리: 처음 뜨는 곳 40,60 · 크기 500x460 (build/verify/toybox/G1-open.png). 단추의 자리는 창에게 묻는다(Game.spot)
CHEAT_TAB = "tab:화면·진행"   # 게임의 함수를 부르는 단추가 있는 탭(첫 탭 "돈"은 값 쓰기 전용 화면이다). 이름은 내장 치트로 돌던 때의 것이다
BUTTON = "run:fullmapshow"   # 그 탭의 첫 줄 "GUI 숨기기/보이기" — GUI 객체의 가상 함수를 부른다(예전에는 cheat fullmapshow)
TITLE = (300, 70)           # 제목 줄 — 눌러도 아무 일도 없다
NO_DIRTY_RECTS = (ctypes.c_byte * 40)()     # 0 으로 채운 DXGI_PRESENT_PARAMETERS (Present1 이 읽는 동안 살아 있어야 한다)


class RATIONAL(ctypes.Structure):
    _fields_ = [("Numerator", ctypes.c_uint), ("Denominator", ctypes.c_uint)]


class MODE_DESC(ctypes.Structure):
    _fields_ = [("Width", ctypes.c_uint), ("Height", ctypes.c_uint), ("RefreshRate", RATIONAL), ("Format", ctypes.c_uint),
                ("ScanlineOrdering", ctypes.c_uint), ("Scaling", ctypes.c_uint)]


class SAMPLE_DESC(ctypes.Structure):
    _fields_ = [("Count", ctypes.c_uint), ("Quality", ctypes.c_uint)]


class SWAP_CHAIN_DESC(ctypes.Structure):
    _fields_ = [("BufferDesc", MODE_DESC), ("SampleDesc", SAMPLE_DESC), ("BufferUsage", ctypes.c_uint),
                ("BufferCount", ctypes.c_uint), ("OutputWindow", wintypes.HWND), ("Windowed", wintypes.BOOL),
                ("SwapEffect", ctypes.c_uint), ("Flags", ctypes.c_uint)]


LRESULT = ctypes.c_ssize_t
PRESENT = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint)
PRESENT1 = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p)
RESIZE = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)
WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
HANDLER = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)    # 게임의 명령 처리 함수: void f(void *context, const char *line)
RECOMPUTE = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int)     # 게임의 "효과를 다시 셈": void f(void *world, int index)
COLONIZE = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_bool)   # 게임의 "식민지화": void f(void *region, int other, bool)
FIGHT = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_bool, ctypes.c_int, ctypes.c_bool, ctypes.c_bool)   # 게임의 "전쟁"
INDEX2, POINTER2 = 0x80, 0x88                   # 가짜 게임의 플레이어 인덱스 · 포인터의 둘째 사본
BECOME = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int)        # 게임의 "플레이 지역 바꾸기": void f(void *region, int 0)
LEADERS, LEADER_ID = 0x90, 0xAE                 # 가짜 게임의 지도자 표의 포인터 · 지역 객체 안의 지도자 번호의 자리
ELECT = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)   # 게임의 "새 지도자 세우기"
NEWS = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool, ctypes.c_void_p,
                          ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool)   # 게임의 "알림"
TREATIES = 0xE00                                # 가짜 게임의 지역 객체 안의 조약 표의 자리(조약 1 의 비트 묶음 포인터는 +8)
TREATY = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_bool, ctypes.c_int, ctypes.c_bool, ctypes.c_int, ctypes.c_bool, ctypes.c_bool,
                            ctypes.c_bool)      # 게임의 "조약"
MAP = 0x98                                      # 가짜 게임의 지도 객체의 포인터
ANNEX = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool, ctypes.c_bool)   # 게임의 "칸의 주인 바꾸기"
SPAWN = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool,
                           ctypes.c_void_p, ctypes.c_bool, ctypes.c_int, ctypes.c_bool)   # 게임의 "부대 만들기"
UNITS = 0xD0                                    # 가짜 게임의 세계의 부대 목록(첫 부대의 포인터. 부대의 +8 로 이어진다)
UI_FLAGS, GUI, GUI_VTABLE = 0xC0, 0xC8, 0x3F00  # 가짜 게임의 화면의 깃발(dword) · GUI 객체의 포인터 · 그 객체의 함수 표(8칸)
GUISET = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                            ctypes.c_int, ctypes.c_int, ctypes.c_int)   # GUI 객체의 가상 함수 "깃발을 바꾼다"
GAME_OVER, WINNERS = 0xB0, 0xB8                 # 가짜 게임의 "판이 끝난 꼴"(dword)과 이긴 지역들의 묶음의 포인터
PICKED_UNITS, UPGRADES = 0xA8, 0x2C6C           # 가짜 게임의 "고른 부대의 목록"(첫 노드의 포인터)과 세계 객체 안의 개량 보정 표의 자리
DEPLOY = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)   # 게임의 "배치 칸 찾기"
MAP_HEX = 0xA0                                  # 가짜 게임의 "지도에서 고른 칸"(dword: 가로 | 세로 << 16. 0 이면 고르지 않았다)
MAP_PICK = 0x70                                 # 가짜 게임의 "지도에서 고른 지역": 이 자리의 포인터가 가리키는 word(바로 뒤 0x78)가 인덱스다
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.CreateWindowExW.restype = wintypes.HWND
user32.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD] + [ctypes.c_int] * 4 \
    + [wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID]
user32.DefWindowProcW.restype = LRESULT
user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetKeyState.restype = ctypes.c_short
user32.GetKeyState.argtypes = [ctypes.c_int]
user32.SendMessageW.restype = LRESULT
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SetWindowLongPtrW.restype = ctypes.c_void_p
user32.SetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
user32.SetWindowLongPtrA.restype = ctypes.c_void_p
user32.SetWindowLongPtrA.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
user32.SendMessageA.restype = LRESULT
user32.SendMessageA.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.IsWindowUnicode.argtypes = [wintypes.HWND]
user32.PeekMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT, wintypes.UINT]
kernel32.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.FlushInstructionCache.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
kernel32.GetCurrentProcess.restype = wintypes.HANDLE
kernel32.GetModuleHandleW.restype = ctypes.c_void_p
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]


def make_swap_chain(buffer: tuple[int, int] | None = None):
    """가짜 게임 창(클라이언트 1024x768, 화면에 보이지 않는다)과 그 swap chain. buffer 를 주면 그리는 크기를 창과 다르게 한다."""
    d3d11 = ctypes.WinDLL("d3d11")
    hwnd = user32.CreateWindowExW(0, "STATIC", "probe", 0x80000000, 0, 0, 1024, 768, None, None, None, None)   # WS_POPUP: 테두리 없음
    desc = SWAP_CHAIN_DESC()
    desc.BufferCount = 1
    desc.BufferDesc.Format = 28          # DXGI_FORMAT_R8G8B8A8_UNORM
    if buffer:
        desc.BufferDesc.Width, desc.BufferDesc.Height = buffer
    desc.BufferUsage = 0x20              # DXGI_USAGE_RENDER_TARGET_OUTPUT
    desc.OutputWindow = hwnd
    desc.SampleDesc.Count = 1
    desc.Windowed = True
    swap, device, context = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_void_p()
    for driver in (1, 5):                # 하드웨어, 안 되면 WARP
        hr = d3d11.D3D11CreateDeviceAndSwapChain(None, driver, None, 0, None, 0, 7, ctypes.byref(desc), ctypes.byref(swap),
                                                 ctypes.byref(device), None, ctypes.byref(context))
        if hr >= 0 and swap.value:
            return swap, hwnd
    return None, hwnd


def vtable(swap):
    return ctypes.cast(ctypes.cast(swap, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))


def write_code(address: int, data: bytes) -> None:
    old = wintypes.DWORD()
    kernel32.VirtualProtect(address, len(data), 0x40, ctypes.byref(old))      # 실행 · 읽기 · 쓰기
    ctypes.memmove(address, data, len(data))
    kernel32.VirtualProtect(address, len(data), old.value, ctypes.byref(old))
    kernel32.FlushInstructionCache(kernel32.GetCurrentProcess(), address, len(data))


def write_slot(table, index: int, value: int) -> None:
    address = ctypes.addressof(table.contents) + index * ctypes.sizeof(ctypes.c_void_p)
    old = wintypes.DWORD()
    kernel32.VirtualProtect(address, 8, 0x04, ctypes.byref(old))              # 읽기 · 쓰기
    table[index] = value
    kernel32.VirtualProtect(address, 8, old.value, ctypes.byref(old))


def plant_jump(function: int, target: int) -> None:
    """함수의 머리 5바이트를 가까운 디딤돌로 가는 점프(E9)로 바꾼다. 디딤돌은 target 으로 가는 절대 점프다."""
    stub = None
    for step in range(1, 4000):                               # E9 가 닿는 2GB 안에서 빈 자리를 찾는다
        stub = kernel32.VirtualAlloc((function & ~0xFFFF) - step * 0x10000, 4096, 0x3000, 0x40)   # 예약+확정, 실행·읽기·쓰기
        if stub:
            break
    assert stub, "디딤돌을 둘 자리를 찾지 못했다"
    ctypes.memmove(stub, bytes([0xFF, 0x25, 0, 0, 0, 0]) + target.to_bytes(8, "little"), 14)
    write_code(function, bytes([0xE9]) + (stub - (function + 5)).to_bytes(4, "little", signed=True))


def prologue_decoder(hook: str):
    """ToyBox 의 명령 길이 세기를 빌려 쓴다. DLL 을 미리 올려 둘 뿐이다 — 끼어드는 것은 훅이 srtoybox_start 를 부를 때다."""
    toybox = ctypes.WinDLL(str(Path(hook).with_name("srtoybox.dll")))
    toybox.srtoybox_prologue_length.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    return toybox.srtoybox_prologue_length


def trampoline(length_of, function: int) -> int:
    """함수의 첫 명령들을 옮겨 놓고 그 다음 자리로 뛰는 코드(머리에 점프를 심는 훅이 '원래 함수'로 부르는 것). 옮길 수 없으면 0."""
    head = ctypes.string_at(function, 32)
    n = length_of(head, 32, 5)
    if n == 0:
        return 0
    code = kernel32.VirtualAlloc(None, 64, 0x3000, 0x40)
    ctypes.memmove(code, head[:n] + bytes([0xFF, 0x25, 0, 0, 0, 0]) + (function + n).to_bytes(8, "little"), n + 14)
    return code


def load_toybox(hook: str, table, slot: int = SLOT_PRESENT) -> bool:
    """훅을 불러 ToyBox 가 끼어들기를 마칠 때까지(로그에 결과가 적힐 때까지) 기다린다. 표의 그 칸이 바뀌었으면 True."""
    before = table[slot]
    ctypes.WinDLL(hook)                                       # 훅이 ToyBox 를 불러오고, ToyBox 가 다른 스레드에서 끼어든다
    home = os.environ.get("SRTOYBOX_HOME")
    log = Path(home, "toybox.log") if home else None
    deadline = time.time() + 10
    while time.time() < deadline:
        if log is None and table[slot] != before:             # 로그를 볼 수 없으면(손으로 돌릴 때) 칸이 바뀐 뒤 조금 더 기다린다
            time.sleep(0.5)
            break
        if log is not None and log.is_file() and "끼어들" in log.read_text(encoding="utf-8", errors="replace"):
            break
        time.sleep(0.05)
    return table[slot] != before


MODIFIERS = (0x11, 0x10, 0x12)          # Ctrl · Shift · Alt


def set_mods(on: bool) -> None:
    """이 스레드의 키 상태표에 Ctrl · Shift 를 눌린 것(또는 뗀 것)으로 적는다. Alt 는 늘 뗀 것으로 적는다 —
    테스트가 도는 동안 사용자가 실제 키보드의 Alt 를 누르고 있으면 상태표에 그것이 남아, 단축키가 Ctrl+Shift+Alt+T 로 읽힌다.

    적기 전에 GetKeyState 를 부른다. 다른 곳에서 실제 수정키가 눌리거나 떼이면(사용자가 다른 창에서 글을 친다) 그 변화는 이 스레드에
    밀려 있다가 **다음 GetKeyState 에서** 상태표를 실제 값으로 덮는다. GetKeyboardState · SetKeyboardState · PeekMessage 는 그것을
    받아 가지 않는다 — 먼저 받아 두지 않으면, 여기서 적은 Ctrl · Shift 가 ToyBox 의 창 프로시저가 수정키를 읽는 순간 지워진다
    [확인: 실행, 2026-10-09 — 오른쪽 Ctrl 을 한 번 눌렀다 뗀 뒤의 단축키가 네 번 가운데 네 번 게임으로 샜다]."""
    for vk in MODIFIERS:
        user32.GetKeyState(vk)
    state = (ctypes.c_ubyte * 256)()
    user32.GetKeyboardState(state)
    for vk in (0x11, 0x10):
        state[vk] = (state[vk] & 0x7F) | (0x80 if on else 0)
    state[0x12] &= 0x7F
    user32.SetKeyboardState(state)


def mods_held() -> tuple[bool, bool]:
    """이 스레드의 키 상태표에서 Ctrl · Shift 가 눌려 있는가. 밀려 있던 실제 키의 변화를 먼저 받는다(set_mods 의 설명)."""
    for vk in MODIFIERS:
        user32.GetKeyState(vk)
    state = (ctypes.c_ubyte * 256)()
    user32.GetKeyboardState(state)
    return bool(state[0x11] & 0x80), bool(state[0x10] & 0x80)


def mods_down() -> bool:
    return any(mods_held())


def mods_left(before: tuple[bool, bool]) -> bool:
    """글쇠 넣기가 Ctrl · Shift 를 눌린 채로 남겼는가: 지금 눌려 있는데, 넣기 전(before)에는 떼어져 있었고 실제 키보드에서도
    눌려 있지 않은 것이 있다. 사용자가 실제로 쥐고 있는 수정키는 넣기가 남긴 것이 아니다 — 테스트가 도는 동안 사용자는 같은 PC 를 쓴다."""
    now = mods_held()
    real = tuple(user32.GetAsyncKeyState(vk) & 0x8000 != 0 for vk in (0x11, 0x10))
    return any(n and not b and not r for n, b, r in zip(now, before, real))


def press_hotkey(hwnd, lparam: int = 1) -> None:
    """Ctrl+Shift+T (ToyBox 의 기본 단축키)."""
    for _ in range(5):                                        # 적은 것이 그대로 읽히는지 보고 보낸다(사이에 실제 수정키가 또 끼면 다시 적는다)
        set_mods(True)
        if [user32.GetKeyState(vk) < 0 for vk in MODIFIERS] == [True, True, False]:
            break
    user32.SendMessageW(hwnd, WM_KEYDOWN, 0x54, lparam)
    set_mods(False)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def tap_real_ctrl() -> bool:
    """시스템에 '진짜' 키 입력을 넣는다: 오른쪽 Ctrl 을 눌렀다 뗀다(사용자가 다른 창에서 수정키를 쓴 것처럼). 넣었으면 True.

    누름과 뗌을 한 번의 SendInput 에 넣는다 — 사용자의 실제 입력이 그 사이에 끼지 않는다. 혼자 눌렀다 떼는 Ctrl 은 아무 일도 하지 않는다.
    """
    events = (INPUT * 2)()
    for i, flags in enumerate((1, 1 | 2)):                    # KEYEVENTF_EXTENDEDKEY, 거기에 KEYEVENTF_KEYUP
        events[i].type = 1                                    # INPUT_KEYBOARD
        events[i].ki = KEYBDINPUT(0xA3, 0, flags, 0, 0)       # VK_RCONTROL
    sent = user32.SendInput(2, events, ctypes.sizeof(INPUT))
    time.sleep(0.05)                                          # 그 입력이 시스템의 키 상태에 닿을 때까지
    return sent == 2


def u32(hr: int) -> str:
    return f"{hr & 0xFFFFFFFF:#x}"


def call(table, slot: int, swap, function: int | None = None) -> int:
    """게임이 하듯 표의 그 칸을(function 을 주면 그 함수를) 부른다."""
    target = function or table[slot]
    if slot == SLOT_PRESENT:
        return PRESENT(target)(swap, 0, 0)
    if slot == SLOT_RESIZE:
        return RESIZE(target)(swap, 0, 800, 600, 0, 0)
    return PRESENT1(target)(swap, 0, 0, ctypes.addressof(NO_DIRTY_RECTS))


def run_fake_hook(hook: str, mode: str, swap, table) -> int:
    slot = {"inline_resize": SLOT_RESIZE, "inline_present1": SLOT_PRESENT1}.get(mode, SLOT_PRESENT)
    proto = {SLOT_PRESENT: PRESENT, SLOT_RESIZE: RESIZE, SLOT_PRESENT1: PRESENT1}[slot]
    real = table[slot]
    plain = call(table, slot, swap)                           # 아무도 끼어들기 전의 반환값
    state = {"calls": 0, "saved": real}                       # saved: 가짜 훅이 아는 "원래 함수"

    def body(*args):
        """다른 프로그램의 훅 대역. 표의 칸에 끼어드는 훅은 그 칸에 있는 것이 자기가 아니면 그것을 원래 함수로 안다."""
        state["calls"] += 1
        if state["calls"] > LIMIT:
            return 0
        now = table[slot]
        return proto(now if mode in ("before", "rehook") and now != fake_address else state["saved"])(*args)

    fake = proto(body)
    fake_address = ctypes.cast(fake, ctypes.c_void_p).value
    length_of = None
    if mode == "steam":                                       # 오버레이가 먼저 진짜 함수에 끼어들었다: 원본은 진짜 함수의 트램펄린
        length_of = prologue_decoder(hook)
        state["saved"] = trampoline(length_of, real)
        if not state["saved"]:
            print("skip 진짜 함수의 머리를 옮길 수 없다")
            return 0
    if mode in ("before", "rehook"):
        write_slot(table, slot, fake_address)                 # 다른 훅이 표의 칸에 먼저 끼어들어 있다
    if mode in ("inline", "steam", "inline_resize", "inline_present1"):
        plant_jump(real, fake_address)                        # 다른 훅이 진짜 함수의 머리에 점프를 심어 두었다
    if not load_toybox(hook, table, slot):
        print("toybox did not hook")
        return 1
    ours = table[slot]
    if mode == "rehook":                                      # 다른 훅이 표의 칸에 ToyBox 위로 다시 끼어든다
        state["saved"] = ours
        write_slot(table, slot, fake_address)
    if mode in ("inline", "inline_resize", "inline_present1"):   # 그 훅이 표에서 본 ToyBox 의 함수를 원래 함수로 안다
        state["saved"] = ours
    if mode == "steam":                                       # 오버레이가 ToyBox 의 함수 머리에도 점프를 심고, 원본을 그 트램펄린으로 바꾼다
        state["saved"] = trampoline(length_of, ours)
        if not state["saved"]:
            print("skip ToyBox 함수의 머리를 옮길 수 없다(컴파일러가 낸 머리가 달라졌다)")
            return 0
        plant_jump(ours, fake_address)
    hr = 0
    for _ in range(3 if slot == SLOT_PRESENT else 1):         # 게임이 화면을 세 번 내보낸다(크기 바꾸기 · Present1 은 한 번)
        hr = call(table, slot, swap)
    print(f"calls={state['calls']} hr={u32(hr)} plain={u32(plain)}")
    return 0


def run_refusal(hook: str, mode: str, table) -> int:
    """ToyBox 의 깨끗한 진입로는 '함수의 첫 14바이트 뒤는 디스크의 파일과 같다'고 믿는다. 그 믿음이 깨진 자리에서는 끼어들면 안 된다."""
    if mode.startswith("longpatch"):   # 다른 훅이 머리를 16바이트 고쳐 놓았다(mov rax, imm64 / jmp rax / nop 4개). 실행하지는 않는다
        real = table[SLOT_RESIZE if mode == "longpatch_resize" else SLOT_PRESENT]
        write_code(real, bytes([0x48, 0xB8]) + bytes(8) + bytes([0xFF, 0xE0, 0x90, 0x90, 0x90, 0x90]))
    else:                         # 게임이 떠 있는 동안 Windows 업데이트가 dxgi.dll 을 바꿨다: 올라온 것의 머리말이 파일과 다르다
        base = kernel32.GetModuleHandleW("dxgi.dll")
        stamp = base + ctypes.c_int.from_address(base + 0x3C).value + 8      # IMAGE_NT_HEADERS.FileHeader.TimeDateStamp
        write_code(stamp, (ctypes.c_uint.from_address(stamp).value ^ 0x5A5A5A5A).to_bytes(4, "little"))
    print("hooked" if load_toybox(hook, table) else "nohook")
    return 0


def run_draw_resize(hook: str, swap, hwnd, table) -> int:
    real_resize = table[SLOT_RESIZE]
    plain_present1 = call(table, SLOT_PRESENT1, swap)
    if not load_toybox(hook, table):
        print("toybox did not hook")
        return 1
    out = {"resize_hooked": int(table[SLOT_RESIZE] != real_resize)}
    PRESENT(table[SLOT_PRESENT])(swap, 0, 0)                  # 첫 화면: ToyBox 가 준비하고 창 프로시저를 감싼다
    press_hotkey(hwnd)
    out["visible"] = u32(max(PRESENT(table[SLOT_PRESENT])(swap, 0, 0) & 0xFFFFFFFF for _ in range(3)))
    # 훅을 거치지 않고 진짜 ResizeBuffers 를 부르면 실패해야 한다 — ToyBox 가 뒷면을 쥐고 있다(= 실제로 그렸다)
    out["direct_resize"] = u32(call(table, SLOT_RESIZE, swap, real_resize))
    out["hooked_resize"] = u32(call(table, SLOT_RESIZE, swap))
    out["after"] = u32(max(PRESENT(table[SLOT_PRESENT])(swap, 0, 0) & 0xFFFFFFFF for _ in range(2)))
    out["present1"] = u32(call(table, SLOT_PRESENT1, swap))
    out["present1_plain"] = u32(plain_present1)
    print(" ".join(f"{k}={v}" for k, v in out.items()))
    return 0


class Game:
    """가짜 게임: 창 프로시저가 받은 글쇠 · 누름을 적고, 화면을 내보내며 메시지를 돌린다."""

    def __init__(self, hook: str, buffer: tuple[int, int] | None = None, ansi: bool = False):
        self.hook = hook
        self.ansi = ansi        # 창 프로시저를 ANSI 방식으로 건다 — 글자 메시지가 코드 페이지의 바이트로 온다
        self.swap, self.hwnd = make_swap_chain(buffer)
        self.got: list[tuple[str, int]] = []
        self.proc = WNDPROC(self.receive)
        self.table = None
        self.toybox = None

    def receive(self, hwnd, message, wparam, lparam):
        name = {WM_KEYDOWN: "down", WM_KEYUP: "up", WM_CHAR: "char", WM_LBUTTONDOWN: "press", WM_LBUTTONUP: "release",
                0x8002: "app"}.get(message)                   # 0x8002: "즉시 승리"가 게임 창에 보내는 메시지
        if name:
            self.got.append((name, wparam))
        return user32.DefWindowProcW(hwnd, message, wparam, lparam)

    def start(self) -> bool:
        subclass = user32.SetWindowLongPtrA if self.ansi else user32.SetWindowLongPtrW
        subclass(self.hwnd, -4, ctypes.cast(self.proc, ctypes.c_void_p))                   # ToyBox 가 이 프로시저를 감싼다
        self.table = vtable(self.swap)
        if not load_toybox(self.hook, self.table):
            return False
        self.toybox = ctypes.WinDLL(str(Path(self.hook).with_name("srtoybox.dll")))
        self.toybox.srtoybox_ui_report.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.present(2)
        return True

    def facts(self) -> dict[str, tuple[int, int, bool, str]]:
        """설정 창이 방금 그린 것들: 이름 → (가운데 x, 가운데 y, 보이는가, 글). 창이 닫혀 있으면 비어 있다."""
        buf = ctypes.create_string_buffer(1 << 16)
        self.toybox.srtoybox_ui_report(buf, len(buf))           # 처음 부를 때부터 모으기 시작한다
        self.present(2)
        assert self.toybox.srtoybox_ui_report(buf, len(buf)) >= 0
        out = {}
        for line in buf.value.decode("utf-8").splitlines():
            name, x, y, visible, text = line.split("\t", 4)
            out[name] = (int(x), int(y), visible == "1", text)
        return out

    def spot(self, name: str) -> tuple[int, int]:
        """그 항목의 가운데(누를 자리). 창에 보이고 있어야 한다."""
        x, y, visible, _ = self.facts()[name]
        assert visible, f"{name} 이 창에 보이지 않는다"
        return x, y

    def shown(self, name: str) -> str:
        """그 항목이 창에 보이면 그 글, 아니면(그리지 않았거나 스크롤 밖) "-"."""
        _, _, visible, text = self.facts().get(name, (0, 0, False, ""))
        return text if visible else "-"

    def present(self, frames: int = 1) -> None:
        for _ in range(frames):
            PRESENT(self.table[SLOT_PRESENT])(self.swap, 0, 0)

    def pump(self, until, limit: float) -> bool:
        """메시지를 돌리고 화면을 내보낸다 — until() 이 참이 되거나 limit 초가 지날 때까지."""
        message = wintypes.MSG()
        end = time.time() + limit
        while time.time() < end:
            while user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
                user32.TranslateMessage(ctypes.byref(message))
                user32.DispatchMessageW(ctypes.byref(message))
            self.present()
            if until():
                return True
            time.sleep(0.005)
        return False

    def wait(self, seconds: float) -> None:
        end = time.time() + seconds
        self.pump(lambda: time.time() >= end, seconds + 1)

    def click(self, point: tuple[int, int] | str) -> str:
        """그 자리(또는 그 이름의 항목)를 누른다. 게임(가짜 창 프로시저)까지 간 것을 돌려준다: "press+release", 아무것도 안 갔으면 "none"."""
        if isinstance(point, str):
            point = self.spot(point)
        seen = len(self.got)
        for message, wparam in ((WM_MOUSEMOVE, 0), (WM_LBUTTONDOWN, 1), (WM_LBUTTONUP, 0)):
            user32.SendMessageW(self.hwnd, message, wparam, (point[1] << 16) | point[0])
            self.present(2)
        return "+".join(name for name, _ in self.got[seen:] if name in ("press", "release")) or "none"

    def hotkey(self, lparam: int = 1) -> str:
        """단축키를 누른다. 그 글쇠를 받은 쪽: "toybox"(삼켰다) 또는 "game"."""
        seen = len(self.got)
        press_hotkey(self.hwnd, lparam)
        self.present(3)
        return "game" if ("down", 0x54) in self.got[seen:] else "toybox"

    def open(self, lparam: int = 1) -> str:
        """단축키로 창을 열고 CHEAT_TAB 으로 간다. 단축키를 받은 쪽("toybox" / "game")을 돌려준다."""
        who = self.hotkey(lparam)
        self.click(CHEAT_TAB)
        return who

    def has(self, name: str, value: int) -> bool:
        return (name, value) in self.got

    def text(self) -> str:
        """게임이 받은 글쇠를 읽기 좋게: 글자는 그대로, 누름은 <이름>."""
        keys = {0x0D: "<Enter>", 0x1B: "<Esc>", 0x53: "<S>", 0x11: "<Ctrl>", 0x10: "<Shift>"}
        out = ""
        for name, value in self.got:
            if name == "char" and 32 <= value < 127:
                out += chr(value)
            elif name == "down":
                out += keys.get(value, f"<{value:#x}>")
        return out


def start_game(hook: str, buffer: tuple[int, int] | None = None, ansi: bool = False) -> Game | None:
    if not Path(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", "malgun.ttf").is_file():
        print("skip 맑은 고딕이 없다(설정 창의 단추 자리가 달라진다)")
        return None
    game = Game(hook, buffer, ansi)
    if game.swap is None:
        print("nodevice")
        return None
    if not game.start():
        print("skip ToyBox 가 끼어들지 않았다")
        return None
    return game


def fake_game(hook: str, handler: int | None = None, values: bool = True, more: dict | None = MORE, research: bool = True,
              recompute: int | None = None) -> FakeGame:
    """ToyBox 가 보는 "게임"을 가짜 메모리로 바꾼다. 폴란드(141, 1106. 국고 $5 B)와 독일(176, 1499. 국고 $14.43 B)이 있고 메뉴 상태다.
    기술 수준은 독일 130 · 폴란드 128, 독일의 여론 세 칸은 0.5 · 0.25 · 0.75, 독일 → 폴란드의 관계는 0.25 · -0.5(전쟁 명분 0.75),
    폴란드 → 독일은 0.125 · 0.5(전쟁 명분 1).

    handler 는 명령 처리 함수 자리에 둘 함수의 주소(없으면 직접 실행을 쓰는 테스트가 아니다).
    values 가 False 면 값의 자리를 찾지 못한 게임이다(돈 탭이 꺼진다).
    more 는 더 쓰는 값의 자리다 — 자리를 0 으로 둔 묶음은 못 찾은 것이 된다. None 이면 모두 못 찾은 게임이다.
    research 가 False 면 연구의 자리를 찾지 못한 게임이다. 아니면 연구의 판(standard_lab)이 fake.lab 에 붙는다.
    recompute 는 "효과를 다시 셈" 자리에 둘 함수의 주소다 — 주지 않으면 불린 지역 인덱스를 fake.recomputed 에 적는 함수를 둔다.
    """
    toybox = ctypes.WinDLL(str(Path(hook).with_name("srtoybox.dll")))
    toybox.srtoybox_test_game.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    toybox.srtoybox_test_values.argtypes = [ctypes.c_void_p]
    toybox.srtoybox_test_more.argtypes = [ctypes.c_void_p]
    fake = FakeGame()
    fake.region(141, 1106, alive=3)
    fake.region(176, 1499)
    fake.set_treasury(141, 5e9)
    fake.set_treasury(176, 14.43e9)
    fake.set_tech(141, 128.0)
    fake.set_tech(176, 130.0)
    fake.set_opinion(176, (0.5, 0.25, 0.75))
    fake.set_relation(176, 141, (0.25, -0.5, 0.75))
    fake.set_relation(141, 176, (0.125, 0.5, 1.0))
    toybox.srtoybox_test_game(fake.base, ctypes.byref(fake.at))
    if values:
        toybox.srtoybox_test_values(ctypes.byref(fake.layout))
    if more is not None:
        toybox.srtoybox_test_more(ctypes.byref(type(fake.more)(**more)))
    fake.colonized = []                                        # 가짜 "식민지화"가 불린 인자들: [종주국의 지역 번호, 대상의 인덱스, 깃발]
    fake.colonize = COLONIZE(lambda region, other, flag: fake.colonized.append(
        [struct.unpack("<H", ctypes.string_at(region + 8, 2))[0], other, int(flag)]))
    fake.fought = []                                           # 가짜 "전쟁"이 불린 인자들: [첫째 나라의 지역 번호, 깃발, 둘째 나라의 인덱스, 깃발, 깃발]
    fake.fight = FIGHT(lambda region, a, other, b, c: fake.fought.append(
        [struct.unpack("<H", ctypes.string_at(region + 8, 2))[0], int(a), other, int(b), int(c)]))
    fake.poke(MAP_PICK, "<Q", fake.base + MAP_PICK + 8)        # 지도에서는 아직 아무것도 고르지 않았다(인덱스 0)
    fake.became = []                                           # 가짜 "플레이 지역 바꾸기"가 불린 인자와 그때의 전역 넷
    fake.become = BECOME(lambda region, zero: fake.became.append(
        [struct.unpack("<H", ctypes.string_at(region + 8, 2))[0], zero, fake.peek(0x20, "<i"), fake.peek(INDEX2, "<i"),
         fake.peek(0x28, "<Q") == region, fake.peek(POINTER2, "<Q") == region]))
    play = fake.play

    def play_both(index: int) -> None:                         # 게임은 플레이어의 인덱스 · 포인터를 두 벌 둔다
        play(index)
        fake.poke(INDEX2, "<i", index)
        fake.poke(POINTER2, "<Q", fake.where[index])
    fake.play = play_both
    # 지도자: 표는 레코드 0xC8 바이트 × 16, 폴란드의 지도자는 3 번. 가짜 "새 지도자 세우기"는 9 번을 세운다
    fake.leaders = (ctypes.c_ubyte * (0xC8 * 16))()
    fake.poke(LEADERS, "<Q", ctypes.addressof(fake.leaders))
    ctypes.memmove(fake.where[141] + LEADER_ID, struct.pack("<H", 3), 2)
    fake.succession = []                                       # 불린 순서대로: ["elect", 지역 번호, 인자 둘, 그때 앞 지도자의 깃발] · ["news", …]
    number = lambda region: struct.unpack("<H", ctypes.string_at(region + 8, 2))[0]

    def elect(region, a, b):
        fake.succession.append(["elect", number(region), a, b, struct.unpack_from("<I", fake.leaders, 3 * 0xC8 + 0x28)[0]])
        ctypes.memmove(region + LEADER_ID, struct.pack("<H", 9), 2)

    def news(region, n1, n2, n3, n4, flag, nothing, before, index, after, z1, z2, z3, z4):
        fake.succession.append(["news", number(region), n1, n2, n3, n4, int(flag), nothing or 0, before, index, after, z1, z2, z3, int(z4)])
    fake.elect, fake.news = ELECT(elect), NEWS(news)
    # 조약: 독일의 조약 1(동맹)의 비트 묶음(비트의 번호가 지역 인덱스). 처음에는 아무와도 맺지 않았다
    fake.allies = (ctypes.c_uint64 * 16)()
    ctypes.memmove(fake.where[176] + TREATIES + 8, struct.pack("<Q", ctypes.addressof(fake.allies)), 8)
    fake.treaties = []                                         # 가짜 "조약"이 불린 인자들: [지역 번호, 쪽, 조약 번호, 맺는다, 다른 지역의 인덱스, 깃발 셋]
    fake.treaty = TREATY(lambda region, side, treaty, make, other, a, b, c: fake.treaties.append(
        [number(region), int(side), treaty, int(make), other, int(a), int(b), int(c)]))
    # 지도: 가로 8 × 세로 4 = 32칸. 칸 레코드는 16바이트(첫 word 가 주인의 인덱스), 칸마다 8바이트인 배열 둘
    owners = [176] * 8 + [150, 150, 141, 141, 150, 0, 176, 141] + [141] * 8 + [150, 176, 176, 150, 0, 0, 141, 150]
    fake.hexes, fake.side_a, fake.side_b = (ctypes.c_ubyte * (16 * 32))(), (ctypes.c_ubyte * (8 * 32))(), (ctypes.c_ubyte * (8 * 32))()
    for i, owner in enumerate(owners):
        struct.pack_into("<H", fake.hexes, 16 * i, owner)
    fake.map = (ctypes.c_ubyte * 0x90)()
    struct.pack_into("<QQQ", fake.map, 0, ctypes.addressof(fake.hexes), ctypes.addressof(fake.side_a), ctypes.addressof(fake.side_b))
    struct.pack_into("<i", fake.map, 0x80, 8)
    struct.pack_into("<i", fake.map, 0x88, 32)
    fake.poke(MAP, "<Q", ctypes.addressof(fake.map))
    fake.owners = lambda: [struct.unpack_from("<H", fake.hexes, 16 * i)[0] for i in range(32)]
    fake.annexed = []                                          # 가짜 "칸의 주인 바꾸기"가 불린 것: [칸의 번호, 가로, 세로, 새 주인, 인자 넷, 묶음의 주소들이 맞는가]

    def annex(ref, owner, z1, z2, f1, f2):
        m, h, a, b, x, y, index = struct.unpack("<QQQQHHi", ctypes.string_at(ref, 0x28))
        right = (m == ctypes.addressof(fake.map) and h == ctypes.addressof(fake.hexes) + 16 * index
                 and a == ctypes.addressof(fake.side_a) + 8 * index and b == ctypes.addressof(fake.side_b) + 8 * index)
        fake.annexed.append([index, x, y, owner, z1, z2, int(f1), int(f2), right])
        struct.pack_into("<H", fake.hexes, 16 * index, owner)  # 게임처럼 그 칸의 주인을 바꾼다
    fake.annex = ANNEX(annex)
    fake.spawned = []                                          # 가짜 "부대 만들기"가 불린 인자들: [지역 번호, 설계, 수, 칸, 나머지 여섯]

    fake.made = []                                             # 가짜 "부대 만들기"가 만든 부대 객체들(세계의 부대 목록의 머리에 건다)

    def spawn(region, design, count, hex_, f1, f2, out_, f3, z, f4):
        fake.spawned.append([struct.unpack("<H", ctypes.string_at(region + 8, 2))[0], design, count, hex_, int(f1), int(f2), out_ or 0, int(f3), z, int(f4)])
        raw = (ctypes.c_ubyte * 0x240)()
        struct.pack_into("<Q", raw, 0x08, fake.peek(UNITS, "<Q"))
        struct.pack_into("<H", raw, 0x1E, struct.unpack("<H", ctypes.string_at(region + 4, 2))[0])
        struct.pack_into("<3f", raw, 0x9C, 0.5, 0.25, 0.75)    # 효율 · 경험 · 사기: 게임이 준 값
        struct.pack_into("<Q", raw, 0x230, ctypes.addressof(fake.lab.designs) + 0x168 * design)
        fake.made.append(raw)
        fake.poke(UNITS, "<Q", ctypes.addressof(raw))
        return 0
    fake.spawn = SPAWN(spawn)
    # GUI 객체: 첫 칸이 함수 표(가짜 게임의 GUI_VTABLE 자리), +0x2C 가 깃발. 표의 5번이 가짜 "깃발을 바꾼다"
    fake.gui = (ctypes.c_ubyte * 0x40)()
    fake.gui_calls = []                                        # 불린 인자들: [새 깃발, 나머지 일곱]

    def gui_set(obj, flags, a, b, c, d, e, f, g):
        fake.gui_calls.append([flags, a, b or 0, c, d or 0, e, f, g])
        struct.pack_into("<I", fake.gui, 0x2C, flags)          # 게임처럼 객체의 깃발을 바꾼다
        return obj
    fake.gui_set = GUISET(gui_set)
    struct.pack_into("<Q", fake.gui, 0, fake.base + GUI_VTABLE)
    struct.pack_into("<I", fake.gui, 0x2C, 0x01000000)
    slot = ctypes.cast(fake.gui_set, ctypes.c_void_p).value
    if handler in CRASH_STUBS:
        slot = handler                                         # "GUI 숨기기/보이기"를 누르면 게임의 함수 안에서 접근 위반이 난다
    elif handler is not None:
        # handler 는 "게임의 함수가 도는 동안 일어나는 일"을 시험하려고 테스트가 주는 몸통이다(내장 치트의 명령 처리 함수 자리였다).
        # 이제 ToyBox 가 부르는 게임의 함수 가운데 하나인 GUI 의 "깃발을 바꾼다" 안에서 그 몸통을 돌린다 — 넘기는 글은 그 단추가 하던 치트의 이름이다.
        inner = HANDLER(handler)

        def through(obj, flags, a, b, c, d, e, f, g):
            gui_set(obj, flags, a, b, c, d, e, f, g)
            inner(None, b"cheat fullmapshow")
            return obj
        fake.gui_through = GUISET(through)
        slot = ctypes.cast(fake.gui_through, ctypes.c_void_p).value
    fake.poke(GUI_VTABLE + 8 * 5, "<Q", slot)
    fake.poke(GUI, "<Q", ctypes.addressof(fake.gui))
    fake.poke(UI_FLAGS, "<I", 0x16BCE00A)                      # 게임에서 읽은 값(숨김 비트는 꺼져 있다)
    fake.deployed = []                                         # 가짜 "배치 칸 찾기"가 불린 인자들: [지역 번호, 유형]

    def deploy(region, out_, move):
        fake.deployed.append([struct.unpack("<H", ctypes.string_at(region + 8, 2))[0], move])
        ctypes.memmove(out_, struct.pack("<I", 0x00070000 | (40 + move)), 4)      # 유형마다 다른 칸: (40 + 유형, 7)
        return out_
    fake.deploy = DEPLOY(deploy)
    toybox.srtoybox_test_acts.argtypes = [ctypes.c_void_p] * 10
    if os.environ.get("PROBE_NO_ACTS") != "1":
        toybox.srtoybox_test_acts((ctypes.c_uint32 * 25)(1, 1, 1, 1, 1, MAP_PICK, INDEX2, POINTER2, LEADERS, LEADER_ID, 1, TREATIES, 1, MAP, 1, MAP_HEX,
                                                         PICKED_UNITS, UPGRADES, GAME_OVER, WINNERS, UI_FLAGS, GUI, GUI_VTABLE,
                                                         1, UNITS),
                                  ctypes.cast(fake.colonize, ctypes.c_void_p), ctypes.cast(fake.fight, ctypes.c_void_p),
                                  ctypes.cast(fake.become, ctypes.c_void_p), ctypes.cast(fake.elect, ctypes.c_void_p),
                                  ctypes.cast(fake.news, ctypes.c_void_p), ctypes.cast(fake.treaty, ctypes.c_void_p),
                                  ctypes.cast(fake.annex, ctypes.c_void_p), ctypes.cast(fake.spawn, ctypes.c_void_p),
                                  ctypes.cast(fake.deploy, ctypes.c_void_p))
    fake.recomputed = []
    fake.recompute = RECOMPUTE(lambda _world, index: fake.recomputed.append(index))     # 게임이 살아 있는 동안 붙들어 둔다
    if research:
        toybox.srtoybox_test_research.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        fake.lab = standard_lab(fake)
        toybox.srtoybox_test_research(ctypes.byref(fake.lab.layout),
                                      recompute if recompute is not None else ctypes.cast(fake.recompute, ctypes.c_void_p))
    return fake


def run_gate(hook: str, mode: str) -> int:
    game = start_game(hook)
    if game is None:
        return 0
    fake = fake_game(hook)
    if mode != "gate_menu":
        fake.play(176)
    game.open()
    button = game.click(BUTTON)
    if mode == "gate_leave":
        fake.menu()                                           # 눌린 명령이 실행되기 전에 게임에서 나갔다
    game.got.clear()
    game.pump(lambda: game.has("up", 0x1B), 20 if mode == "gate_typing" else 2)
    game.wait(0.4)
    print(f"button={button} text={game.text()}")
    return 0


CRASH_STUBS: set[int] = set()   # crash_stub 이 만든 함수들의 주소 — fake_game 이 그것을 GUI 의 함수 자리에 그대로 둔다


def crash_stub() -> int:
    """부르면 0 번지에 쓰는 함수(mov dword ptr [0],1 / ret) — 게임의 함수 안에서 나는 접근 위반을 흉내 낸다."""
    code = kernel32.VirtualAlloc(None, 16, 0x3000, 0x40)
    ctypes.memmove(code, bytes([0xC7, 0x04, 0x25, 0, 0, 0, 0, 1, 0, 0, 0, 0xC3]), 12)
    CRASH_STUBS.add(code)
    return code


def run_direct(hook: str, mode: str) -> int:
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    box: dict[str, FakeGame] = {}

    def body(_context, line):
        lines.append(line.decode())
        if line == b"cheat allowcheats":                      # 게임이 하듯 치트 허용 비트를 세운다
            box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)

    handler = HANDLER(body)
    fake = box["fake"] = fake_game(hook, crash_stub() if mode == "direct_fault" else ctypes.cast(handler, ctypes.c_void_p).value)
    if mode not in ("direct_menu", "direct_read_off"):
        fake.play(176)
    game.open()
    game.got.clear()
    if mode in ("direct_off", "direct_read_off"):
        game.click(BUTTON)
        game.pump(lambda: game.has("up", 0x1B), 20 if mode == "direct_off" or os.environ.get("SRTOYBOX_READ") == "0" else 2)
        game.wait(0.4)
    elif mode == "direct_menu":
        game.click(BUTTON)
        game.wait(1.0)
    else:
        for press in range(3):
            if press == 2:
                fake.poke(OPTIONS, "<I", 0)                   # 메뉴에 나갔다 온 것처럼 치트 허용이 꺼졌다
            game.click(BUTTON)
            game.wait(0.4)
    print("lines=" + "|".join(line.replace(" ", "_") for line in lines) + " text=" + game.text())
    return 0


DIPLOMACY = "tab:외교·영토"
LAST_BUTTON = "run:becomeregion"   # 그 탭의 맨 아래 단추 "이 나라로 플레이" — 되돌릴 수 없는 것이라 두 번 눌러야 한다(게임의 함수를 부른다)


def recording_game(hook: str, crash: bool = False, ansi: bool = False):
    """가짜 게임(폴란드 · 덴마크 · 독일, 메뉴 상태)과 명령 처리 함수가 받은 줄들. crash 면 그 함수가 잘못된 주소에 쓴다."""
    game = start_game(hook, ansi=ansi)
    if game is None:
        return None
    lines: list[str] = []
    box: dict[str, FakeGame] = {}

    def body(_context, line):
        lines.append(line.decode())
        if line == b"cheat allowcheats":
            box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)

    game.handler = HANDLER(body)                              # 게임이 살아 있는 동안 붙들어 둔다
    fake = box["fake"] = fake_game(hook, crash_stub() if crash else ctypes.cast(game.handler, ctypes.c_void_p).value)
    fake.region(150, 1201, alive=3)                           # 덴마크
    return game, fake, lines


def scroll_to(game: Game, name: str, up: bool = False) -> bool:
    """마우스를 설정 창 위에 두고, 그 항목이 보일 때까지 휠을 아래로(up 이면 위로) 굴린다."""
    for _ in range(12):
        if game.facts()[name][2]:
            return True
        user32.SendMessageW(game.hwnd, WM_MOUSEMOVE, 0, (400 << 16) | 400)
        user32.SendMessageW(game.hwnd, WM_MOUSEWHEEL, ((120 if up else -120) & 0xFFFF) << 16, 0)
        game.present(2)
    return game.facts()[name][2]


def run_window(hook: str, mode: str) -> int:
    """설정 창에 보이는 글과 그 전이. 출력은 JSON 한 줄."""
    if mode == "ansi_search" and kernel32.GetACP() != 949:
        print("skip 시스템 코드 페이지가 949(한국어)가 아니다")
        return 0
    made = recording_game(hook, crash=mode == "confirm_fault", ansi=mode == "ansi_search")
    if made is None:
        return 0
    game, fake, lines = made
    out: dict[str, object] = {}
    if mode in ("leave", "multiplayer"):
        fake.play(176)
        game.open()
        game.click(BUTTON)                                    # 눌렀다. 실행은 다음 타이머에서다(메시지를 돌릴 때 온다)
        if mode == "leave":
            fake.menu()                                       # 그 전에 게임에서 나갔다
        else:
            fake.poke(MULTIPLAYER, "<B", 1)                   # 그 전에 멀티플레이 표시가 섰다
        game.wait(0.5)
        out["dropped"] = list(lines)
        out["trouble"], out["status"] = game.shown("unwritten"), game.shown("status")
        game.click(BUTTON)                                    # 단추가 꺼져 있다
        game.wait(0.4)
        out["while_off"] = list(lines)
        if mode == "leave":
            fake.play(176)
        else:
            fake.poke(MULTIPLAYER, "<B", 0)
        game.click(BUTTON)                                    # 돌아오면 다시 된다
        game.wait(0.4)
        out["lines"] = lines
        print(json.dumps(out, ensure_ascii=False))
        return 0
    if mode == "hints":
        game.open()                                           # 내장 치트로 도는 탭에서 본다(돈 · 물자 탭의 안내는 money_hint 가 본다)
        out["menu"] = game.shown("hint")
        fake.play(176)
        out["game"] = game.shown("hint")
        print(json.dumps(out, ensure_ascii=False))
        return 0
    fake.play(176)
    game.hotkey()
    game.click(DIPLOMACY)
    game.wait(1.2)                                            # 나라 목록은 1초마다 읽는다
    game.click("row:1106")
    if mode == "confirm":
        out["hidden"] = game.shown(LAST_BUTTON)               # 맨 아래 단추는 처음에는 가려 있다
        out["scrolled"] = scroll_to(game, LAST_BUTTON)
        game.click(LAST_BUTTON)                               # 처음 누르면 묻기만 한다
        game.wait(0.3)
        out["after_first"] = list(lines)
        out["armed"] = game.shown(LAST_BUTTON)
        out["status"], out["picked"] = game.shown("status"), game.shown("picked")   # 스크롤을 내린 채로도 보이는가
        game.click(LAST_BUTTON)
        game.wait(0.4)
        out["became"] = [call[:2] for call in fake.became]
    elif mode == "confirm_fault":
        game.click(CHEAT_TAB)                                 # 게임의 함수를 부르는 줄이 있는 탭 — 가짜 GUI 함수 안에서 예외가 난다
        game.click(BUTTON)
        game.wait(0.4)
        out["at_top"] = game.shown("fault")
        game.click(DIPLOMACY)                                 # 다른 탭의 아래쪽을 보고 있어도 경고는 남는다
        out["scrolled"] = scroll_to(game, LAST_BUTTON)
        out["scrolled_down"] = game.shown("fault")
    elif mode in ("pick_gone", "pick_become"):
        game.click("run:love")                                # 직접 쓰는 줄: 폴란드와의 관계를 쓴다
        game.wait(0.3)
        out["first"] = [fake.relation(176, 141), fake.relation(141, 176)]
        fake.set_relation(176, 141, (0.25, -0.5, 0.75))       # 되돌려 둔다 — 뒤의 누름이 다시 쓰면 알아본다
        fake.set_relation(141, 176, (0.125, 0.5, 1.0))
        if mode == "pick_gone":
            struct.pack_into("<I", fake.objects[141], 0, 5)   # 폴란드가 이번 판에 없는 지역이 됐다(다른 판을 불러온 것처럼)
            game.wait(1.5)                                    # 나라 목록은 1초마다 읽는다
        else:
            fake.play(141)                                    # 플레이하는 나라가 폴란드로 바뀌었다(cheat becomeregion)
            game.wait(0.3)
        out["status"], out["picked"], out["row"] = game.shown("status"), game.shown("picked"), game.shown("row:1106")
        game.click("run:love")                                # 고른 나라가 없다 — 단추가 꺼져 있다
        game.wait(0.3)
        out["later"], out["unwritten"] = [fake.relation(176, 141), fake.relation(141, 176)], game.shown("unwritten")
    elif mode == "pick_again":
        scroll_to(game, LAST_BUTTON)
        game.click(LAST_BUTTON)                               # 폴란드에 대해 묻는 중이다
        out["asked"] = game.shown(LAST_BUTTON)
        scroll_to(game, "search", up=True)                    # 목록은 탭 내용의 맨 위에 있다
        game.click("row:1201")                                # 그 사이에 덴마크로 바꿔 골랐다
        scroll_to(game, LAST_BUTTON)
        out["after_repick"] = game.shown(LAST_BUTTON)         # 묻던 것은 폴란드에 대한 것이었다 — 처음부터 다시
        game.click(LAST_BUTTON)
        game.wait(0.3)
        out["after_one_press"] = list(lines)
        out["asked_again"] = game.shown(LAST_BUTTON)
        game.click(LAST_BUTTON)
        game.wait(0.4)
        out["became"] = [call[:2] for call in fake.became]
    elif mode == "ansi_search":
        game.click("search")
        game.present(3)                                       # 검색란이 글을 받는다
        for byte in "폴".encode("cp949"):                      # ANSI 창에는 한글 한 글자가 WM_CHAR 두 번(앞 · 뒤 바이트)으로 온다
            user32.SendMessageA(game.hwnd, WM_CHAR, byte, 1)
            game.present()
        game.present(3)
        facts = game.facts()
        out["ansi"] = int(not user32.IsWindowUnicode(game.hwnd))
        out["search"] = game.shown("search")
        out["rows"] = sorted(name for name in facts if name.startswith("row:"))
    out["lines"] = lines
    print(json.dumps(out, ensure_ascii=False))
    return 0


def run_reenter(hook: str, mode: str) -> int:
    """게임의 명령 처리 함수가 일하는 도중에 ToyBox 로 되돌아온다(메시지를 돌리거나 화면을 내보내는 게임 코드가 그렇다)."""
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    inside: list[str] = []
    box = {"depth": 0, "deepest": 0, "done": False}

    def body(_context, line):
        lines.append(line.decode())
        box["depth"] += 1
        box["deepest"] = max(box["deepest"], box["depth"])
        try:
            if line == b"cheat allowcheats":
                box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)
            elif not box["done"]:
                box["done"] = True
                if mode == "reenter_timer":       # ToyBox 의 타이머가 그 안에서 다시 온다
                    user32.SendMessageW(game.hwnd, WM_TIMER, TIMER_ID, 0)
                elif mode == "reenter_keyup":     # 실제 키보드의 뗌(스캔 코드가 있다)이 그 안에서 온다
                    user32.SendMessageW(game.hwnd, WM_KEYUP, 0x41, 0xC01E0001)
                else:                             # 그 안에서 화면을 한 번 내보낸다
                    PRESENT(game.table[SLOT_PRESENT])(game.swap, 0, 0)
                inside.append("ok")
        except OSError as error:
            inside.append(f"error:{(error.winerror or 0) & 0xFFFFFFFF:#x}")
        finally:
            box["depth"] -= 1

    handler = HANDLER(body)
    fake = box["fake"] = fake_game(hook, ctypes.cast(handler, ctypes.c_void_p).value)
    fake.play(176)
    game.open()
    game.got.clear()
    game.click(BUTTON)
    game.click(BUTTON)                                        # 둘째 명령이 대기열에 있는 채로 첫째가 실행된다
    game.wait(0.5)
    game.click(BUTTON)                                        # 그 뒤에도 ToyBox 가 살아 있어야 한다
    game.wait(0.4)
    print(f"inside={';'.join(inside) or '-'} deepest={box['deepest']} lines=" + "|".join(line.replace(" ", "_") for line in lines)
          + " text=" + game.text())
    return 0


MONEY = "tab:돈"


def run_money(hook: str, mode: str) -> int:
    """돈 탭: 내장 치트를 거치지 않고 국고를 고친다. 출력은 JSON 한 줄(보이지 않는 글은 "-")."""
    home = os.environ.get("SRTOYBOX_HOME")
    if mode == "money" and home:
        Path(home, "toybox.ini").write_text("money.amount=1234\n", encoding="utf-8")   # 입력란의 값(백만 달러). ToyBox 가 뜰 때 읽는다
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    inside: list[float] = []
    box: dict[str, FakeGame] = {}

    def body(_context, line):
        lines.append(line.decode())
        if line == b"cheat allowcheats":
            box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)
        elif mode == "money_reenter":                         # 게임의 함수가 일하는 도중에 국고 요청이 들어오고 ToyBox 의 타이머가 다시 온다
            game.click(MONEY)
            game.click("money:+10b")
            inside.append(box["fake"].treasury(176))
            user32.SendMessageW(game.hwnd, WM_TIMER, TIMER_ID, 0)
            inside.append(box["fake"].treasury(176))

    game.handler = HANDLER(body)                              # 게임이 살아 있는 동안 붙들어 둔다
    handler = crash_stub() if mode == "money_fault" else ctypes.cast(game.handler, ctypes.c_void_p).value
    fake = box["fake"] = fake_game(hook, handler, values=mode != "money_notfound")
    if mode == "money_fail":
        fake.lock(176)                                        # 플레이어 지역 객체가 읽기 전용 쪽에 있다(읽을 수는 있다)
    if mode == "money_unreadable":
        fake.set_stock(176, 9, float("nan"))                  # 값을 통째로 믿지 않는다(read_values)
    if mode != "money_menu":
        fake.play(176)
    game.hotkey()                                             # 창이 열리면 첫 탭이 "돈"이다
    game.got.clear()
    facts = game.facts()
    out: dict[str, object] = {"now": game.shown("money:now"), "off": game.shown("money:off"), "hint": game.shown("hint")}
    out["cheat_buttons"] = sorted(name for name in facts if name.startswith("run:"))
    out["buttons"] = sorted(name for name in facts
                            if name.startswith("money:") and name not in ("money:now", "money:off", "money:amount", "money:keep",
                                                                           "money:keepvalue"))     # 단추만 — 최소 유지의 칸은 따로 본다

    def press(name: str) -> float:
        game.click(name)
        game.wait(0.2)                                        # 쓰는 것은 다음 타이머에서다
        return fake.treasury(176)

    if mode == "money":
        out["steps"] = [press(name) for name in ("money:+10b", "money:+100b", "money:-10b", "money:-100b", "money:zero",
                                                 "money:add", "money:add", "money:sub", "money:+10b", "money:set")]
        out["amount"], out["now_after"], out["wrote"] = game.shown("money:amount"), game.shown("money:now"), game.shown("wrote")
    elif mode == "money_menu":
        out["steps"] = [press("money:+10b")]
        out["status"] = game.shown("status")
    elif mode == "money_leave":
        game.click("money:+10b")                              # 눌렀다. 쓰는 것은 다음 타이머에서다(메시지를 돌릴 때 온다)
        fake.menu()                                           # 그 전에 게임에서 나갔다
        game.wait(0.3)
        out["dropped"], out["unwritten"] = fake.treasury(176), game.shown("unwritten")
        fake.play(176)
        out["steps"] = [press("money:+10b")]                  # 돌아오면 다시 된다
        out["unwritten_after"] = game.shown("unwritten")
    elif mode == "money_fail":
        out["steps"] = [press("money:+10b")]
        out["off_after"] = game.shown("money:off")
        out["buttons_after"] = sorted(name for name in game.facts() if name.startswith("money:") and name != "money:off")
    elif mode == "money_reenter":
        game.click(CHEAT_TAB)
        game.click(BUTTON)                                    # 게임의 함수를 부르는 단추 — 그 안에서 body 가 국고 단추를 누른다
        game.wait(0.5)
        out["inside"], out["after"] = inside, fake.treasury(176)
    elif mode == "money_fault":
        game.click(CHEAT_TAB)
        game.click(BUTTON)                                    # 게임의 함수를 부르는 단추 — 그 함수 안에서 죽는다
        game.wait(0.5)
        game.click(MONEY)
        game.click("money:+10b")                              # 오류 가드가 걸린 뒤의 국고 요청
        game.wait(0.5)
        out["after"], out["fault"] = fake.treasury(176), game.shown("fault")
    elif mode == "money_hint":
        game.click(STOCK)
        out["stock_hint"] = game.shown("hint")
        game.click(CHEAT_TAB)
        out["cheat_hint"] = game.shown("hint")
    elif mode in ("money_write_off", "money_notfound", "money_unread", "money_unreadable"):
        game.click(STOCK)                                     # 물자 탭도 같은 까닭 한 줄뿐이어야 한다
        facts = game.facts()
        out["stock_off"] = game.shown("stock:off")
        out["stock_rows"] = sorted(name for name in facts if name.startswith("stock:") and name != "stock:off")
        out["stock_cheats"] = sorted(name for name in facts if name.startswith("run:"))
    out["lines"], out["text"], out["options"], out["poland"] = lines, game.text(), fake.peek(OPTIONS, "<I"), fake.treasury(141)
    print(json.dumps(out, ensure_ascii=False))
    return 0


STOCK = "tab:물자"
STOCK_SLOTS_SEEN = (0, 3, 7, 11, 5)     # run_stock 이 재고를 적어 내는 칸: 쓰는 물자 넷과 쓰지 않는 물자 하나


def stock_rows(facts: dict) -> list[str]:
    """물자 탭에 그려진 줄들의 이름("stock:all", "stock:3" …), 그린 순서대로."""
    return [name for name in facts if name.startswith("stock:") and name.count(":") == 1 and name != "stock:off"]


def type_into(game: Game, name: str, digits: str) -> None:
    """그 입력란을 누르고(지금 값이 통째로 골라진다) 숫자를 친다. Enter 는 누르지 않는다."""
    game.click(name)
    game.present(3)
    for char in digits:
        user32.SendMessageW(game.hwnd, WM_CHAR, ord(char), 1)
        game.present()
    game.present(3)


def run_keep(hook: str, mode: str) -> int:
    """최소 유지. 출력은 JSON 한 줄(보이지 않는 글은 "-").

    설정(ToyBox 가 뜰 때 읽는다): 국고의 유지는 꺼져 있고 값이 20000(백만 달러), 석유(3)는 켜져 있고 500만, 석탄(4)은 켜져 있고 100.
    독일은 물자 0(농산물 1000) · 3(석유 250만) · 7(전력 0)을 쓴다 — 석탄은 keep_menu 에서만 쓰는 판이다.
    기다리는 시간(0.8초)은 유지를 보는 간격(0.5초)보다 넉넉히 길다.
    """
    home = os.environ.get("SRTOYBOX_HOME")
    if home:
        ini = "keep.treasury.value=20000\nkeep.stock.3=1\nkeep.stock.3.value=5000000\n"
        ini += {"keep_type": "", "keep_reenter": "keep.treasury=1\n"}.get(mode, "keep.stock.4=1\nkeep.stock.4.value=100\n")
        Path(home, "toybox.ini").write_text(ini, encoding="utf-8")
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    inside: list[float] = []

    def body(_context, line):                                 # keep_reenter 에서만 불린다(옮기지 않은 기능의 직접 실행)
        lines.append(line.decode())
        if line == b"cheat allowcheats":
            fake.poke(OPTIONS, "<I", fake.peek(OPTIONS, "<I") | 0x40)
            return
        fake.set_treasury(176, 1e9)                           # 게임의 함수가 국고를 고치는 중이다(바닥 아래로)
        time.sleep(0.6)                                       # 유지를 볼 때가 지났다
        user32.SendMessageW(game.hwnd, WM_TIMER, TIMER_ID, 0)  # 그 안에서 ToyBox 의 타이머가 다시 온다
        inside.append(fake.treasury(176))

    game.handler = HANDLER(body)                              # 게임이 살아 있는 동안 붙들어 둔다
    fake = fake_game(hook, ctypes.cast(game.handler, ctypes.c_void_p).value)
    for slot, amount in ((0, 1000.0), (3, 2.5e6), (7, 0.0)):
        fake.use(slot)
        fake.set_stock(176, slot, amount)
    out: dict[str, object] = {}
    seen = lambda: [fake.treasury(176), fake.stock(176, 3), fake.stock(176, 4)]
    keeps = lambda facts: {name: [facts[name + ":keep"][3], facts[name + ":keepvalue"][3]] for name in stock_rows(facts)
                           if name != "stock:all"}
    if mode == "keep":
        fake.play(176)
        game.wait(0.8)                                        # 창은 닫혀 있다. 유지는 타이머에서 돈다
        out["closed"] = seen()
        game.hotkey()                                         # 창이 열리면 첫 탭이 "돈"이다
        game.got.clear()
        out["status"] = game.shown("status")
        out["money"] = [game.shown("money:keep"), game.shown("money:keepvalue")]
        game.click("money:keep")                              # 국고의 유지를 켠다
        game.wait(0.8)
        out["on"], out["status_on"] = fake.treasury(176), game.shown("status")
        game.click("money:-10b")                              # 바닥 아래로 내린다 — 다음 검사에서 돌아온다
        game.wait(0.8)
        out["back"] = fake.treasury(176)
        game.click("money:keep")                              # 끈다
        game.click("money:-10b")
        game.wait(0.8)
        out["off"], out["status_off"] = fake.treasury(176), game.shown("status")
        game.click(STOCK)
        facts = game.facts()
        out["rows"] = [[name, facts[name][3], facts[name + ":now"][3]] for name in stock_rows(facts)]
        out["keeps"] = keeps(facts)
        game.click("stock:4:+1m")                             # 쓰지 않는 물자의 단추 — 꺼져 있다
        game.wait(0.2)
        out["unused_press"] = fake.stock(176, 4)
        game.click("stock:4:keep")                            # 석탄의 유지를 끈다 — 줄이 없어진다
        game.click("stock:3:keep")                            # 석유의 유지를 끈다
        game.click("stock:3:zero")
        game.wait(0.8)
        out["stock_off"] = [fake.stock(176, 3), fake.stock(176, 4)]
        out["status_end"], out["rows_end"] = game.shown("status"), stock_rows(game.facts())
    elif mode == "keep_menu":
        fake.use(4)                                           # 석탄을 쓰는 판
        game.wait(0.8)
        game.hotkey()
        game.got.clear()
        out["status"], out["rested"] = game.shown("status"), seen()[:2]
        game.click(STOCK)
        out["keeps"] = keeps(game.facts())
        game.click("stock:3:keep")                            # 메뉴에서 석유의 유지를 끈다
        out["status_off"] = game.shown("status")
        fake.play(176)
        game.wait(0.8)
        out["entered"] = seen()
    elif mode == "keep_write_off":                            # 값 쓰기가 꺼져 있다 — 유지는 게임에 들어가도 돌지 않는다
        game.hotkey()
        out["status_menu"] = game.shown("status")
        fake.play(176)
        game.wait(0.8)
        out["status_game"], out["rested"] = game.shown("status"), seen()
    elif mode == "keep_reenter":
        fake.play(176)
        game.wait(0.8)
        out["before"] = fake.treasury(176)                    # 유지가 돌고 있다(국고 $20 B)
        game.open()
        game.click(BUTTON)                                    # 옮기지 않은 기능 하나(직접 실행) — 그 안에서 body 가 돈다
        game.wait(1.5)
        out["inside"], out["after"] = inside, fake.treasury(176)
    else:
        fake.play(176)
        game.hotkey()
        game.click(STOCK)
        game.wait(0.8)                                        # 석유가 500만으로 올라온다
        game.got.clear()
        amount = lambda: [fake.stock(176, 3), game.shown("stock:3:keepvalue")]
        type_into(game, "stock:3:keepvalue", "7000000")
        game.wait(0.8)                                        # 치는 동안의 값이 쓰였다면 그사이에 700만이 됐을 것이다
        out["typing"] = amount()
        user32.SendMessageW(game.hwnd, WM_KEYDOWN, 0x0D, 1)   # Enter
        user32.SendMessageW(game.hwnd, WM_KEYUP, 0x0D, 0xC0000001)
        game.wait(0.8)
        out["entered"] = amount()
        type_into(game, "stock:3:keepvalue", "9000000")
        game.click("stock:all")                               # 다른 곳(글)을 누른다
        game.wait(0.8)
        out["clicked_away"] = amount()
        type_into(game, "stock:3:keepvalue", "1234")
        game.hotkey()                                         # 치던 채로 창을 닫는다
        game.wait(0.8)
        game.hotkey()                                         # 다시 연다(물자 탭이 그대로 열려 있다)
        out["closed"] = amount()
    out["lines"], out["text"], out["options"] = lines, game.text(), fake.peek(OPTIONS, "<I")
    print(json.dumps(out, ensure_ascii=False))
    return 0


def run_stock(hook: str, mode: str) -> int:
    """물자 탭: 내장 치트를 거치지 않고 물자 재고를 고친다. 출력은 JSON 한 줄(보이지 않는 글은 "-").

    독일은 물자 0(농산물 1000) · 3(석유 250만) · 7(전력 0) · 11(이름 없는 열두째 칸, 5)을 쓴다. 폴란드의 석유는 777 이다.
    """
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    game.handler = HANDLER(lambda _context, line: lines.append(line.decode()))   # 불리면 안 된다
    fake = fake_game(hook, ctypes.cast(game.handler, ctypes.c_void_p).value)
    for slot, amount in ((0, 1000.0), (3, 2.5e6), (7, 0.0), (11, 5.0)):
        fake.use(slot)
        fake.set_stock(176, slot, amount)
    fake.set_stock(141, 3, 777.0)
    if mode != "stock_menu":
        fake.play(176)
    game.hotkey()
    game.click(STOCK)
    game.got.clear()
    facts = game.facts()
    rows = [name for name in facts if name.startswith("stock:") and name.count(":") == 1 and name != "stock:off"]   # 그린 순서대로
    out: dict[str, object] = {"off": game.shown("stock:off"), "hint": game.shown("hint"),
                              "rows": [[name, facts[name][3], facts[name + ":now"][3]] for name in rows],
                              "cheat_buttons": sorted(name for name in facts if name.startswith("run:")),
                              "buttons": sorted(name[len("stock:3:"):] for name in facts
                                                if name.startswith("stock:3:") and name[len("stock:3:"):] not in ("now", "keep", "keepvalue"))}

    def press(name: str) -> list[float]:
        game.click(name)
        game.wait(0.2)                                        # 쓰는 것은 다음 타이머에서다
        return [fake.stock(176, slot) for slot in STOCK_SLOTS_SEEN]

    if mode == "stock":
        out["steps"] = [press(name) for name in ("stock:3:+1m", "stock:3:-100m", "stock:3:+100m", "stock:3:zero", "stock:all:+1m",
                                                 "stock:all:-100m")]
        out["wrote_all"] = game.shown("wrote")
        out["steps"].append(press("stock:0:+100m"))
        out["wrote"], out["now_after"] = game.shown("wrote"), game.shown("stock:0:now")
    else:
        out["steps"] = [press("stock:3:+1m")]
        out["status"] = game.shown("status")
    out["lines"], out["text"], out["options"], out["poland"] = lines, game.text(), fake.peek(OPTIONS, "<I"), fake.stock(141, 3)
    print(json.dumps(out, ensure_ascii=False))
    return 0


RESEARCH, PEOPLE = "tab:연구", "tab:인구·여론"


def run_more(hook: str, mode: str) -> int:
    """직접 쓰는 줄 넷: 내장 치트를 거치지 않고 기술 수준 · 세계 시장 여론 · 관계를 고친다. 출력은 JSON 한 줄(보이지 않는 글은 "-").

    fake_game 의 게임에 덴마크(150, 1201)를 더했다. 독일 ↔ 덴마크의 관계는 모두 0.5 다.
    """
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    inside: list[float] = []
    box: dict[str, FakeGame] = {}

    def body(_context, line):
        lines.append(line.decode())
        if line == b"cheat allowcheats":
            box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)
        elif mode == "more_reenter":                          # 게임의 함수가 일하는 도중에 요청이 들어오고 ToyBox 의 타이머가 다시 온다
            game.click(RESEARCH)
            game.click("run:finalexam")
            inside.append(box["fake"].tech(176))
            user32.SendMessageW(game.hwnd, WM_TIMER, TIMER_ID, 0)
            inside.append(box["fake"].tech(176))

    game.handler = HANDLER(body)                              # 게임이 살아 있는 동안 붙들어 둔다
    handler = crash_stub() if mode == "more_fault" else ctypes.cast(game.handler, ctypes.c_void_p).value
    fake = box["fake"] = fake_game(hook, handler, values=mode != "more_alone",
                                   more={**MORE, "tech": 0} if mode == "more_notfound" else MORE)
    fake.region(150, 1201, alive=3)
    fake.set_relation(176, 150, (0.5, 0.5, 0.5))
    fake.set_relation(150, 176, (0.5, 0.5, 0.5))
    fake.set_people(176, (82615760.0, 3632.0), 0.375)
    fake.set_people(141, (38e6, 2500.0), 0.5)
    if mode == "more_fail":
        fake.lock(141)                                        # 폴란드의 객체가 읽기 전용 쪽에 있다(읽을 수는 있다)
    if mode != "more_menu":
        fake.play(176)
    game.hotkey()
    game.got.clear()
    out: dict[str, object] = {}

    def seen(tab: str) -> dict[str, str]:
        """그 탭에 그려진 줄들을 그린 순서대로: "run:<id>" 는 단추(글은 이름), "off:<id>" 는 단추 대신 나온 까닭 한 줄."""
        game.click(tab)
        facts = game.facts()
        return {name: facts[name][3] for name in facts if name.startswith(("run:", "off:"))}

    def state() -> dict[str, object]:
        return {"tech": fake.tech(176), "opinion": fake.opinion(176), "poland": [fake.relation(176, 141), fake.relation(141, 176)],
                "denmark": [fake.relation(176, 150), fake.relation(150, 176)]}

    def press(name: str) -> None:
        game.click(name)
        game.wait(0.2)                                        # 쓰는 것은 다음 타이머에서다

    if mode in ("more", "more_alone", "more_notfound", "more_write_off", "more_unread", "more_menu"):
        out["research"] = seen(RESEARCH)
        if "run:finalexam" in out["research"]:
            press("run:finalexam")
            press("run:finalexam")
        out["wrote_tech"] = game.shown("wrote")
        out["people"] = seen(PEOPLE)
        if "run:shelovesme" in out["people"]:
            press("run:shelovesme")
        out["wrote_opinion"] = game.shown("wrote")
        if "run:populate" in out["people"]:                   # 인구 +100만 · 지지율 100% (3단계 4)
            press("run:populate")
            out["wrote_people"] = game.shown("wrote")
            press("run:approval")
            out["wrote_approval"] = game.shown("wrote")
        out["people_now"] = [fake.people(176), fake.people(141)]
        out["diplomacy"] = seen(DIPLOMACY)
        if "run:love" in out["diplomacy"]:
            press("run:love")                                 # 아직 나라를 고르지 않았다 — 단추가 꺼져 있다
            out["unpicked"] = state()
            game.wait(1.2)                                    # 나라 목록은 1초마다 읽는다
            if mode != "more_menu":
                game.click("row:1106")
            press("run:love")
            out["after_love"], out["wrote_love"] = state(), game.shown("wrote")
            press("run:neutral")
            out["wrote_neutral"] = game.shown("wrote")
            if "run:colonize" in out["diplomacy"] and mode == "more":   # 식민지화(3단계 4): 게임의 함수를 부른다. 한 번 더 눌러야 한다
                press("run:colonize")
                out["colonize_asking"], out["colonized_first"] = game.shown("run:colonize"), list(fake.colonized)
                press("run:colonize")
                out["wrote_colonize"] = game.shown("wrote")
            out["colonized"] = fake.colonized
            if "run:fight" in out["diplomacy"] and mode == "more":      # 전쟁 붙이기(3단계 4): 지도에서 고른 나라 → 목록에서 고른 나라
                scroll_to(game, "run:fight")                          # 탭의 아래쪽에 있다
                out["fight_unpicked"] = game.facts()["fight:map"][3]
                press("run:fight")                                    # 지도에서 고르지 않았다 — 단추가 꺼져 있다
                out["fight_asking_unpicked"] = game.shown("run:fight")
                fake.poke(MAP_PICK + 8, "<H", 150)                    # 게임의 지도에서 덴마크를 골랐다
                game.wait(0.2)
                out["fight_map"] = game.facts()["fight:map"][3]
                press("run:fight")
                out["fight_asking"], out["fought_first"] = game.shown("run:fight"), list(fake.fought)
                press("run:fight")
                out["wrote_fight"] = game.shown("wrote")
            out["fought"] = fake.fought
        out["hint"], out["status"] = game.shown("hint"), game.shown("status")
    elif mode == "more_leave":
        game.click(RESEARCH)
        game.click("run:finalexam")                           # 눌렀다. 쓰는 것은 다음 타이머에서다(메시지를 돌릴 때 온다)
        fake.menu()                                           # 그 전에 게임에서 나갔다
        game.wait(0.3)
        out["dropped"], out["unwritten"] = fake.tech(176), game.shown("unwritten")
        fake.play(176)
        press("run:finalexam")                                # 돌아오면 다시 된다
        out["unwritten_after"] = game.shown("unwritten")
    elif mode == "more_annex":                                # 병합(3단계 4): 지도에서 그 나라의 칸마다 게임의 함수를 부른다
        game.click(DIPLOMACY)
        game.wait(1.2)
        game.click("row:1201")                                # 덴마크(인덱스 150)
        press("run:annex")
        out["asking"], out["first"] = game.facts()["run:annex"][3], list(fake.annexed)
        press("run:annex")
        out["annexed"], out["wrote"], out["owners"] = list(fake.annexed), game.shown("wrote"), fake.owners()
        press("run:annex")
        press("run:annex")                                    # 한 번 더 — 이제 덴마크의 땅이 없다
        out["again"], out["none"] = len(fake.annexed), game.shown("unwritten")
        struct.pack_into("<i", fake.map, 0x88, 30)            # 지도의 꼴이 어긋났다(칸 수가 너비의 배수가 아니다) — 한 칸도 부르지 않는다
        game.click("row:1106")
        press("run:annex")
        press("run:annex")
        out["broken"], out["broken_calls"] = game.shown("unwritten"), len(fake.annexed)
    elif mode == "more_treaty":                               # 동맹 맺기(3단계 4): 게임의 조약 함수를 네 번 부른다. 이미 동맹이면 부르지 않는다
        game.click(DIPLOMACY)
        game.wait(1.2)
        out["unpicked"] = game.click("run:treaty")            # 나라를 고르지 않았다 — 단추가 꺼져 있다
        game.wait(0.3)
        out["treaties_unpicked"] = list(fake.treaties)
        game.click("row:1106")
        press("run:treaty")
        out["treaties"], out["wrote"] = list(fake.treaties), game.shown("wrote")
        fake.allies[141 // 64] |= 1 << (141 % 64)             # 게임이 독일의 동맹에 폴란드를 올렸다
        press("run:treaty")
        out["again"], out["already"] = list(fake.treaties), game.shown("unwritten")
    elif mode == "more_novichok":                             # 지도자 제거(3단계 4): 깃발에 비트를 켜고 게임의 함수 둘을 부른다
        game.click(DIPLOMACY)
        game.wait(1.2)
        game.click("row:1106")
        scroll_to(game, "run:becomeregion")                   # 맨 아래까지 내린다 — 그 위의 단추가 탭 안쪽에 온다
        press("run:novichok")
        out["asking"], out["first"] = game.facts()["run:novichok"][3], list(fake.succession)
        out["flags_first"] = struct.unpack_from("<I", fake.leaders, 3 * 0xC8 + 0x28)[0]
        press("run:novichok")
        out["succession"], out["wrote"] = fake.succession, game.shown("wrote")
        out["flags"] = [struct.unpack_from("<I", fake.leaders, n * 0xC8 + 0x28)[0] for n in range(16)]
    elif mode == "more_become":                               # 이 나라로 플레이(3단계 4): 전역 넷을 쓰고 게임의 함수를 부른다
        game.click(DIPLOMACY)
        game.wait(1.2)
        game.click("row:1106")
        scroll_to(game, "run:becomeregion")
        fake.poke(INDEX2, "<i", 150)                          # 둘째 사본이 첫째와 다르다 — 쓰지 않는다
        fake.poke(POINTER2, "<Q", fake.where[176])
        press("run:becomeregion")
        out["asking"] = game.facts()["run:becomeregion"][3]
        press("run:becomeregion")
        out["mismatch"], out["became_mismatch"], out["index_mismatch"] = game.shown("unwritten"), list(fake.became), fake.peek(0x20, "<i")
        fake.poke(INDEX2, "<i", 176)                          # 이제 맞는다
        fake.poke(POINTER2, "<Q", fake.where[150])            # 둘째 포인터는 달라도 된다(게임이 첫 자정부터 전날 사본으로 돌린다)
        scroll_to(game, "run:becomeregion")
        press("run:becomeregion")
        out["became_first"] = list(fake.became)
        press("run:becomeregion")
        game.wait(1.2)
        out["became"], out["wrote"], out["status_after"], out["picked_after"] = fake.became, game.shown("wrote"), game.shown("status"), game.shown("picked")
    elif mode == "more_repick":
        game.click(DIPLOMACY)
        game.wait(1.2)                                        # 나라 목록은 1초마다 읽는다
        game.click("row:1106")
        game.click("run:love")                                # 눌렀다. 쓰는 것은 다음 타이머에서다(메시지를 돌릴 때 온다)
        game.click("row:1201")                                # 그 전에 덴마크로 바꿔 골랐다
        game.wait(0.3)
        out["picked"], out["wrote"] = game.shown("picked"), game.shown("wrote")
    elif mode == "more_fail":
        game.click(DIPLOMACY)
        game.wait(1.2)
        game.click("row:1106")
        press("run:love")
        out["diplomacy"] = seen(DIPLOMACY)
        out["research"] = seen(RESEARCH)
        game.click(MONEY)
        out["money_off"] = game.shown("money:off")
    elif mode in ("more_reenter", "more_fault"):
        game.click(CHEAT_TAB)
        game.click(BUTTON)                                    # 게임의 함수를 부르는 단추(more_reenter: 그 안에서 body 가 지식 순위를 누른다)
        game.wait(0.5)
        if mode == "more_fault":
            game.click(RESEARCH)
            game.click("run:finalexam")                       # 오류 가드가 걸린 뒤의 요청
            game.wait(0.5)
        out["inside"], out["fault"] = inside, game.shown("fault")
    out["state"], out["lines"], out["text"], out["options"] = state(), lines, game.text(), fake.peek(OPTIONS, "<I")
    out["treasury"] = fake.treasury(176)
    print(json.dumps(out, ensure_ascii=False))
    return 0


def run_research(hook: str, mode: str) -> int:
    """연구의 두 줄: 내장 치트를 거치지 않고 기술 · 부대 설계의 보유를 고친다. 출력은 JSON 한 줄(보이지 않는 글은 "-").

    held 는 [독일의 기술, 독일의 부대 설계, 폴란드의 기술, 폴란드의 부대 설계] — 처음에는 [[1, 4, 6], [10, 13, 14], [1, 2], [11, 13]].
    """
    game = start_game(hook)
    if game is None:
        return 0
    lines: list[str] = []
    inside: list[int] = []
    box: dict[str, FakeGame] = {}

    def body(_context, line):
        lines.append(line.decode())
        if line == b"cheat allowcheats":
            box["fake"].poke(OPTIONS, "<I", box["fake"].peek(OPTIONS, "<I") | 0x40)

    def recompute(_world, index):
        box["fake"].recomputed.append(index)
        if mode == "research_reenter":                        # 게임의 함수가 일하는 도중에 ToyBox 의 타이머가 다시 온다
            inside.append(len(lines))
            user32.SendMessageW(game.hwnd, WM_TIMER, TIMER_ID, 0)
            inside.append(len(lines))

    game.handler, game.recompute = HANDLER(body), RECOMPUTE(recompute)      # 게임이 살아 있는 동안 붙들어 둔다
    fake = box["fake"] = fake_game(hook, ctypes.cast(game.handler, ctypes.c_void_p).value, research=mode != "research_notfound",
                                   recompute=crash_stub() if mode == "research_fault" else ctypes.cast(game.recompute, ctypes.c_void_p))
    if mode != "research_menu":
        fake.play(176)
    game.hotkey()
    game.got.clear()
    out: dict[str, object] = {}

    def held() -> list[list[int]]:
        lab = fake.lab
        return [lab.held(TECH, 64, 176), lab.held(DESIGN, 64, 176), lab.held(TECH, 64, 141), lab.held(DESIGN, 64, 141)]

    def press(name: str) -> None:
        game.click(name)
        game.wait(0.2)                                        # 쓰는 것은 다음 타이머에서다

    game.click(RESEARCH)
    facts = game.facts()
    out["rows"] = {name: facts[name][3] for name in facts if name.startswith(("run:", "off:", "value:"))}
    if mode in ("research", "research_menu"):
        press("run:e=mc2")
        out["after_queue"], out["wrote_queue"] = held(), game.shown("wrote")
        press("run:technology")
        out["wrote_level"] = game.shown("wrote")
        press("run:e=mc2")                                    # 한 번 더 — 대기열이 비었다
        out["unwritten"] = game.shown("unwritten")
    elif mode == "research_leave":
        game.click("run:e=mc2")                               # 눌렀다. 쓰는 것은 다음 타이머에서다(메시지를 돌릴 때 온다)
        fake.menu()                                           # 그 전에 게임에서 나갔다
        game.wait(0.3)
        out["dropped"], out["unwritten"] = held(), game.shown("unwritten")
        fake.play(176)
        press("run:e=mc2")                                    # 돌아오면 다시 된다
        out["unwritten_after"] = game.shown("unwritten")
    elif mode == "research_fault":
        press("run:e=mc2")
        out["fault"] = game.shown("fault")
        out["after_fault"] = held()
        press("run:technology")                               # 오류 가드가 걸렸다 — 단추가 꺼져 있다
    elif mode == "research_list":
        items = lambda: {name[5:]: fact[3] for name, fact in game.facts().items() if name.startswith("item:")}
        game.wait(0.8)                                        # 목록의 스냅숏은 탭이 보이는 동안 0.5초마다 뜬다
        out["picked"], out["first"], out["count"] = game.shown("picked"), items(), game.shown("list:count")
        game.click("list:owner")                              # 보유국: 그 나라가 보유한 것만(보기와 함께 걸린다)
        game.wait(0.2)
        owners = {fact[3]: name for name, fact in game.facts().items() if name.startswith("owner:")}
        out["owners"] = list(owners)
        game.click(next(name for label, name in owners.items() if label.startswith("폴란드")))
        game.wait(0.8)                                        # 고른 나라가 바뀌면 스냅숏을 다시 뜬다
        out["by_owner"], out["owner"], out["picked_owner"] = items(), game.shown("list:owner"), game.shown("picked")
        game.click("list:owner")
        game.click("owner:0")                                 # 전체 — 고른 나라(와 그 열)는 남는다
        game.wait(0.3)
        out["owner_all"] = game.shown("list:owner")
        game.click("item:3")
        game.click("item:7")
        out["count_chosen"] = game.shown("list:count")
        press("list:done")                                    # 고른 것 완료: 기술 3(+ 선행 2) · 7
        game.wait(0.3)
        out["after_done"], out["wrote_done"], out["left"], out["count_left"] = held(), game.shown("wrote"), items(), game.shown("list:count")
        game.click("list:show")
        game.click("show:1")                                  # 보기: 자국 보유
        game.wait(0.2)
        out["mine"] = items()
        order = []
        for head in ("head:1", "head:1", "head:6", "head:3"):  # 머리 줄을 누르면 그 열로 놓는다: 이름 ↑ · 이름 ↓ · 보유국 ↑ · 수준 ↑(처음 순서)
            game.click(head)
            game.wait(0.2)
            order.append(list(items()))
        out["sorted"] = order
        game.click("list:designs")                            # 목록을 바꾸면 고른 것이 풀린다
        game.wait(0.2)
        out["designs"], out["count_designs"] = items(), game.shown("list:count")
        game.click("list:all_undo")                           # 한 번 더 눌러야 실행된다
        game.wait(0.2)
        out["asking"], out["held_asking"] = game.shown("list:all_undo"), held()
        press("list:all_undo")
        game.wait(0.3)
        out["wrote_undo"], out["designs_after"] = game.shown("wrote"), items()
    elif mode == "research_units":                            # 부대 탭의 설계도 목록: 영역으로 거르고, 등록 · 해제, 모든 설계도
        items = lambda: {name[5:]: fact[3] for name, fact in game.facts().items() if name.startswith("item:")}
        game.click("tab:부대")
        game.wait(0.8)
        facts = game.facts()
        out["buttons"] = {name: facts[name][3] for name in facts if name.startswith("list:") and name not in ("list:count", "list:find")}
        out["first"], out["count"] = items(), game.shown("list:count")
        game.click("list:domain")
        game.click("domain:1")                                # 공군 — 이 판에는 없다
        game.wait(0.2)
        out["air"] = items()
        game.click("list:domain")
        game.click("domain:0")                                # 육군
        game.wait(0.2)
        out["land"] = items()
        scroll_to(game, "list:every_done")
        press("list:every_done")                              # 모든 설계도 등록 — 선행 기술도 함께
        game.wait(0.3)
        out["after_every"], out["wrote_every"] = held(), game.shown("wrote")
        scroll_to(game, "list:every_undo")
        press("list:every_undo")                              # 모든 설계도 해제 — 한 번 더 눌러야 한다
        out["asking"], out["held_asking"] = game.facts()["list:every_undo"][3], held()
        press("list:every_undo")
        game.wait(0.3)
        out["after_none"], out["wrote_none"] = held(), game.shown("wrote")
        names = ctypes.create_string_buffer(256)
        toybox_dll = ctypes.WinDLL(str(Path(hook).with_name("srtoybox.dll")))
        toybox_dll.srtoybox_owner_names(0, 1, names, len(names))
        out["owners_of_tech_1"] = names.value.decode("utf-8")
    elif mode == "research_gui":                              # GUI 숨기기/보이기(3단계 6): 화면의 깃발을 뒤집고 GUI 객체의 가상 함수를 부른다
        state = lambda: [hex(fake.peek(UI_FLAGS, "<I")), hex(struct.unpack_from("<I", fake.gui, 0x2C)[0]), list(fake.gui_calls)]
        fake.poke(GUI_VTABLE + 8 * 5, "<Q", ctypes.cast(fake.gui_set, ctypes.c_void_p).value)   # 몸통 없이, 가짜 함수만
        game.click("tab:화면·진행")
        game.wait(0.5)
        press("run:fullmapshow")
        out["hidden"], out["wrote"] = state(), game.shown("wrote")
        press("run:fullmapshow")
        out["back"], out["wrote_back"] = state(), game.shown("wrote")
        struct.pack_into("<Q", fake.gui, 0, fake.base + GUI_VTABLE + 8)     # 객체의 함수 표가 찾아 둔 표가 아니다 — 깃발도 쓰지 않는다
        press("run:fullmapshow")
        out["other_table"], out["after_other"] = game.shown("unwritten"), state()[:2] + [len(fake.gui_calls)]
        fake.poke(GUI, "<Q", 0)                               # GUI 객체가 없다 — 깃발만 쓴다(치트도 그렇다)
        press("run:fullmapshow")
        out["no_object"] = [hex(fake.peek(UI_FLAGS, "<I")), len(fake.gui_calls)]
    elif mode == "research_victory":                          # 즉시 승리(3단계 6): 전역 둘에 쓰고 게임 창에 메시지를 보낸다
        def state():
            over, bundle = fake.peek(GAME_OVER, "<I"), fake.peek(WINNERS, "<Q")
            bits = ctypes.string_at(bundle, 128) if bundle else b""
            return [over, [i for i in range(len(bits) * 8) if bits[i // 8] >> (i % 8) & 1], [list(m) for m in game.got if m[0] == "app"]]
        game.click(CHEAT_TAB)
        game.wait(0.5)
        press("run:instantwin")
        out["asking"], out["first"] = game.facts()["run:instantwin"][3], state()
        press("run:instantwin")
        game.wait(0.3)
        out["won"], out["wrote"] = state(), game.shown("wrote")
        press("run:instantwin")
        press("run:instantwin")                               # 이미 끝난 판
        game.wait(0.3)
        out["again"], out["again_messages"] = game.shown("unwritten"), len(state()[2])
    elif mode == "research_best":                             # 최강 부대 묶음(3단계 5): 병과마다 능력치가 가장 높은 설계를, 게임이 정한 배치 칸에
        lab = fake.lab
        for number in range(1, 64):                           # 이 판의 설계는 아래 넷뿐이게 한다(빈 자리 = 첫 칸이 0)
            struct.pack_into("<Q", lab.designs, number * 0x168, 0)

        def design(number, cls, stat, value, move):
            lab.design(number, cls=cls, name=b"Best")
            struct.pack_into("<f", lab.designs, number * 0x168 + 0x84 + 4 * stat, value)
            lab.designs[number * 0x168 + 0x12] = move
        design(20, 2, 1, 5.0, 0)                              # 전차(육군. 능력치 1번): 5
        design(21, 2, 1, 9.0, 0)                              # 전차: 9 — 이것이 뽑힌다
        design(22, 16, 6, 3.0, 3)                             # 주력함(해군. 능력치 6번)
        design(23, 8, 0, 99.0, 0)                             # 미사일 — 받지 않는 병과다
        game.click("tab:부대")
        game.wait(0.8)
        out["domain"] = game.facts()["best:domain"][3]
        press("run:darran")
        out["asking"], out["first"] = game.facts()["run:darran"][3], list(fake.spawned)
        press("run:darran")                                   # 모든 영역, 7기씩
        out["all"], out["deployed"], out["wrote"] = list(fake.spawned), list(fake.deployed), game.shown("wrote")
        game.click("best:domain")
        game.click("best:domain:0")                           # 육군만
        game.wait(0.2)
        press("run:darran")
        press("run:darran")
        out["land"], out["wrote_land"] = fake.spawned[len(out["all"]):], game.shown("wrote")
        game.click("best:domain")
        game.click("best:domain:1")                           # 공군 — 이 판에는 없다
        game.wait(0.2)
        press("run:darran")
        press("run:darran")
        out["air_calls"], out["air"] = len(fake.spawned), game.shown("unwritten")
    elif mode == "research_supply":                           # 보급 채우기(3단계 5): 고른 내 부대의 보급 두 칸을 "가득"으로 · 0 으로
        lab = fake.lab
        lab.design(20, cls=0, name=b"Truck")
        at = 20 * 0x168
        struct.pack_into("<ff", lab.designs, at + 0x78, 10.0, 0.0)          # 첫째 칸의 용량 10
        struct.pack_into("<f", lab.designs, at + 0x80, 4.0)                 # 둘째 칸의 용량 4
        struct.pack_into("<8H", lab.designs, at + 0x38, 1, 0, 0, 0, 0, 0, 0, 0)   # 효과를 주는 기술: 1(독일 · 폴란드가 보유)
        struct.pack_into("<4H4f", lab.techs, 1 * 0x88 + 0xC, 0xA6, 0xA5, 0, 0, 0.5, 0.25, 0.0, 0.0)   # 첫째 × 1.5, 둘째 × 1.25
        record = 0x800 + UPGRADES + 5 * 0x78                  # 병과 0 · 칸 0 · 단계 1 의 레코드(번호 5)의 깃발
        fake.poke(record, "<I", 1 << 18)                      # 둘째 칸의 값은 곱하지 않고 그대로 쓴다
        fake.poke(record - 0x68 + 4 * 16, "<f", 0.1)          # 첫째 칸 × 1.1
        fake.poke(record - 0x68 + 4 * 18, "<f", 7.0)          # 둘째 칸 = 7

        def unit(owner):
            raw = (ctypes.c_ubyte * 0x238)()
            struct.pack_into("<H", raw, 0x1E, owner)
            struct.pack_into("<ff", raw, 0x80, 1.0, 1.0)
            struct.pack_into("<f", raw, 0x90, 2.0)            # 규모 2
            raw[0xDC], raw[0xE7], raw[0xF8] = 1, 1, 0x40
            struct.pack_into("<Q", raw, 0x230, ctypes.addressof(lab.designs) + at)
            return raw
        mine, theirs = unit(176), unit(141)
        nodes = [(ctypes.c_ubyte * 0x30)(), (ctypes.c_ubyte * 0x30)()]
        struct.pack_into("<Q", nodes[0], 8, ctypes.addressof(nodes[1]))
        struct.pack_into("<Q", nodes[0], 0x18, ctypes.addressof(mine))
        struct.pack_into("<Q", nodes[1], 0x18, ctypes.addressof(theirs))
        stores = lambda raw: [round(v, 3) for v in struct.unpack_from("<ff", raw, 0x80)]
        game.click("tab:부대")
        game.wait(0.8)
        out["unpicked"], out["off_click"] = game.facts()["supply:picked"][3], game.click("run:stranded")
        fake.poke(PICKED_UNITS, "<Q", ctypes.addressof(nodes[0]))           # 게임에서 부대 둘을 골랐다(하나는 폴란드의 것)
        game.wait(0.3)
        out["picked"] = game.facts()["supply:picked"][3]
        press("run:stranded")
        out["filled"], out["theirs"], out["wrote"] = stores(mine), stores(theirs), game.shown("wrote")
        # 내 부대 전부: 세계의 부대 목록에서 주인이 독일이고 시설이 아닌 것 — mine 과 second. 폴란드의 것과 독일의 시설은 건너뛴다
        facility, second, region = unit(176), unit(176), unit(176)
        struct.pack_into("<I", facility, 0xF4, 0x2000)
        struct.pack_into("<Q", region, 0x230, ctypes.addressof(lab.designs))   # 설계 0번: 게임이 지역마다 하나씩 두는 객체 — 부대가 아니다
        unbuilt = unit(176)
        struct.pack_into("<I", unbuilt, 0xF4, 0x800)          # 아직 지도에 없는 부대(생산 중으로 보인다) — 건드리지 않는다
        for before, after in ((mine, theirs), (theirs, facility), (facility, region), (region, unbuilt), (unbuilt, second)):
            struct.pack_into("<Q", before, 8, ctypes.addressof(after))
        fake.poke(UNITS, "<Q", ctypes.addressof(mine))
        stat = lambda raw: [round(v, 2) for v in struct.unpack_from("<3f", raw, 0x9C)]      # 효율 · 경험 · 사기

        def push(name):
            scroll_to(game, name, up=True)
            press(name)
        struct.pack_into("<ff", mine, 0x80, 0.0, 0.0)
        push("units:fill_all")
        out["all_filled"] = [stores(u) for u in (mine, theirs, facility, second)]
        out["region_object"], out["unbuilt"] = stores(region), stores(unbuilt)
        out["wrote_all"] = game.shown("wrote")
        push("units:stat_picked")                             # 경험 100% 를 고른 부대(mine · theirs)에
        out["stat_picked"], out["wrote_picked"] = [stat(u) for u in (mine, theirs, facility, second)], game.shown("wrote")
        game.click("units:stat")
        game.click("units:stat:1")                            # 사기
        game.wait(0.2)
        push("units:stat_all")
        out["stat_all"], out["wrote_stat_all"] = [stat(u) for u in (mine, theirs, facility, second)], game.shown("wrote")
        out["region_object"] += stat(region)
        out["unbuilt"] += stat(unbuilt)
        scroll_to(game, "empty:stranded", up=True)
        press("empty:stranded")
        out["emptied"], out["wrote_empty"] = stores(mine), game.shown("wrote")
        struct.pack_into("<Q", theirs, 0x230, 0)              # 부대 객체로 보이지 않는 것이 목록에 있다 — 하나도 쓰지 않는다
        press("run:stranded")
        out["broken"], out["after_broken"] = game.shown("unwritten"), stores(mine)
    elif mode == "research_spawn":                               # 부대 생성(3단계 5): 게임의 지도에서 고른 칸에, 게임의 함수로
        game.click("tab:부대")
        game.wait(0.8)
        facts = game.facts()
        out["unpicked"] = facts["list:hex"][3]
        out["rows"] = [name for name in facts if name.startswith("run:")]   # 번호로 만드는 줄은 없다
        game.click("list:spawn")                              # 칸을 고르지 않았다 — 꺼져 있다
        game.wait(0.3)
        fake.poke(MAP_HEX, "<I", 3 | 2 << 16)                 # 게임의 지도에서 (3, 2) 를 골랐다
        game.wait(0.3)
        out["picked"] = game.facts()["list:hex"][3]
        game.click("list:show")
        game.click("show:2")                                  # 보기: 자국 미보유 — 설계 11 · 12 가 맨 위에 온다
        game.wait(0.3)
        scroll_to(game, "item:11")
        game.click("item:11")
        scroll_to(game, "item:12")
        game.click("item:12")
        scroll_to(game, "list:spawn", up=True)
        press("list:spawn")                                   # 고른 설계 둘을 하나씩
        out["spawned"], out["wrote"] = list(fake.spawned), game.shown("wrote")
        stats = lambda: [list(struct.unpack_from("<3f", raw, 0x9C)) for raw in fake.made]   # [효율, 경험, 사기]
        out["plain"] = stats()                                # 상태를 정하지 않았다 — 게임이 준 값 그대로
        scroll_to(game, "list:stat:0", up=True)
        game.click("list:stat:0")                             # 경험을 켠다(100 %)
        game.click("list:stat:2")                             # 효율도
        game.wait(0.2)
        press("list:spawn")                                   # 둘을 더 만든다 — 새 부대에만 쓴다
        out["stated"], out["wrote_stated"] = stats(), game.shown("wrote")
        fake.poke(MAP_HEX, "<I", 9 | 2 << 16)                 # 지도(8 × 4) 밖의 칸 — 고르지 않은 것으로 친다
        game.wait(0.3)
        out["outside"] = game.facts()["list:hex"][3]
    elif mode == "research_reenter":
        game.click(CHEAT_TAB)
        game.click(BUTTON)
        game.click(BUTTON)                                    # 내장 치트로 도는 기능 둘이 실행기의 대기열에 든다
        game.click(RESEARCH)
        game.click("run:e=mc2")                               # 연구 요청도 든다
        game.wait(1.0)
        out["inside"] = inside
    if mode != "research_notfound":
        out["held"] = held()
        out["days"] = sorted({struct.unpack_from("<f", fake.lab.techs, n * 0x88 + 0x30)[0] for n in (1, 2, 3, 4, 6, 7)})
    out["hint"], out["status"] = game.shown("hint"), game.shown("status")
    out["recomputed"], out["lines"], out["text"], out["options"] = fake.recomputed, lines, game.text(), fake.peek(OPTIONS, "<I")
    print(json.dumps(out, ensure_ascii=False))
    return 0


def run_layout(hook: str, mode: str) -> int:
    """물자 탭이 창 안에 들어오는가. 독일은 물자 열하나를 모두 쓴다. 출력은 JSON 한 줄(보이지 않는 글은 "-").

    안내 글의 오른쪽 끝은 창이 스스로 적어 둔다(항목 help:money · help:stock 의 글이 그 x 좌표다).
    """
    home = os.environ.get("SRTOYBOX_HOME")
    if mode == "layout_saved" and home:                       # ImGui 가 저장해 둔 창의 자리와 크기(이미 써 본 사용자)
        Path(home, "imgui.ini").write_text("[Window][SR2030 ToyBox]\nPos=40,60\nSize=500,460\nCollapsed=0\n", encoding="utf-8")
    game = start_game(hook)
    if game is None:
        return 0
    fake = fake_game(hook, None)
    for slot in range(11):
        fake.use(slot)
        fake.set_stock(176, slot, 1000.0 * (slot + 1))
    fake.play(176)
    game.hotkey()
    out: dict[str, object] = {"money_help_right": game.shown("help:money")}
    game.click(STOCK)
    game.click("stock:0:+1m")                                 # 바닥에 "마지막으로 쓴 값" 줄이 생긴다
    game.wait(0.3)
    facts = game.facts()
    out["rows"] = len(stock_rows(facts))
    out["hidden"] = [name for name in stock_rows(facts) if not facts[name][2]]
    out["help_right"], out["wrote"] = game.shown("help:stock"), game.shown("wrote")
    print(json.dumps(out, ensure_ascii=False))
    return 0


def crash_once_stub(flag: int, real: int) -> int:
    """깃발(flag 의 바이트)이 서 있으면 내리고 0 번지에 쓴다. 아니면 real 로 뛴다 — 한 번만 죽는 "진짜 Present"."""
    code = kernel32.VirtualAlloc(None, 64, 0x3000, 0x40)
    body = (bytes([0x48, 0xB8]) + flag.to_bytes(8, "little")                 # mov rax, flag
            + bytes([0x80, 0x38, 0x00, 0x74, 0x0E])                           # cmp byte ptr [rax], 0 / je +14
            + bytes([0xC6, 0x00, 0x00])                                       # mov byte ptr [rax], 0
            + bytes([0xC7, 0x04, 0x25, 0, 0, 0, 0, 1, 0, 0, 0])               # mov dword ptr [0], 1
            + bytes([0x48, 0xB8]) + real.to_bytes(8, "little") + bytes([0xFF, 0xE0]))   # mov rax, real / jmp rax
    ctypes.memmove(code, body, len(body))
    return code


def run_present_fault(hook: str) -> int:
    """ToyBox 가 부른 다음 함수(진짜 Present 나 다른 훅) 안에서 예외가 나고 그것을 위에서 누가 잡는다 — 그 뒤에도 ToyBox 는 그려야 한다."""
    if not Path(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", "malgun.ttf").is_file():
        print("skip 맑은 고딕이 없다(설정 창의 단추 자리가 달라진다)")
        return 0
    game = Game(hook)
    if game.swap is None:
        print("nodevice")
        return 0
    table = vtable(game.swap)
    flag = ctypes.c_ubyte(0)
    write_slot(table, SLOT_PRESENT, crash_once_stub(ctypes.addressof(flag), table[SLOT_PRESENT]))   # ToyBox 가 끼어들 때 그 칸에 있던 것
    if not game.start():
        print("skip ToyBox 가 끼어들지 않았다")
        return 0
    flag.value = 1
    caught = 0
    try:
        game.present()
    except OSError:
        caught = 1
    game.hotkey()
    print(f"caught={caught} drawn={int('status' in game.facts())}")
    return 0


def run_input(hook: str, mode: str = "input") -> int:
    """글쇠와 누름이 제 쪽으로만 가는가: 단축키와 설정 창 위의 누름은 ToyBox 가, 창 밖의 누름은 게임이 받는다.
    ToyBox 는 게임에 글쇠를 넣지 않는다(text 가 비어 있다)."""
    game = start_game(hook)
    if game is None:
        return 0
    out = {"hotkey": game.open()}
    out["button"] = game.click(CHEAT_TAB)                     # 설정 창의 탭 — 게임에 가면 안 된다
    out["outside"] = game.click((900, 100))                   # 설정 창 밖(창은 40..760 x 60..660 이다) — 게임이 받아야 한다
    game.got.clear()
    out["hotkey_again"] = game.hotkey(REAL_KEY)               # 실제 키보드의 단축키 — 창이 닫힌다
    game.wait(0.3)
    out["title_after"] = game.click(TITLE)                    # 창이 닫혔으니 그 자리의 누름은 게임이 받는다
    print(" ".join(f"{k}={v}" for k, v in out.items()) + " text=" + game.text())
    return 0


def run_real_key(hook: str) -> int:
    """사용자가 다른 창에서 수정키를 쓰는 동안에도 검사 도구의 단축키가 ToyBox 에 닿는다. 출력 "first=… shown=… second=… hidden=…"."""
    game = start_game(hook)
    if game is None:
        return 0
    if not tap_real_ctrl():                                   # 가짜 게임이 뜬 뒤, 첫 단축키 전에
        print("skip 실제 키 입력을 넣을 수 없다(SendInput 이 막혔다)")
        return 0
    first = game.hotkey()
    shown = int("status" in game.facts())                     # 창이 열렸다
    tap_real_ctrl()                                           # 창이 열린 뒤에 한 번 더
    second = game.hotkey()
    hidden = int("status" not in game.facts())                # 창이 닫혔다
    print(f"first={first} shown={shown} second={second} hidden={hidden}")
    return 0


def run_scale(hook: str) -> int:
    game = start_game(hook, buffer=(512, 384))                # 창은 1024x768 인데 그리는 크기는 그 절반 — 화면에는 두 배로 늘어나 보인다
    if game is None:
        return 0
    game.hotkey()
    # 설정 창은 그리는 좌표로 40..760 x 60..660 에 있고, 화면(창의 좌표)에는 80..1520 x 120..1320 으로 보인다
    title = game.click((TITLE[0] * 2, TITLE[1] * 2))          # 보이는 창의 제목 줄 — 게임에 가면 안 된다
    beside = game.click((60, 80))                             # 보이는 창의 왼쪽 위 바깥(그리는 좌표로 30,40) — 게임이 받아야 한다
    print(f"title={title} beside={beside}")
    return 0


def main() -> int:
    hook, mode = sys.argv[1], sys.argv[2]
    if mode in ("input", "input_quiet", "input_held"):
        return run_input(hook, mode)
    if mode == "scale":
        return run_scale(hook)
    if mode == "real_key":
        return run_real_key(hook)
    if mode.startswith("gate_"):
        return run_gate(hook, mode)
    if mode.startswith("direct"):
        return run_direct(hook, mode)
    if mode.startswith("reenter_"):
        return run_reenter(hook, mode)
    if mode.startswith("money"):
        return run_money(hook, mode)
    if mode.startswith("more"):
        return run_more(hook, mode)
    if mode.startswith("research"):
        return run_research(hook, mode)
    if mode.startswith("stock"):
        return run_stock(hook, mode)
    if mode.startswith("keep"):
        return run_keep(hook, mode)
    if mode.startswith("layout"):
        return run_layout(hook, mode)
    if mode in ("confirm", "confirm_fault", "hints", "leave", "multiplayer", "pick_gone", "pick_become", "pick_again", "ansi_search"):
        return run_window(hook, mode)
    if mode == "present_fault":
        return run_present_fault(hook)
    swap, hwnd = make_swap_chain()
    if swap is None:
        print("nodevice")
        return 0
    table = vtable(swap)
    if mode in ("longpatch", "longpatch_resize", "stale"):
        return run_refusal(hook, mode, table)
    if mode == "draw_resize":
        return run_draw_resize(hook, swap, hwnd, table)
    return run_fake_hook(hook, mode, swap, table)


if __name__ == "__main__":
    sys.exit(main())
