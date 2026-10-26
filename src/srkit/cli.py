"""srkit 명령줄 진입점: ``uv run srkit <명령>``."""
from __future__ import annotations

import argparse
import sys
from datetime import datetime
from pathlib import Path

from . import cheats, config, deploy, hook, inventory, korean, mt, probe, toybox
from . import spritefont as sf


def _print_reports(reports: list[korean.TableReport]) -> None:
    total = done = reviewed = 0
    print(f"  {'테이블':<29} {'번역':>6}   {'전체':<5}            검수")
    for r in reports:
        total, done, reviewed = total + r.total, done + r.translated, reviewed + r.reviewed
        pct = 100 * r.translated / r.total if r.total else 0
        print(f"  {r.name:<32} {r.translated:>6} / {r.total:<6} ({pct:5.1f}%)  {r.reviewed:>6}")
        for label, keys in (("원문 변경", r.changed), ("원문에서 삭제됨", r.removed)):
            if keys:
                print(f"      {label} {len(keys)}건: " + ", ".join(keys[:5]) + (" ..." if len(keys) > 5 else ""))
    print(f"  {'합계':<30} {done:>6} / {total:<6} ({100 * done / total if total else 0:5.1f}%)  {reviewed:>6}")


def cmd_info(cfg: config.Config, _args) -> int:
    print(f"프로젝트 루트 : {cfg.root}")
    print(f"게임 폴더     : {cfg.game_dir} ({'있음' if cfg.game_dir.is_dir() else '없음'})")
    langs = sorted(p.name for p in cfg.localize_dir.glob("LOCAL*") if p.is_dir())
    print(f"설치된 언어   : {', '.join(langs)}")
    print(f"원문 → 대상   : {cfg.source_lang} → {cfg.target_lang} ('{cfg.display_name}')")
    print(f"한글 글꼴     : {cfg.hangul_font} ({'있음' if cfg.hangul_font.is_file() else '없음'})")
    print(f"디코딩 훅     : {hook.status(cfg)}")
    print(f"ToyBox        : {toybox.status(cfg)}")
    return 0


def cmd_extract(cfg, _args) -> int:
    _print_reports(korean.extract(cfg))
    return 0


def cmd_tm_fill(cfg, args) -> int:
    print(f"채운 행: {korean.tm_fill(cfg, args.pattern)}")
    return 0


def cmd_tcheck_import(cfg, args) -> int:
    log = args.log or config.user_save_dir() / "LOG-TRANS-CHECK.log"
    if not log.is_file():
        print(f"로그가 없습니다: {log}\n게임을 -tcheck 옵션으로 실행하면 시작할 때 만들어집니다 "
              "(uv run python scripts/gamedrive.py start -window -tcheck).")
        return 1
    r = korean.tcheck_import(cfg, log)
    print(f"로그: {log} ({datetime.fromtimestamp(log.stat().st_mtime):%Y-%m-%d %H:%M:%S} 작성)")
    print(f"빠진 문구 {r['missing']}종, 대소문자만 다른 문구 {r['case']}종")
    print(f"키 목록에 추가 {r['new_keys']}개, 기존 번역으로 채움 {r['filled']}행, GUI 테이블의 미번역 {r['untranslated']}행")
    if r["stale"]:
        print(f"번역이 있는데 게임이 찾지 못한 문구 {len(r['stale'])}개 (설치된 빌드가 오래됐거나 키가 어긋남):")
        for text in r["stale"][:20]:
            print(f"  - {text!r}")
    return 0


def cmd_stats(cfg, _args) -> int:
    _print_reports(korean.stats(cfg))
    return 0


def cmd_check(cfg, _args) -> int:
    problems = korean.check(cfg)
    print("\n".join(problems) if problems else "문제 없음")
    return 1 if problems else 0


def cmd_consistency(cfg, args) -> int:
    found = korean.inconsistencies(cfg)
    for en, kos in found[:args.limit]:
        print(f"{en!r}")
        for ko, where in kos.items():
            print(f"    {ko!r}  x{len(where)}  ({where[0]}{' …' if len(where) > 1 else ''})")
    print(f"같은 원문에 번역이 여럿인 경우: {len(found)}건" + (f" (앞 {args.limit}건 표시)" if len(found) > args.limit else ""))
    return 0


