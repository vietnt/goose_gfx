"""Writes the Clarus side of the gfx and ui layers' boundaries from their C API headers.

    python clarus/gen.py            write clarus/gfx/ and clarus/ui/: api.clarus and gsc_gfx.h, gsc_ui.h
    python clarus/gen.py --check    fail when any is not what this script writes

src/gfx/gfx_api.h and src/ui/ui_api.h list every function (GS_GFX_API,
GS_UI_API), struct and constant of their layer once. From each this writes:

* gsc_MODULE.h: a Clarus name for each struct (`typedef gs_gfx_texture_desc
  TextureDesc;`, Goose's names; one that gfx already names, as ui's Float2, is
  UiFloat2, since one C program holds both), and one static inline adapter for
  each function, `gsc_MODULE_NAME`. An adapter takes a slice as Clarus passes
  one, its data pointer and its count (X20), and hands the layer the
  `{ data, len }` struct it takes by value, which Clarus has no form for.
* api.clarus: an `extern struct` for each struct (the header keeps the
  layout, X15), a `const` for each constant, a `private extern fn` for each
  adapter (X1 checks each against gsc_gfx.h), and for each struct gfx.goose
  gives field defaults, a function that returns it with them, as `pass_desc()`
  (a Clarus field has no default, V1).

The public functions are written by hand in gfx.clarus and ui.clarus beside them.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULES = ["gfx", "ui"]

SCALARS = {"uint8_t": "u8", "int8_t": "i8", "uint16_t": "u16", "int16_t": "i16", "int32_t": "i32",
           "uint32_t": "u32", "int64_t": "i64", "uint64_t": "u64", "float": "f32", "double": "f64"}


MOD, PREFIX = "gfx", "gs_gfx_"
TAKEN = set()       # Clarus names the modules written before this one give their structs
RENAMED = {}


def camel(cname):
    if cname in RENAMED:
        return RENAMED[cname]
    return "".join(w[:1].upper() + w[1:] for w in cname[len(PREFIX):].split("_"))


def strip_comments(s):
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)


def parse_structs(text):
    structs = {}
    for body, name in re.findall(r"typedef struct \{(.*?)\}\s*(" + PREFIX + r"\w+);", strip_comments(text), re.S):
        # A slice is { T *data; int64_t len; }; gs_ui_nine_slice only ends like one.
        if re.fullmatch(r"\s*\w+ \*data; int64_t len; ", body):
            continue
        fields = []
        for decl in strip_comments(body).split(";"):
            decl = " ".join(decl.split())
            if not decl:
                continue
            ctype, names = decl.split(" ", 1)
            for n in names.split(","):
                n = n.strip()
                m = re.fullmatch(r"(\w+)\[(\d+)\]", n)
                fields.append((m.group(1), ctype, int(m.group(2))) if m else (n, ctype, None))
        structs[name] = fields
    return structs


def parse_slices(text):
    return {name: elem for elem, name in
            re.findall(r"typedef struct \{\s*(\w+) \*data; int64_t len; \}\s*(" + PREFIX + r"\w+_slice);", text)}


def parse_functions(text):
    block = re.search(r"#define GS_" + MOD.upper() + r"_API\(X\)(.*?)\n\n", text, re.S).group(1)
    block = strip_comments(block).replace("\\\n", " ")
    return [(ret, name, [] if params.strip() == "void" else [p.strip() for p in params.split(",")])
            for ret, name, params in re.findall(r"X\(([\w ]+?),\s*(" + PREFIX + r"\w+),\s*\((.*?)\)\)", block)]


def parse_constants(text):
    block = re.search(r"#define GS_" + MOD.upper() + r"_CONSTANTS\(X\)(.*?)\n\n", text, re.S).group(1)
    return re.findall(r"X\((\w+),\s*(\w+),\s*(-?\w+)\)", strip_comments(block))


def parse_defaults():
    """Goose's field defaults, per struct: {Name: [(field, goose expression)]}."""
    out, name = {}, None
    for line in (ROOT / f"{MOD}.goose").read_text().splitlines():
        line = re.sub(r"//.*", "", line).strip()
        m = re.fullmatch(r"struct (\w+) \{", line)
        if m:
            name, out[m.group(1)] = m.group(1), []
        elif line == "}":
            name = None
        elif name:
            parts, depth, cur = [], 0, ""
            for ch in line:
                depth += ch in "{[" and 1 or ch in "}]" and -1 or 0
                if ch == "," and depth == 0:
                    parts.append(cur)
                    cur = ""
                else:
                    cur += ch
            for part in parts + [cur]:
                d = re.fullmatch(r"\s*(\w+):\s*[\w\[\]0-9]+\s*=\s*(.+?)\s*", part)
                if d:
                    out[name].append((d.group(1), d.group(2)))
    return out


