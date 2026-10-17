import argparse
import sys

from .core.engine import Engine
from .core.plugin import load_all


def main() -> int:
    ap = argparse.ArgumentParser(prog="wandlike", description="Game trainer framework (CK3 first)")
    ap.add_argument("--dry-run", action="store_true", help="log commands instead of typing them into the game")
    ap.add_argument("--list", action="store_true", help="list games and cheats, then exit")
    ap.add_argument("--run", nargs="+", metavar=("GAME", "CHEAT"),
                    help="headless: --run ck3 money 5000 | --run ck3 instabuild on")
    ap.add_argument("--self-check", metavar="OUT.json", help="write a JSON health report and exit (used by build tests)")
    ap.add_argument("--version", action="store_true")
    ap.add_argument("--debug", action="store_true", help="debug mode: console logging + debug page (source/debug builds only)")
    ap.add_argument("--validate", metavar="FILE", help="check a game plugin .json and explain any problems")
    ap.add_argument("--preview", metavar="FILE", help="show the exact commands a plugin would send (no game needed)")
    ap.add_argument("--find-games", nargs="?", const="", metavar="TEXT", help="list games detected in your Steam/Epic/GOG libraries")
    ap.add_argument("--new-game", metavar="TEXT", help="create a starter plugin for a detected game matching TEXT")
    ap.add_argument("--out", metavar="DIR", help="folder for --new-game (default: wandlike/games from source, your plugins folder when installed)")
    ns, _ = ap.parse_known_args()

    if ns.validate or ns.preview or ns.find_games is not None or ns.new_game:
        return authoring_cli(ns)

    if ns.self_check:
        import json
        import webview  # noqa: F401  (proves pywebview is bundled)
        from .version import UPDATE_REPO, __version__
        games = load_all()
        with open(ns.self_check, "w", encoding="utf-8") as f:
            json.dump({"version": __version__, "repo": UPDATE_REPO, "frozen": bool(getattr(sys, "frozen", False)),
                       "games": {g.id: len(g.cheats) for g in games.values()}}, f)
        return 0
    if ns.version:
        from .version import __version__
        print(__version__)
        return 0

    if ns.list:
        for g in load_all().values():
            print(f"{g.name} ({g.id})")
            for c in g.cheats:
                print(f"  {c.id:20} {c.kind:7} [{c.verified}] {c.label}")
        return 0

    if ns.run:
        import time
        games = load_all()
        game = games[ns.run[0]]
        eng = Engine(game, print, ns.dry_run)
        c = game.cheat(ns.run[1])
        arg = ns.run[2] if len(ns.run) > 2 else None
        if c.kind == "number":
            eng.apply_number(c, float(arg), ns.run[3] if len(ns.run) > 3 else "set")
        elif c.kind == "toggle":
            eng.set_toggle(c, arg != "off")
        else:
            eng.press(c)
        time.sleep(2 if ns.dry_run else 6)
        return 0

    from .app.main import run
    run(dry_run=ns.dry_run)
    return 0


def authoring_cli(ns) -> int:
    import json
    from pathlib import Path

    from .core import authoring
    from .core.plugin import load_game

    if ns.validate:
        errors, warns = authoring.validate_file(ns.validate)
        for w in warns:
            print(f"warning: {w}")
        for e in errors:
            print(f"ERROR:   {e}")
        print(f"{len(errors)} error(s), {len(warns)} warning(s)" if errors or warns else "OK: plugin is valid")
        return 1 if errors else 0
    if ns.preview:
        errors, _ = authoring.validate_file(ns.preview)
        if errors:
            print("fix these first (python -m wandlike --validate FILE):", *errors, sep="\n  ")
            return 1
        print("\n".join(authoring.preview(load_game(Path(ns.preview)))))
        return 0

    from .core.library import Library
    games = Library().scan()
    if ns.find_games is not None:
        q = ns.find_games.lower()
        hits = [g for g in games if q in g["name"].lower()]
        for g in hits:
            print(f"{g['store']:6} {g['id']:>10}  {g['name']}   {g['install_dir']}")
        print(f"{len(hits)} game(s)")
        return 0
    q = ns.new_game.lower()
    hits = [g for g in games if q in g["name"].lower()]
    if len(hits) != 1:
        print(f"{len(hits)} detected games match {ns.new_game!r}" + (":" if hits else ". Try --find-games to see what was detected."))
        for g in hits[:20]:
            print(f"  {g['name']}  ({g['store']} {g['id']})")
        return 2
    plugin = authoring.scaffold(hits[0])
    out = Path(ns.out) if ns.out else authoring.default_output_dir()
    out.mkdir(parents=True, exist_ok=True)
    path = out / f"{plugin['id']}.json"
    if path.exists():
        print(f"{path} already exists; not overwriting")
        return 2
    path.write_text(json.dumps(plugin, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")
    print("next: edit the cheats, then run  python -m wandlike --validate FILE  and  --preview FILE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