def cmd_build(cfg, args) -> int:
    only = {f.upper() for f in args.font} if args.font else None
    result = korean.build(cfg, fonts=not args.no_fonts, only_fonts=only)
    print(f"산출물      : {result['out']}")
    print(f"적용된 번역 : {result['applied']}")
    print(f"글꼴        : {len(result['fonts'])}개 생성, 텍스트의 비ASCII 문자 {result['nonascii_chars']}종")
    print(f"디코딩 훅   : {'포함' if result['hook'] else '없음 — srkit hook-build 후 다시 빌드'}")
    notes = result["sprite_notes"]
    print(f"그림 글자   : {'모두 처리' if not notes else f'처리하지 못한 항목 {len(notes)}건'}")
    for line in notes:
        print(f"  - {line}")
    return 0


def cmd_font_preview(_cfg, args) -> int:
    font = sf.load(args.spritefont)
    sf.render_text(font, args.text.replace("\\n", "\n")).save(args.out)
    h, w = font.alpha.shape
    print(f"글리프 {len(font.glyphs)}개, 텍스처 {w}x{h}, 줄 간격 {font.line_spacing:.1f} → {args.out}")
    return 0


def cmd_mt_export(cfg, args) -> int:
    paths = mt.export(cfg, args.table, max_rows=args.rows, max_chars=args.chars, key_filter=args.filter, tag=args.tag)
    for p in paths:
        print(p)
    print(f"{len(paths)}개 청크")
    return 0


def _print_chunk(result: mt.ChunkResult, limit: int = 30) -> None:
    done = len(result.valid) + result.skipped
    print(f"{result.path.name}: {done}/{result.total} 통과 (번역 {len(result.valid)}, 미번역 표시 {result.skipped}), "
          f"문제 {len(result.problems)}건")
    for line in result.problems[:limit]:
        print(f"  - {line}")
    if len(result.problems) > limit:
        print(f"  ... 외 {len(result.problems) - limit}건")


def cmd_mt_check(_cfg, args) -> int:
    bad = 0
    for path in args.chunk:
        result = mt.check(path)
        _print_chunk(result)
        bad += bool(result.problems)
    return 1 if bad else 0


def cmd_mt_import(cfg, args) -> int:
    applied_total = 0
    for result, applied in mt.import_all(cfg, overwrite=args.overwrite):
        _print_chunk(result, limit=5)
        applied_total += applied
    print(f"번역 테이블에 반영: {applied_total}행")
    return 0


def cmd_hook_build(cfg, args) -> int:
    print(f"빌드 완료: {hook.build(cfg, trace=args.trace)}")
    if args.trace:
        print("진단용 빌드입니다: 게임의 레지스트리 접근을 %TEMP%\\srhook-trace-<pid>.log 에 적습니다. "
              "다 본 뒤 --trace 없이 다시 빌드하세요.")
    return 0


def cmd_toybox_build(cfg, _args) -> int:
    print(f"빌드 완료: {toybox.build(cfg)}")
    return 0


def _print_sig_rows(rows) -> None:
    for row in rows:
        mark = "한 번" if row.count == 1 else "안 맞음" if row.count == 0 else "여러 번"
        value = f"{row.value:#x}" + (f" · {row.value2:#x}" if row.value2 else "")
        print(f"  {row.name:<15} {mark:<5} 자리 {row.at:#010x} → {value:<16} {row.text}")


