"""Builds and runs the package's Clarus programs, and checks what each prints.

    python clarus/run_tests.py --clarus PATH/clarus-lang [--window] [--shaders TGZ] [name ...]

Each program in clarus/ is built natively against this checkout's C layers
(native.py, once, under build/clarus/) and run there. What it prints must be
exactly its expected file: test/gfx/<name>.out for a port of a Goose test, so
both languages answer to one file, else clarus/<name>.out, and the
<name>.<driver>.out beside either when the driver needs its own. A line
`driver: NAME` is the GPU backend, which the run reports and the comparison
leaves out; the driver is SDL_GPU_DRIVER's, else the one the program prints. gfx_window opens a window, so it runs only with --window, on a
machine with a display.

The shader module comes from clarus/test/ through shaders.py, which needs
`goose`; on a machine without it, --shaders takes a .tgz of clarus/shaders/
made where goose is. With goose, the shaders gfx and ui keep committed
(clarus/glsl/) are compiled again too, and a blob that differs fails the run.
"""

import argparse
import os
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
PROGRAMS = ["gfx_smoke", "gfx_readback", "gfx_depth_read", "gfx_compute_cycle", "demo"]
WINDOW = ["gfx_window"]


def expected(name, driver):
    for d in (REPO / "test" / "gfx", HERE):
        for f in (d / f"{name}.{driver}.out", d / f"{name}.out"):
            if f.exists():
                return f
    return HERE / f"{name}.out"


LIBRARY_SHADERS = [("depth", "gfx", "depth_shaders"), ("ui", "ui", "ui_shaders")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clarus", type=Path, required=True, help="a clarus-lang checkout")
    ap.add_argument("--cc", default="clang" if os.name == "nt" or sys.platform == "darwin" else "gcc")
    ap.add_argument("--window", action="store_true", help="also run the windowed test")
    ap.add_argument("--shaders", type=Path, help="a .tgz of clarus/shaders/, for a machine without goose")
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()
    names = args.names or PROGRAMS + (WINDOW if args.window else [])
    out = REPO / "build" / "clarus"
    out.mkdir(parents=True, exist_ok=True)
    if args.shaders:
        with tarfile.open(args.shaders) as t:
            t.extractall(HERE, filter="data")
    else:
        subprocess.run([sys.executable, HERE / "shaders.py", HERE / "test", HERE / "shaders"], check=True)
    failed = []
    if not args.shaders:
        for glsl, module, name in LIBRARY_SHADERS:
            fresh = out / "library-shaders" / module
            subprocess.run([sys.executable, HERE / "shaders.py", HERE / "glsl" / glsl, fresh, name], check=True)
            for ext in (".h", ".clarus"):
                if (fresh / (name + ext)).read_bytes() != (HERE / module / (name + ext)).read_bytes():
                    print(f"FAIL clarus/{module}/{name}{ext} is not what shaders.py makes from clarus/glsl/{glsl}")
                    failed.append(name)
    # Always: CMake rebuilds what changed in src/ and does nothing else.
    subprocess.run([sys.executable, HERE / "native.py", out / "native", "--cc", args.cc], check=True)
    for name in names:
        exe = out / (name + (".exe" if os.name == "nt" else ""))
        build = subprocess.run([sys.executable, args.clarus / "compiler" / "clarusc.py", HERE / f"{name}.clarus",
                                "-o", exe, "--cc", args.cc, "-I", HERE / "gfx",
                                f"--link=@{(out / 'native' / 'link.rsp').as_posix()}"],
                               capture_output=True, text=True)
        if build.returncode != 0:
            print(f"FAIL {name}: build\n{build.stdout}{build.stderr}")
            failed.append(name)
            continue
        run = subprocess.run([exe], capture_output=True, text=True, cwd=out, timeout=120)
        lines = run.stdout.splitlines()
        driver = next((l[len("driver: "):] for l in lines if l.startswith("driver: ")), "")
        got = [l for l in lines if not l.startswith("driver: ")]
        want = expected(name, os.environ.get("SDL_GPU_DRIVER") or driver).read_text().splitlines()
        if run.returncode != 0 or got != want:
            print(f"FAIL {name} ({driver or 'no driver line'}): exit {run.returncode}")
            print("  want: " + "\n        ".join(want))
            print("  got:  " + "\n        ".join(got))
            if run.stderr:
                print("  stderr: " + run.stderr.strip())
            failed.append(name)
        else:
            print(f"ok   {name}" + (f" ({driver})" if driver else ""))
    print(f"{len(names) - len([f for f in failed if f in names])} of {len(names)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