def ctype_clarus(ctype):
    if ctype in SCALARS:
        return SCALARS[ctype]
    if ctype.startswith(PREFIX):
        return camel(ctype)
    raise SystemExit(f"gen.py: no Clarus type for C {ctype}")


def literal(expr, ctype, structs):
    """Goose's default `expr` for a field of C type `ctype` as a Clarus value, or None when it is zero (V1)."""
    expr = {"true": "1", "false": "0"}.get(expr.strip(), expr.strip())
    if re.fullmatch(r"-?\d+(\.\d+)?", expr):
        return None if float(expr) == 0 else expr
    if re.fullmatch(r"\w+ \{\s*\}", expr):
        return None
    lit = re.fullmatch(r"\w+ \{ (.*) \}", expr)
    if lit and ctype in structs:
        values = [v.strip() for v in lit.group(1).split(",")]
        fields = structs[ctype]
        if len(values) != len(fields) or any(n for _, _, n in fields):
            raise SystemExit(f"gen.py: cannot write the default {expr}")
        parts = [(f, literal(v, t, structs)) for (f, t, _), v in zip(fields, values)]
        if all(v is None for _, v in parts):
            return None
        return camel(ctype) + " { " + ", ".join(f"{f}: {v or ('0.0' if t in ('float', 'double') else '0')}"
                                              for (f, v), (_, t, _) in zip(parts, fields)) + " }"
    raise SystemExit(f"gen.py: cannot write the default {expr}")


def default_lines(sname, fields, defaults, structs):
    """Clarus statements that set `d`'s fields to Goose's defaults, leaving zeros out."""
    kinds = {f: (t, n) for f, t, n in fields}
    lines = []
    for f, expr in defaults:
        ctype, count = kinds[f]
        arr = re.fullmatch(r"\[(.*); (\d+)\]", expr.strip())
        value = literal(arr.group(1) if arr else expr, ctype, structs)
        if value is None:
            continue
        lines.append(f"\tfor i in 0..{count} {{ d.{f}[i] = {value} }}" if arr else f"\td.{f} = {value}")
    return lines


