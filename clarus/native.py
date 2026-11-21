"""Builds this package's C layers for a Clarus program, and writes the link line.

    python clarus/native.py OUT [--cc clang]

configures and builds goose_gfx_package (SDL3, gfx, ui, audio: CMakeLists.txt
beside this directory) under OUT with the C compiler that clarusc will use, and
writes OUT/link.rsp: the package's archives, SDL3's and every system library
or macOS framework they need, in link order, one per line. A Clarus program
links the package with

    python clarus-lang/compiler/clarusc.py prog.clarus -o prog --cc clang \\
        -I goose_gfx/clarus/gfx --link=@OUT/link.rsp

The link line is the one CMake gives a probe executable linked to
goose_gfx_package, read from CMake's file API reply: the method of the Goose
compiler (vietnt/goose src/native.h and cmake/linkclosure.cmake), so no list
of libraries is kept here. The static C runtime on Windows matches what clang
links by default there (libcmt).
"""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

PACKAGE = Path(__file__).resolve().parent.parent

WRAPPER = """cmake_minimum_required(VERSION 3.20)
project(goose_gfx_clarus C)
if(APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_subdirectory("{package}" pkg EXCLUDE_FROM_ALL)
add_executable(clarus_linkprobe probe.c)
target_link_libraries(clarus_linkprobe PRIVATE goose_gfx_package)
"""


def run(cmd):
    print("+", " ".join(shlex.quote(str(c)) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def link_closure(build, target):
    reply = build / ".cmake" / "api" / "v1" / "reply"
    index = json.loads(sorted(reply.glob("index-*.json"))[-1].read_text())
    codemodel = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text())
    config = codemodel["configurations"][0]
    entry = next(t for t in config["targets"] if t["name"] == target)
    fragments = json.loads((reply / entry["jsonFile"]).read_text())["link"]["commandFragments"]
    items = []
    for f in fragments:
        if f["role"] not in ("libraries", "libraryPath", "frameworkPath"):
            continue
        for w in shlex.split(f["fragment"], posix=os.name != "nt"):
            if os.name == "nt":
                w = w.strip('"')
            # clang reads the response file with GNU quoting, where a backslash escapes: forward slashes only.
            if ("/" in w or "\\" in w) and (build / w).exists():
                w = (build / w).resolve().as_posix()
            elif (os.name == "nt" and w.lower().endswith(".lib") and not w.startswith("-")
                  and "/" not in w and "\\" not in w):
                w = "-l" + w[:-4]
            items.append(w)
    return items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--cc", default="clang" if os.name == "nt" or sys.platform == "darwin" else "gcc")
    args = ap.parse_args()
    out = args.out.resolve()
    src = out / "wrapper"
    build = out / "cmake"
    src.mkdir(parents=True, exist_ok=True)
    (src / "CMakeLists.txt").write_text(WRAPPER.format(package=PACKAGE.as_posix()))
    (src / "probe.c").write_text("int main(void) { return 0; }\n")
    query = build / ".cmake" / "api" / "v1" / "query"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").write_text("")
    configure = ["cmake", "-S", src, "-B", build, "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_COMPILER={args.cc}"]
    if shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    run(configure)
    run(["cmake", "--build", build, "--target", "clarus_linkprobe", "--parallel"])
    items = link_closure(build, "clarus_linkprobe")
    (out / "link.rsp").write_text("".join((f'"{i}"' if " " in i else i) + "\n" for i in items))
    print(f"wrote {out / 'link.rsp'} ({len(items)} items)")


if __name__ == "__main__":
    main()
