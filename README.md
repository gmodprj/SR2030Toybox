# SR2030 ToyBox

**Supreme Ruler 2030** 의 한글화 모드와, 게임 안에서 여는 모드 설정 창 **ToyBox** 를 만드는 저장소입니다.

| | 무엇 | 상태 |
|---|---|---|
| **한글화 모드** | 게임의 메뉴 · 텍스트 · 지도 이름 · 메뉴 그림을 한국어로 | 기계 번역 초안 98.5 % (사람 검수 전) |
| **ToyBox** | `Ctrl+Shift+T` 로 여는 창에서 돈 · 물자 · 연구 · 외교 · 부대 · 배율을 고친다 | 동작 확인됨 (싱글플레이 전용) |

- 확인한 게임: Steam 판 build `21347933`(12.1.1360), Expansion Pass DLC 포함
- 최신 배포판: [Releases](https://github.com/gmodprj/SR2030Toybox/releases)

---

## 설치

[Releases](https://github.com/gmodprj/SR2030Toybox/releases) 에서 두 파일을 받습니다.

1. **게임을 끕니다.** 게임 폴더(예: `SteamLibrary/steamapps/common/Supreme Ruler 2030`)의 `INI/UISettings.csv` 를 다른 곳에 복사해 둡니다.
   한글화 모드가 바꾸는 게임 파일은 이것 하나뿐입니다.
2. `SR2030-Korean-v*.zip` 을 게임 폴더에 그대로 풉니다. 폴더 구조가 게임 폴더와 같습니다.
3. ToyBox 도 쓰려면 `srtoybox.dll` 을 게임 폴더 맨 위(`WTSAPI32.dll` 옆)에 둡니다.
4. 게임을 켜고 **옵션 → FILES → Game Language → `Korean`** 을 한 번 고릅니다.

**되돌리기**: 넣은 파일을 지우고 복사해 둔 `UISettings.csv` 를 되돌립니다. 또는 Steam 의 "게임 파일 무결성 검사"를 실행합니다.
영어로만 돌아가려면 옵션에서 Game Language 를 `English` 로 고릅니다.

> ToyBox 는 한글화 모드가 설치되어 있어야 불러와집니다. Workshop 으로는 배포할 수 없습니다.

---

## ToyBox

게임을 진행하는 중에 **`Ctrl+Shift+T`** 로 엽니다. 단축키는 `설정` 탭에서 바꿀 수 있습니다.
창 맨 위의 상태 줄이 ToyBox 가 게임을 어떻게 읽고 있는지 보여 줍니다(`플레이 중: 독일 (1499)` 등). 메뉴 · 로비 · 멀티플레이에서는 단추가 꺼집니다.

| 탭 | 할 수 있는 것 |
|---|---|
| **돈** | 국고 더하기 · 빼기 · 이 값으로, 빠른 단추(±100억 · ±1,000억 · 0), **최소 유지**(국고가 정한 값보다 적어지면 채운다) |
| **물자** | 물자마다(또는 모든 물자) 재고 ±100만 · ±1억 · 0, 물자마다 최소 유지 |
| **배율** | 세금 · 국방비 · 사회 지출 · 연구 속도(~10000 %) · 유닛 생산 · 시설 건설 속도(~2000 %) · 물자별 생산 · 인구 소비 · 산업 소비(~1000 %)의 퍼센트, **시설 건설을 다음 자정에 완료** |
| **사회** | 보건 · 교육 · 인프라 · 환경 · 가족 수당 · 치안 · 문화 지원 · 사회 보장의 **품질 등급** — 오를 때 · 내릴 때의 배율(0 ~ 10000 %), 항목마다 최소 유지(0 ~ 999 %) |
| **연구** | 대기열의 연구 즉시 완료, 기술 수준 N 이하 전부 보유, 기술 · 부대 설계 목록에서 골라 완료 · 미완료, **연구 가능으로**(모자란 선행만 채워 게임에서 직접 연구하게 — 다른 지역 전용이면 그 지역 코드도 연다. 상태: 연구 가능 · 선행 부족 · 열 수 없음) |
| **인구·여론** | 인구 +100만, 세계 시장 여론 최고, 내 나라 지지율 100 %, **예비 인원** +10만 ~ +1000만 · 최소 유지(유닛 생산 · 병력 보충에 쓰이는 인원), **점령지 충성**(얻은 땅의 충성 대상을 내 나라로 · 계속 유지), 국민 · 군부 지지율과 세계 시장 여론의 최소 유지 |
| **외교·영토** | 고른 나라와 관계 최고 · 중립, 동맹, 병합, 식민지화, 지도자 제거, 전쟁 붙이기, 이 나라로 플레이 |
| **부대** | 설계도 목록에서 골라 등록 · 해제 · 연구 가능으로, 지도에서 고른 칸에 수량만큼 생성, 보급 채우기 · 비우기, 경험 · 사기 · 효율(고른 부대 / 내 부대 전부), 최강 부대 묶음 받기 |
| **화면·진행** | GUI 숨기기/보이기, 즉시 승리 |
| **설정** | 창을 여닫는 단축키 바꾸기 |

**알아 둘 것**

- **플레이어 나라에만 적용됩니다.** `외교·영토` 탭만 창이나 지도에서 고른 나라 하나에 닿습니다.
- **게임의 내장 치트를 쓰지 않습니다.** 게임의 메모리에 직접 쓰거나, 게임이 스스로도 부르는 함수를 부릅니다.
  그래서 게임이 업데이트되어 주소를 찾지 못하면 그 기능의 단추가 꺼집니다.
- **최소 유지와 배율은 저장되는 설정입니다.** 창을 닫아도, 게임을 다시 켜도, 새 판에서도 계속됩니다(`%APPDATA%\SR2030ToyBox\toybox.ini`).
- 값은 바로 바뀌지만 이미 열려 있던 게임 패널의 숫자는 그 패널을 다시 열어야 바뀝니다.
- 되돌릴 수 없는 단추(병합 · 이 나라로 플레이 등)는 한 번 더 눌러야 실행됩니다. 중요한 판은 먼저 저장해 두기를 권합니다.

자세한 사용법과 한계는 [docs/10 — ToyBox](docs/10-toybox.md) 에 있습니다.

---

## 한글화 모드

- 번역 단위 39,275 개 가운데 38,705 개(98.5 %, 지도의 도시 이름 포함)가 기계 번역 초안입니다. **사람 검수는 아직 하지 않았습니다.**
- 게임 엔진이 한글을 다루도록 디코딩 훅(`WTSAPI32.dll`)과 한글 글꼴(Noto Sans KR, SIL OFL)을 넣습니다.
- Expansion Pass 의 역사 시나리오(1914 · 1917 · 1936 · 1940 ~ 1942, 좀비 · 코로나 캠페인)도 번역합니다.

설계 · 검증 현황 · 남은 일은 [docs/04 — 한글화](docs/04-korean-localization.md) 에 있습니다.

---

## 개발

### 준비

- Windows, [uv](https://docs.astral.sh/uv/) (Python 은 시스템에 설치하지 않고 `uv run` 으로 실행), DLL 빌드에는 MSVC Build Tools
- 게임 경로는 `srkit.toml` 의 `game_dir`(또는 환경 변수 `SR2030_GAME_DIR`)

### 자주 쓰는 명령

| 하는 일 | 명령 |
|---|---|
| 설정과 게임 설치 상태 | `uv run srkit info` |
| 테스트 | `uv run pytest` |
| 한글화 모드 빌드 → `build/korean/` | `uv run srkit hook-build` → `uv run srkit build` |
| ToyBox 빌드 → `build/toybox/` | `uv run srkit toybox-build` |
| 게임 폴더에 설치 · 제거 (미리보기, `--apply` 로 실행) | `uv run srkit deploy korean` · `undeploy korean` (`toybox` 도 같다) |
| 번역 테이블 갱신 · 진행률 · 검증 | `uv run srkit extract` · `stats` · `check` |
| 기계 번역 청크 | `uv run srkit mt-export <테이블>` → `mt-check` → `mt-import` |
| 게임에서 빠진 GUI 문구 가져오기 | `uv run srkit tcheck-import` |
| 게임 업데이트 뒤 점검 | `uv run srkit cheats-check` → `locate` → `inventory` |
| ToyBox 서명 다시 뽑기 | `uv run srkit sig-mine <RVA>` |
| 데이터 수정 시험 모드 | `uv run srkit probe <이름> <파일> <바꿀 문자열> <새 문자열>` |
| 게임 안 검증(화면 밖 실행 · 캡처 · 입력 · 메모리 읽기) | `uv run python scripts/gamedrive.py start\|shot\|click\|key\|peek\|research\|stop` |

`deploy --apply` 는 게임 폴더를 바꿉니다. 덮어쓴 원본은 `build/.deploy/` 에 백업하고 `undeploy --apply` 로 되돌립니다.
한 게임 파일은 한 모드만 바꿀 수 있어서, 겹치는 모드가 설치되어 있으면 설치하지 않습니다.

작업 규칙(브랜치 · 게임 안 검증 · ToyBox 가 쓰는 곳)은 [CLAUDE.md](CLAUDE.md) 에 있습니다.

### 구조

```
mods/korean/         한글화 원본 — 번역 테이블(translation/*.csv), 번역 규칙(STYLE.md), 용어집, 그림 글자 설정
mods/toybox/         ToyBox 설명 (모드의 파일은 DLL 하나)
src/srkit/           도구 (텍스트 파서 · SR-UTF8 · 글꼴 · 번역 빌드 · 배포 · 조사 · 서명 뽑기)
native/srhook/       디코딩 훅 DLL (C)
native/srtoybox/     ToyBox DLL (C++, Dear ImGui)
scripts/gamedrive.py 게임 안 검증 도구
tests/               자동 테스트 (일부는 게임 설치본 · 빌드된 DLL 이 있을 때만)
docs/                조사와 설계
build/               산출물 (git 제외)
```

### 문서

| 문서 | 내용 |
|---|---|
| [01](docs/01-game-structure.md) · [02](docs/02-modding-system.md) · [03](docs/03-localization-internals.md) | 게임 구성, 모드 구성 방식, 현지화 내부 구조 |
| [04](docs/04-korean-localization.md) | 한글화 설계 · 검증 현황 · 다음 단계 |
| [05](docs/05-game-systems.md) · [06](docs/06-data-reference.md) | 게임 시스템과 데이터 참조 |
| [07](docs/07-cheats.md) · [08](docs/08-workshop-survey.md) · [09](docs/09-cheat-mod-plan.md) | 내장 치트표, Workshop 조사, 모드 제작 계획 |
| [10](docs/10-toybox.md) · [11](docs/11-game-internals.md) | ToyBox 사용법 · 한계, 게임의 안쪽(주소 · 찾는 법) |
| [superpowers/](docs/superpowers/) | 기능마다의 설계서와 구현 계획 |

---

## 저장소에 대해

- 공개 저장소입니다. 번역 테이블에는 게임 원문(영어)이 들어 있습니다. 게임 파일과 `build/` 산출물은 커밋하지 않습니다.
- 브랜치: `develop` 에서 작업 브랜치를 만들어 PR 로 합치고, 릴리스 때만 `develop` → `main` 으로 올립니다.
- 포함한 외부 코드: Dear ImGui (MIT, `native/third_party/imgui/`).
