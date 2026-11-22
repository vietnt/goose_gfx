"""Checks that the Clarus modules have every gfx and ui function a Goose program calls.

    python clarus/coverage.py PATH/rome_goose/src

reads each `gfx::name(` and `ui::name(` call in the program's .goose files
with its number of arguments, and finds the Clarus function or functions that
stand for that form in clarus/gfx/ and clarus/ui/. Clarus has no overloading,
so one Goose name that takes several forms is several Clarus names (FORMS, by
name, or by name and argument count for one form); every other keeps its name,
and the Clarus function must take at least as many arguments, since a Goose
form with fewer leaves the rest to defaults the Clarus call writes out. Exits 1
and names what is missing.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FORMS = {
    "gfx": {"bind": ["bind_pipeline", "bind_compute"],
            "release": ["release_buffer", "release_texture", "release_sampler", "release_pipeline",
                        "release_compute_pipeline", "release_readback"],
            "sampler": ["sampler", "sampler_of"],
            "begin_pass": ["begin_pass", "begin_pass_into"],
            "take": ["take_readback"],
            ("draw", 2): ["draw_instanced"],
            ("draw", 4): ["draw_at"]},
    "ui": {"destroy": ["destroy", "destroy_font_atlas"],
           "render": ["render", "render_into"]},
}


def defined(module):
    """{name: number of parameters} of the module's functions."""
    names = {}
    for f in (HERE / module).glob("*.clarus"):
        for name, params in re.findall(r"^fn (\w+)(?:<[^>]*>)?\((.*?)\)", f.read_text(), re.M):
            names[name] = len([p for p in params.split(",") if p.strip()])
    return names


def calls(module, text):
    """{(name, number of arguments)} of each call of the module in `text`."""
    out = set()
    for m in re.finditer(module + r"::(\w+)\(", text):
        depth, args, i, empty = 1, 1, m.end(), True
        while depth:
            ch = text[i]
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "," and depth == 1:
                args += 1
            if depth and not ch.isspace():
                empty = False
            i += 1
        out.add((m.group(1), 0 if empty else args))
    return out


def main():
    src = Path(sys.argv[1])
    text = "\n".join(p.read_text() for p in src.rglob("*.goose"))
    missing = []
    for module in ("gfx", "ui"):
        forms = sorted(calls(module, text))
        have = defined(module)
        for name, n in forms:
            want = FORMS[module].get((name, n)) or FORMS[module].get(name, [name])
            gone = [w for w in want if w not in have or (len(want) == 1 and have[w] < n)]
            if gone:
                missing.append(f"{module}::{name}/{n} -> {', '.join(gone)}")
        names = {name for name, _ in forms}
        print(f"{module}: {len(forms) - sum(1 for m in missing if m.startswith(module + '::'))} of {len(forms)} "
              f"forms of the {len(names)} functions the program calls have Clarus forms")
    for m in missing:
        print("missing " + m)
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