def cmd_locate(cfg, _args) -> int:
    exe = cfg.game_dir / toybox.EXE_NAME
    data = exe.read_bytes()
    stamp = int.from_bytes(data[int.from_bytes(data[0x3C:0x40], "little") + 8:][:4], "little")
    print(f"{exe.name}: {len(data):,} 바이트, PE TimeDateStamp {stamp:#x}")
    if not toybox.output(cfg).is_file():
        print("ToyBox DLL 이 없습니다 — srkit toybox-build")
        return 1
    found = toybox.locate(cfg)
    print(f"새 찾기 — 게임 상태를 읽는 주소(서명. 내장 치트와 무관하다): {found.ms:.0f} ms")
    _print_sig_rows(found.rows)
    if found.state is None:
        print(f"찾지 못했습니다: {found.state_why}")
        print("ToyBox 는 이 빌드에서 게임을 읽지 못합니다. uv run srkit sig-mine <RVA> 로 서명을 다시 뽑습니다(docs/11).")
    else:
        for name in toybox.STATE_FIELDS:
            print(f"  {found.state[name]:#010x}  {toybox.ADDRESS_NAMES[name]}")
    print("새 찾기 — 값을 읽고 쓰는 자리(서명. 내장 치트와 무관하다. 둘을 읽는 서명은 간격 · 첫 칸):")
    _print_sig_rows(found.value_rows)
    if found.values is None:
        print(f"찾지 못했습니다: {found.values_why}")
        print("ToyBox 의 돈 · 물자 탭이 이 빌드에서 꺼집니다. uv run srkit sig-mine [--offset] 으로 서명을 다시 뽑습니다(docs/11).")
    else:
        for name in toybox.VALUE_FIELDS:
            print(f"  {found.values[name]:#010x}  {toybox.VALUE_NAMES[name]}")
    print("새 찾기 — 더 쓰는 값(서명. 기능마다 한 묶음 — 따로 찾고 따로 꺼진다. 모두 지역 객체 안의 자리):")
    _print_sig_rows(found.more_rows)
    for (group, fields), why in zip(toybox.MORE_GROUPS, found.more_why):
        if why:
            print(f"  {group}: 찾지 못했습니다: {why}")
        else:
            for name in fields:
                print(f"  {found.more[name]:#010x}  {toybox.MORE_NAMES[name]}")
    if any(found.more_why):
        print("ToyBox 에서 그 묶음의 단추만 꺼집니다. uv run srkit sig-mine --offset 으로 서명을 다시 뽑습니다(docs/11).")
    print("새 찾기 — 부르는 게임의 함수(서명. 그 함수를 부르는 자리 — 셋이 모두 맞아야 한다):")
    _print_sig_rows(found.act_rows)
    for name, why in zip(toybox.ACT_FIELDS, found.acts_why):
        print(f"  찾지 못했습니다: {why}" if why else f"  {found.acts[name]:#010x}  {toybox.ACT_NAMES[name]}")
    print("새 찾기 — 배율(서명. 셋 가운데 둘 — 따로 찾고 따로 꺼진다. 표와 날의 칸은 주소, 나머지는 지역 객체 안의 자리):")
    _print_sig_rows(found.mult_rows)
    for name, why in zip(toybox.MULT_FIELDS, found.mult_why):
        print(f"  찾지 못했습니다: {why}" if why else f"  {found.mult[name]:#010x}  {toybox.MULT_NAMES[name]}")
    print("새 찾기 — 연구(서명. 셋이 모두 맞아야 한다 — 표와 목록의 꼴이 서명에 박혀 있다. 세계 객체는 주소 · 지역 표까지의 거리):")
    _print_sig_rows(found.research_rows)
    if found.research is None:
        print(f"찾지 못했습니다: {found.research_why}")
        print("ToyBox 의 연구 기능이 이 빌드에서 꺼집니다. uv run srkit sig-mine [--with · --back · --through-jumps] 로 서명을 다시 "
              "뽑고 꼴의 상수를 확인합니다(docs/11).")
    else:
        for name in toybox.RESEARCH_FIELDS:
            print(f"  {found.research[name]:#010x}  {toybox.RESEARCH_NAMES[name]}")
    print("옛 찾기 — 검증 도구만 보는 주소(치트 문자열이 닻이다. ToyBox 의 기능은 쓰지 않는다):")
    if found.legacy is None:
        print(f"  찾지 못했습니다: {found.legacy_why}")
        print("  gamedrive.py peek 이 치트 허용 비트를 읽지 못합니다. ToyBox 의 기능에는 영향이 없습니다(docs/11).")
    else:
        for name in toybox.LEGACY_FIELDS:
            print(f"  {found.legacy[name]:#010x}  {toybox.ADDRESS_NAMES[name]}")
    new = found.state is not None and found.values is not None and not any(found.more_why) and found.research is not None
    if new and found.legacy is not None:
        print("모두 찾았습니다. docs/11 의 표와 다르면 게임이 바뀐 것입니다.")
    return 0 if new else 1


