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
    ns, _ = ap.parse_known_args()

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


if __name__ == "__main__":
    sys.exit(main())
