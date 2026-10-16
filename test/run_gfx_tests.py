"""Runs the package's headless gfx tests: every test/gfx/*.goose, a program
that declares this checkout as its gfx package (`import gfx from "../..";`),
renders off screen, reads the results back and prints them. Each one is run
in-process by TinyCC (`goose run`) and built and run natively (`goose
build`), and what it prints must be exactly test/gfx/<name>.out, or
test/gfx/<name>.<driver>.out where the device's driver (gfx::driver())
needs other output and one exists.

A machine without a GPU device makes a program print `gfx: no GPU device`;
that is reported as skipped, unless --require-gpu makes it a failure, as it
should be for any run that is meant to show a change works. --validate runs
with SDL's GPU debug mode (GOOSE_GFX_DEBUG=1), which turns on the Vulkan
validation layers where they are installed, logs what they report to a
file of the run's own, and fails a run that violated a valid-usage rule (a
VUID, or one an older layer reports without an ID). The layer's cache of shaders it has checked is off, so each run
checks its shaders again. On Direct3D 12 it fails a run whose stderr has a
debug-layer error, which SDL logs at its GPU category's warn level.

A test whose first line is `// needs: <what>` uses a shader feature newer
than some Goose compilers; with one whose shader compiler cannot compile
NEEDS[<what>], it is reported as skipped.

    python test/run_gfx_tests.py [--goose PATH] [--require-gpu] [--validate] [--jit-only | --aot-only] [name ...]
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
NEEDS = {
    "demote": "layout(location = 0) out vec4 c;\nvoid main() { demote; c = vec4(1.0); }\n",
}


def run(cmd, timeout, env=None):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return None, "", f"timed out after {timeout} s"
    return p.returncode, p.stdout, p.stderr


def validation_env(log):
    """The environment of a run whose validation layer logs to `log`."""
    settings = log.parent / (log.stem + "-settings")
    settings.mkdir(exist_ok=True)
    (settings / "vk_layer_settings.txt").write_text(
        "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
        f"khronos_validation.log_filename = {log}\n"
        "khronos_validation.check_shaders_caching = false\n"
        "khronos_validation.disables = VK_VALIDATION_FEATURE_DISABLE_SHADER_VALIDATION_CACHE_EXT\n")
    return dict(os.environ, VK_LAYER_SETTINGS_PATH=str(settings),
                SDL_LOGGING=os.environ.get("SDL_LOGGING", "gpu=warn"))


def driver(goose, timeout):
    """The driver of the device this machine gives, or None for none."""
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "driver.goose"
        probe.write_text(f'import gfx from "{REPO.as_posix()}";\nimport std;\n'
                         'fn main() { if gfx::open_headless(1, 1) { print(gfx::driver()); gfx::close(); } }\n')
        code, out, _ = run([goose, "run", str(probe)], timeout)
    lines = out.split()
    return lines[-1] if code == 0 and lines else None


def lacks(goose, test, tmp, timeout):
    """What of NEEDS the test needs that the compiler cannot compile, or None."""
    first = test.read_text().split("\n", 1)[0]
    if not first.startswith("// needs: "):
        return None
    what = first[len("// needs: "):].strip()
    probe = Path(tmp) / f"needs-{what}.frag"
    probe.write_text(NEEDS[what])
    code, _, _ = run([goose, "--compile-shader", str(probe)], timeout)
    return None if code == 0 else what


def check(name, how, code, out, err, expected, require_gpu, log):
    """One run's verdict: 'pass', 'skip' or 'fail', and what to show."""
    if NO_GPU in out or NO_GPU in err:
        return ("fail" if require_gpu else "skip"), f"{how}: no GPU device"
    reported = log.read_text(errors="replace") if log and log.exists() else ""
    # Newer layers follow an error with a line of its own quoting the spec,
    # which names the VUID again.
    vuids = [line for line in reported.splitlines()
             if ("VUID-" in line or "Validation Error:" in line)
             and not line.lstrip().startswith("The Vulkan spec states")]
    if log:
        vuids += [line for line in err.splitlines() if "D3D12 ERROR" in line]
    if vuids:
        return "fail", f"{how}: {len(vuids)} validation error(s)\n  " + "\n  ".join(vuids[:5])
    # Some versions of the Vulkan layer also say where they log to, on stdout.
    out = "".join(line for line in out.splitlines(True)
                  if not line.startswith("Validation Layer Info - "))
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
    ap.add_argument("--validate", action="store_true",
                    help="run with GPU debug mode and fail on any valid-usage (VUID) error")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("names", nargs="*", help="tests to run, by name without .goose")
    args = ap.parse_args()

    os.environ["GOOSE_GFX_HEADLESS"] = "1"
    if args.validate:
        os.environ["GOOSE_GFX_DEBUG"] = "1"
    tests = sorted(TESTS.glob("*.goose"))
    if args.names:
        tests = [t for t in tests if t.stem in args.names]
        missing = set(args.names) - {t.stem for t in tests}
        if missing:
            ap.error("no such test: " + ", ".join(sorted(missing)))
    counts = {"pass": 0, "skip": 0, "fail": 0}
    device = driver(args.goose, args.timeout)
    print(f"== driver: {device or 'none'}")
    with tempfile.TemporaryDirectory() as tmp:
        for t in tests:
            expected_file = t.with_suffix(".out")
            if device and t.with_suffix(f".{device}.out").exists():
                expected_file = t.with_suffix(f".{device}.out")
            if not expected_file.exists():
                print(f"FAIL {t.stem}: no {expected_file.name}")
                counts["fail"] += 1
                continue
            what = lacks(args.goose, t, tmp, args.timeout)
            if what:
                print(f"SKIP {t.stem}: this compiler's shaders have no {what}")
                counts["skip"] += 1
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
                log = Path(tmp) / f"{t.stem}-{how}.vvl" if args.validate else None
                code, out, err = run(cmd, args.timeout, validation_env(log) if log else None)
                verdict, msg = check(t.stem, how, code, out, err, expected, args.require_gpu, log)
                counts[verdict] += 1
                print(f"{verdict.upper():4} {t.stem} {msg}")
    print(f"== gfx tests: {counts['pass']} passed, {counts['skip']} skipped, "
          f"{counts['fail']} failed")
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