def cmd_sig_mine(cfg, args) -> int:
    from . import sigmine      # capstone 은 개발 의존성이다 — 이 명령에서만 불러온다

    exe = cfg.game_dir / toybox.EXE_NAME
    image = sigmine.Image(toybox.image_of(exe.read_bytes()))
    values = [int(value, 16) for value in args.values]
    if len(values) > (2 if args.offset else 1):
        print("주소는 하나, --offset 의 상수는 하나나 둘입니다(서명 하나가 읽어 내는 값은 둘까지다).")
        return 1
    try:
        cheats = sigmine.cheat_ranges(image)
    except LookupError as error:
        print(error)
        return 1
    shape = dict(holding=[int(value, 16) for value in getattr(args, "holding", None) or []], back=getattr(args, "back", 0),
                 through_jumps=getattr(args, "through_jumps", False))
    for name in ("most", "longest"):                   # 주지 않으면 뽑는 쪽의 기본값(주소 8 · 상수 9 명령, 60바이트)
        if getattr(args, name, None):
            shape[name] = getattr(args, name)
    if shape.get("longest", 0) > 64:
        print("서명 하나는 64바이트까지입니다(native/srtoybox/sigs.h 의 SIG_MAX).")
        return 1
    if args.offset:
        found = sigmine.mine_constants(image, values, exclude=cheats, sites=args.sites, **shape)
        what = "상수 " + " · ".join(f"{value:#x}" for value in values) + " 을 차례로 든 코드에서"
    else:
        found = sigmine.mine_address(image, values[0], exclude=cheats, sites=args.sites, **shape)
        what = f"{values[0]:#x} 를 가리키는 코드에서"
    if shape["holding"]:
        what += ", 상수 " + " · ".join(f"{value:#x}" for value in shape["holding"]) + " 을 든 것만"
    picks = sigmine.best_per_function(found)
    left_out = (f"치트 코드 {len(cheats)}곳은 뺐다 — 명령 처리 함수 {cheats[0][0]:#x} – {cheats[0][1]:#x} 와 치트 코드에서만 불리는 함수"
                if cheats else "치트 명령 처리 함수가 없는 빌드다")
    print(f"{exe.name}: {what} ({left_out})")
    for c in picks[:args.limit]:
        function = f"{c.function:#x}" if c.function is not None else "표에 없음"
        print(f"  자리 {c.at:#010x}  함수 {function:>10}  {c.length:>2}바이트  {c.text}")
        print(f"      {sigmine.listing(image, c)}")
    print(f"서로 다른 함수 {len(picks)}개에서 후보가 나왔습니다. 서명 표에는 서로 다른 함수의 것 셋을 골라 옮깁니다.")
    if args.offset:
        print("상수가 우연히 같은 코드도 섞여 나옵니다 — 명령을 보고 뜻이 맞는 것만 고릅니다(docs/11).")
    return 0 if len(picks) >= 3 else 1


def cmd_inventory(cfg, _args) -> int:
    r = inventory.run(cfg)
    print(f"산출물         : {r['out']}")
    print(f"섹션           : {r['sections']}종 (키 섹션 {r['keyed']}, 표 {r['sections'] - r['keyed']})")
    print(f"키             : {r['keys']}개")
    print(f"실행 파일 후보 : {r['candidates']}개 (추정 — exe-candidates.csv)")
    print(f"건너뛴 파일    : {r['skipped']}개 (skipped.csv)")
    return 0


def cmd_cheats_check(cfg, _args) -> int:
    r = cheats.check(cfg)
    print(f"설치된 빌드    : {r.build or '알 수 없음'} (문서 기준 {r.doc_build or '알 수 없음'})")
    print(f"사라진 치트    : {', '.join(r.removed) or '없음'}")
    print(f"새로 생긴 치트 : {', '.join(r.added) or '없음'}")
    print(f"설정 창 단축키 : {'있음' if r.settings_hotkey else '없음 — 치트 입력란을 열 수 없다'}")
    if not r.ok:
        print("게임이 문서와 달라졌습니다. docs/09 의 '게임 업데이트 대비'를 보세요.")
    elif r.build != r.doc_build:
        print("빌드가 문서와 다릅니다. 치트 목록은 같지만 효과는 문서의 빌드에서 확인한 것입니다.")
    return 0 if r.ok else 1


