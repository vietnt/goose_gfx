"""Runs the package's headless gfx tests: every test/gfx/*.goose, a program
that declares this checkout as its gfx package (`import gfx from "../..";`),
renders off screen, reads the results back and prints them. Each one is run
in-process by TinyCC (`goose run`) and built and run natively (`goose
build`), and what it prints must be exactly test/gfx/<name>.out.

A machine without a GPU device makes a program print `gfx: no GPU device`;
that is reported as skipped, unless --require-gpu makes it a failure, as it
should be for any run that is meant to show a change works.

    python test/run_gfx_tests.py [--goose PATH] [--require-gpu] [--jit-only | --aot-only] [name ...]
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TESTS = REPO / "test" / "gfx"
NO_GPU = "gfx: no GPU device"


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "", f"timed out after {timeout} s"
    return p.returncode, p.stdout, p.stderr


def check(name, how, code, out, err, expected, require_gpu):
    """One run's verdict: 'pass', 'skip' or 'fail', and what to show."""
    if NO_GPU in out or NO_GPU in err:
        return ("fail" if require_gpu else "skip"), f"{how}: no GPU device"
    if code is None:
        return "fail", f"{how}: {err}"
    if code != 0:
        return "fail", f"{how}: exit {code}\n{err.strip()[-2000:]}"
    if out != expected:
        diff = "\n".join(f"  {a!r}\n  {b!r}" for a, b in
                         zip(expected.splitlines() + [""] * len(out.splitlines()),
                             out.splitlines() + [""] * len(expected.splitlines()))
                         if a != b)
        return "fail", f"{how}: output differs (expected, then got)\n{diff}"
    return "pass", how


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--goose", default="goose", help="the Goose compiler to run")
    ap.add_argument("--require-gpu", action="store_true",
                    help="a program that finds no GPU device fails instead of being skipped")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--jit-only", action="store_true")
    mode.add_argument("--aot-only", action="store_true")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("names", nargs="*", help="tests to run, by name without .goose")
    args = ap.parse_args()

    os.environ["GOOSE_GFX_HEADLESS"] = "1"
    tests = sorted(TESTS.glob("*.goose"))
    if args.names:
        tests = [t for t in tests if t.stem in args.names]
        missing = set(args.names) - {t.stem for t in tests}
        if missing:
            ap.error("no such test: " + ", ".join(sorted(missing)))
    counts = {"pass": 0, "skip": 0, "fail": 0}
    with tempfile.TemporaryDirectory() as tmp:
        for t in tests:
            expected_file = t.with_suffix(".out")
            if not expected_file.exists():
                print(f"FAIL {t.stem}: no {expected_file.name}")
                counts["fail"] += 1
                continue
            expected = expected_file.read_text()
            runs = []
            if not args.aot_only:
                runs.append(("jit", [args.goose, "run", str(t)], None))
            if not args.jit_only:
                exe = str(Path(tmp) / t.stem)
                runs.append(("aot", [exe], [args.goose, "build", str(t), "-o", exe]))
            for how, cmd, build in runs:
                if build:
                    code, out, err = run(build, args.timeout)
                    if code != 0:
                        print(f"FAIL {t.stem} {how}: build failed\n{err.strip()[-2000:]}")
                        counts["fail"] += 1
                        continue
                code, out, err = run(cmd, args.timeout)
                verdict, msg = check(t.stem, how, code, out, err, expected, args.require_gpu)
                counts[verdict] += 1
                print(f"{verdict.upper():4} {t.stem} {msg}")
    print(f"== gfx tests: {counts['pass']} passed, {counts['skip']} skipped, "
          f"{counts['fail']} failed")
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