def generate():
    """The two files of module MOD, from its header."""
    text = (ROOT / "src" / MOD / f"{MOD}_api.h").read_text()
    structs = parse_structs(text)
    slices = parse_slices(text)
    functions = parse_functions(text)
    constants = parse_constants(text)
    defaults = parse_defaults()
    for name in structs:
        plain = "".join(w[:1].upper() + w[1:] for w in name[len(PREFIX):].split("_"))
        if plain in TAKEN:
            RENAMED[name] = MOD[:1].upper() + MOD[1:] + plain
    goose_name = {camel(n): n for n in structs}

    src = f"src/{MOD}/{MOD}_api.h"
    h = [f"/* Generated by goose_gfx/clarus/gen.py from {src}. Do not edit. */",
         f"#ifndef GSC_{MOD.upper()}_H", f"#define GSC_{MOD.upper()}_H", "",
         f'#include "../../{src}"', *(['#include "gsc_shader.h"'] if MOD == "gfx" else []), ""]
    for name in structs:
        h.append(f"typedef {name} {camel(name)};")
    h.append("")

    c = [f"// Generated by goose_gfx/clarus/gen.py from {src}. Do not edit.",
         f"// The boundary of the {MOD} layer: its structs, constants and functions, as {MOD}.clarus uses them.", "",
         f'include "gsc_{MOD}.h"', ""]
    copies = set()
    for name, fields in structs.items():
        plain = all((t in SCALARS or t in copies) and n is None for _, t, n in fields)
        if plain:
            copies.add(name)
        body = ", ".join(f"{f}: [{ctype_clarus(t)}; {n}]" if n else f"{f}: {ctype_clarus(t)}" for f, t, n in fields)
        c.append(f"{'copy ' if plain else ''}extern struct {camel(name)} {{ {body} }}")
    c.append("")
    for ctype, name, value in constants:
        c.append(f"const {name}: {ctype} = {value}")
    c.append("")

    for ret, name, params in functions:
        cparams, call, cl = [], [], []
        for p in params:
            words = p.replace("*", " * ").split()
            pname, const = words[-1], words[0] == "const"
            ctype = words[-2 if words[-2] != "*" else -3] + (" *" if "*" in words else "")
            if ctype == PREFIX + "bytes" or ctype in slices:
                elem = "uint8_t" if ctype == PREFIX + "bytes" else slices[ctype]
                write = pname.startswith("out")
                cparams += [f"{'' if write else 'const '}{elem} *{pname}", f"int64_t {pname}_n"]
                call.append(f"({ctype}){{ ({elem} *){pname}, {pname}_n }}")
                ct = ctype_clarus(elem)
                cl += [f"{pname}: {'SliceMut' if write else 'Slice'}<{ct}>", f"{pname}_n: i64 = len({pname})"]
            elif ctype.endswith("*"):
                base = ctype[:-1].strip()
                cparams.append(f"{'const ' if const else ''}{base} *{pname}")
                call.append(pname)
                cl.append(f"{pname}: &{ctype_clarus(base)}" if const else f"mut {pname}: {ctype_clarus(base)}")
            else:
                cparams.append(f"{ctype} {pname}")
                call.append(pname)
                cl.append(f"{pname}: {ctype_clarus(ctype)}")
        short = name[len(PREFIX):]
        ret = ret.strip()
        h.append(f"static inline {ret} gsc_{MOD}_{short}({', '.join(cparams) or 'void'}) "
                 f"{{ {'' if ret == 'void' else 'return '}{name}({', '.join(call)}); }}")
        rt = "" if ret == "void" else f" -> {ctype_clarus(ret)}"
        c.append(f"private extern fn gsc_{MOD}_{short}({', '.join(cl)}){rt}")
    c.append("")

    for name, fields in structs.items():
        sname = camel(name)
        gname = "".join(w[:1].upper() + w[1:] for w in name[len(PREFIX):].split("_"))
        lines = default_lines(sname, fields, defaults.get(gname, []), structs)
        if not lines:
            continue
        fname = re.sub(r"(?<!^)([A-Z])", r"_\1", sname).lower()
        c += [f"// {sname} with the field defaults {MOD}.goose gives it.",
              f"fn {fname}() -> {sname} {{", f"\tvar d = {sname} {{ .. }}", *lines, "\treturn d", "}", ""]

    TAKEN.update(camel(n) for n in structs)
    h += ["", "#endif", ""]
    return {f"gsc_{MOD}.h": "\n".join(h), "api.clarus": "\n".join(c)}


def main():
    global MOD, PREFIX
    stale, wrote = [], []
    for MOD in MODULES:
        PREFIX = f"gs_{MOD}_"
        out = ROOT / "clarus" / MOD
        for n, s in generate().items():
            if "--check" in sys.argv:
                if not (out / n).exists() or (out / n).read_text() != s:
                    stale.append(f"{MOD}/{n}")
            else:
                out.mkdir(parents=True, exist_ok=True)
                (out / n).write_text(s)
                wrote.append(f"{MOD}/{n}")
    if stale:
        sys.exit(f"gen.py: {', '.join(stale)} out of date with the layers' headers; run python clarus/gen.py")
    if wrote:
        print(f"gen.py: wrote {', '.join(wrote)}")


if __name__ == "__main__":
    main()
