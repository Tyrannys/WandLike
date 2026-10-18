"""Regenerate wandlike/games/_index.json from the plugin files.

    python tools/make_catalog.py          # write it
    python tools/make_catalog.py --check  # exit 1 if the committed index is stale (CI / tests)

Installed launchers detect plugin changes by content hash, so there is nothing to bump. The catalog.yml workflow
runs this automatically on every push to main that touches a plugin; run it locally to keep the tests green.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
GAMES = ROOT / "wandlike" / "games"

from wandlike.core.catalog import sha256  # noqa: E402
from wandlike.core.plugin import parse_game  # noqa: E402


def build() -> dict:
    games = []
    for p in sorted(GAMES.glob("*.json")):
        if p.name.startswith("_"):
            continue
        data = p.read_bytes()
        g = parse_game(json.loads(data.decode("utf-8")))
        if p.stem != g.id:
            raise SystemExit(f"{p.name}: file name must equal the plugin id ({g.id})")
        games.append({"id": g.id, "name": g.name, "version": g.version, "file": p.name, "sha256": sha256(data),
                      "match": g.match_spec, "cheats": sum(1 for c in g.cheats if c.available and c.kind != "button"),
                      **({"min_app": g.min_app} if g.min_app else {})})
    return {"schema": 1, "games": games}


def render(index: dict) -> str:
    return json.dumps(index, indent=2) + "\n"


if __name__ == "__main__":
    out = GAMES / "_index.json"
    text = render(build())
    if "--check" in sys.argv:
        current = out.read_text(encoding="utf-8").replace("\r\n", "\n") if out.exists() else ""
        if current != text:
            print("_index.json is stale: run python tools/make_catalog.py")
            sys.exit(1)
        print("_index.json is up to date")
    else:
        out.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {out} ({len(json.loads(text)['games'])} games)")