def cmd_probe(cfg, args) -> int:
    out = probe.make(cfg, args.name, args.file, args.old, args.new, after=args.after)
    print(f"시험 모드 : {out.relative_to(cfg.root)}")
    print(f"설치      : uv run srkit deploy probe-{args.name}   (미리보기. --apply 로 실행, 끝나면 undeploy)")
    return 0


def cmd_deploy(cfg, args) -> int:
    for line in deploy.deploy(cfg, args.mod, apply=args.apply):
        print(line)
    return 0


def cmd_undeploy(cfg, args) -> int:
    for line in deploy.undeploy(cfg, args.mod, apply=args.apply):
        print(line)
    return 0


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    p = argparse.ArgumentParser(prog="srkit", description="Supreme Ruler 2030 모드 / 한글화 제작 도구")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info", help="설정과 게임 설치 상태 표시").set_defaults(fn=cmd_info)
    sub.add_parser("extract", help="영문 원문을 번역 테이블로 추출(기존 번역 유지)").set_defaults(fn=cmd_extract)
    sub.add_parser("stats", help="번역 진행률").set_defaults(fn=cmd_stats)
    t = sub.add_parser("tcheck-import", help="게임의 번역 검사 로그에서 빠진 GUI 문구를 번역 테이블에 추가")
    t.add_argument("log", type=Path, nargs="?", help="LOG-TRANS-CHECK.log 경로 (기본: 게임 저장 폴더)")
    t.set_defaults(fn=cmd_tcheck_import)
    t = sub.add_parser("tm-fill", help="빈 행을 다른 테이블의 같은 원문 번역으로 채우기")
    t.add_argument("pattern", nargs="?", default="scen.*.csv", help="대상 테이블 (기본: 시나리오 전용 테이블)")
    t.set_defaults(fn=cmd_tm_fill)
    sub.add_parser("check", help="번역문 검증(서식 토큰, 금지 문자)").set_defaults(fn=cmd_check)
    c = sub.add_parser("consistency", help="같은 영어 원문이 서로 다르게 번역된 곳 찾기(검수용)")
    c.add_argument("--limit", type=int, default=40)
    c.set_defaults(fn=cmd_consistency)
    b = sub.add_parser("build", help="build/korean 에 한글화 모드 생성")
    b.add_argument("--no-fonts", action="store_true", help="글꼴 생성 생략")
    b.add_argument("--font", action="append", help="지정한 글꼴만 생성 (예: --font FONT11)")
    b.set_defaults(fn=cmd_build)
    f = sub.add_parser("font-preview", help="spritefont 로 문자열을 그려 PNG 로 저장")
    f.add_argument("spritefont", type=Path)
    f.add_argument("text")
    f.add_argument("out", type=Path)
    f.set_defaults(fn=cmd_font_preview)
    m = sub.add_parser("mt-export", help="미번역 행을 기계 번역용 청크(build/mt/*.in.jsonl)로 내보내기")
    m.add_argument("table", help="번역 테이블 파일명 (예: localtext-gui.csv)")
    m.add_argument("--rows", type=int, default=400, help="청크당 최대 행 수")
    m.add_argument("--chars", type=int, default=20000, help="청크당 최대 원문 글자 수")
    m.add_argument("--filter", help="키에 대한 정규식 (일치하는 행만)")
    m.add_argument("--tag", default="", help="청크 이름에 붙일 구분 태그")
    m.set_defaults(fn=cmd_mt_export)
    m = sub.add_parser("mt-check", help="청크 번역 결과(*.out.jsonl) 검증")
    m.add_argument("chunk", type=Path, nargs="+", help="*.in.jsonl 경로")
    m.set_defaults(fn=cmd_mt_check)
    m = sub.add_parser("mt-import", help="검증을 통과한 청크 결과를 번역 테이블에 병합(status=mt)")
    m.add_argument("--overwrite", action="store_true", help="이미 번역이 있는 행도 덮어쓰기")
    m.set_defaults(fn=cmd_mt_import)
    h = sub.add_parser("hook-build", help="디코딩 훅 DLL(WTSAPI32.dll) 빌드")
    h.add_argument("--trace", action="store_true", help="진단용: 게임의 레지스트리 접근을 로그로 남기는 빌드")
    h.set_defaults(fn=cmd_hook_build)
    sub.add_parser("toybox-build", help="ToyBox DLL(srtoybox.dll, 게임 안 모드 설정 창) 빌드 → build/toybox") \
        .set_defaults(fn=cmd_toybox_build)
    sub.add_parser("locate", help="설치된 게임에서 ToyBox 가 쓰는 주소를 찾아 보고(게임 업데이트 뒤 cheats-check 다음에)") \
        .set_defaults(fn=cmd_locate)
    m = sub.add_parser("sig-mine", help="(개발용) 설치된 게임에서 주소나 상수를 읽어 낼 서명 후보 뽑기 — 치트 코드 밖에서")
    m.add_argument("values", nargs="+", help="주소(RVA, 16진수. 예: 18295f8). --offset 이면 차례로 나올 상수 하나나 둘(예: 150 14da4)")
    m.add_argument("--offset", action="store_true", help="값이 주소가 아니라 구조체 안의 자리 · 간격(상수)이다")
    m.add_argument("--limit", type=int, default=12, help="보여 줄 후보의 수")
    m.add_argument("--sites", type=int, default=400, help="살펴볼 자리의 수(많으면 오래 걸린다)")
    m.add_argument("--with", dest="holding", nargs="+", metavar="상수", help="서명 안의 명령이 들고 있어야 하는 상수들(16진수. 구조체의 "
                   "크기 · 칸의 자리) — 꼴이 바뀐 빌드에서 서명이 맞지 않게 한다")
    m.add_argument("--back", type=int, default=0, help="가리키는 명령보다 이만큼 앞선 명령부터 서명을 시작한다")
    m.add_argument("--most", type=int, help="서명의 최대 명령 수(기본: 주소 8, 상수 9)")
    m.add_argument("--longest", type=int, help="서명의 최대 바이트 수(기본 60. 서명 하나는 64바이트까지다)")
    m.add_argument("--through-jumps", action="store_true", help="무조건 jmp 를 지나서도 서명을 늘린다(목록을 훑는 코드처럼 갈래가 많은 곳)")
    m.set_defaults(fn=cmd_sig_mine)
    sub.add_parser("inventory", help="게임 데이터의 섹션·키·열 목록을 build/inventory 에 CSV 로").set_defaults(fn=cmd_inventory)
    sub.add_parser("cheats-check", help="게임의 내장 치트가 docs/07 과 같은지 대조(게임 업데이트 감지)") \
        .set_defaults(fn=cmd_cheats_check)
    pr = sub.add_parser("probe", help="설치본 파일에서 값 한 곳만 바꾼 시험 모드를 build/probe-<이름> 에 만들기")
    pr.add_argument("name", help="시험 이름 (설치할 때는 probe-<이름>)")
    pr.add_argument("file", help="게임 폴더 기준 경로 (예: Maps/W2030.CVP)")
    pr.add_argument("old", help="바꿀 문자열 (파일에 한 번만 나와야 한다)")
    pr.add_argument("new", help="새 문자열")
    pr.add_argument("--after", help="이 문자열(한 번만 나와야 한다) 뒤의 첫 일치를 바꾼다")
    pr.set_defaults(fn=cmd_probe)
    for name, fn, text in (("deploy", cmd_deploy, "빌드한 모드를 게임 폴더에 설치"),
                           ("undeploy", cmd_undeploy, "설치한 모드를 제거하고 원본 복원")):
        d = sub.add_parser(name, help=text + " (기본은 미리보기, --apply 로 실행)")
        d.add_argument("mod", help="build/ 아래 모드 이름 (예: korean)")
        d.add_argument("--apply", action="store_true")
        d.set_defaults(fn=fn)
    args = p.parse_args(argv)
    return args.fn(config.load(), args)


if __name__ == "__main__":
    raise SystemExit(main())
