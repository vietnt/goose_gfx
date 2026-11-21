"""Writes the Clarus side of the gfx layer's boundary from its C API header.

    python clarus/gen.py            write clarus/gfx/api.clarus and clarus/gfx/gsc_gfx.h
    python clarus/gen.py --check    fail when either is not what this script writes

src/gfx/gfx_api.h lists every function (GS_GFX_API), struct and constant of
the layer once. From it this writes:

* gsc_gfx.h: a Clarus name for each struct (`typedef gs_gfx_texture_desc
  TextureDesc;`, Goose's names), and one static inline adapter for each
  function, `gsc_gfx_NAME`. An adapter takes a slice as Clarus passes one, its
  data pointer and its count (X20), and hands the layer the `{ data, len }`
  struct it takes by value, which Clarus has no form for.
* api.clarus: an `extern struct` for each struct (the header keeps the
  layout, X15), a `const` for each constant, a `private extern fn` for each
  adapter (X1 checks each against gsc_gfx.h), and for each struct gfx.goose
  gives field defaults, a function that returns it with them, as `pass_desc()`
  (a Clarus field has no default, V1).

The public functions are written by hand in gfx.clarus beside them.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "gfx" / "gfx_api.h"
GOOSE = ROOT / "gfx.goose"
OUT = ROOT / "clarus" / "gfx"

SCALARS = {"uint8_t": "u8", "int8_t": "i8", "uint16_t": "u16", "int16_t": "i16", "int32_t": "i32",
           "uint32_t": "u32", "int64_t": "i64", "uint64_t": "u64", "float": "f32", "double": "f64"}


def camel(cname):
    return "".join(w[:1].upper() + w[1:] for w in cname[len("gs_gfx_"):].split("_"))


def strip_comments(s):
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)


def parse_structs(text):
    structs = {}
    for body, name in re.findall(r"typedef struct \{(.*?)\}\s*(gs_gfx_\w+);", text, re.S):
        if name.endswith("_slice") or name == "gs_gfx_bytes":
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
            re.findall(r"typedef struct \{\s*(gs_gfx_\w+) \*data; int64_t len; \}\s*(gs_gfx_\w+_slice);", text)}


def parse_functions(text):
    block = re.search(r"#define GS_GFX_API\(X\)(.*?)\n\n", text, re.S).group(1)
    block = strip_comments(block).replace("\\\n", " ")
    return [(ret, name, [] if params.strip() == "void" else [p.strip() for p in params.split(",")])
            for ret, name, params in re.findall(r"X\(([\w ]+?),\s*(gs_gfx_\w+),\s*\((.*?)\)\)", block)]


def parse_constants(text):
    block = re.search(r"#define GS_GFX_CONSTANTS\(X\)(.*?)\n\n", text, re.S).group(1)
    return re.findall(r"X\((\w+),\s*(\w+),\s*(-?\w+)\)", strip_comments(block))


def parse_defaults():
    """Goose's field defaults, per struct: {Name: [(field, goose expression)]}."""
    out, name = {}, None
    for line in GOOSE.read_text().splitlines():
        line = re.sub(r"//.*", "", line).strip()
        m = re.fullmatch(r"struct (\w+) \{", line)
        if m:
            name, out[m.group(1)] = m.group(1), []
        elif line == "}":
            name = None
        elif name:
            d = re.fullmatch(r"(\w+):\s*[\w\[\]0-9]+\s*=\s*(.+?),?", line)
            if d:
                out[name].append((d.group(1), d.group(2)))
    return out


def ctype_clarus(ctype):
    if ctype in SCALARS:
        return SCALARS[ctype]
    if ctype.startswith("gs_gfx_"):
        return camel(ctype)
    raise SystemExit(f"gen.py: no Clarus type for C {ctype}")


def default_lines(sname, fields, defaults):
    """Clarus statements that set `d`'s fields to Goose's defaults, leaving zeros out."""
    kinds = {f: (t, n) for f, t, n in fields}
    lines = []
    for f, expr in defaults:
        expr = expr.strip()
        ctype, count = kinds[f]
        value = {"true": "1", "false": "0"}.get(expr, expr)
        vec = re.fullmatch(r"float4 \{ ([^}]*) \}", value)
        arr = re.fullmatch(r"\[(.*); (\d+)\]", value)
        if arr:
            inner = arr.group(1).strip()
            if re.fullmatch(r"0|0\.0|\w+ \{ 0 \}", inner):
                continue
            vec = re.fullmatch(r"float4 \{ ([^}]*) \}", inner)
            lit = "Float4 { " + ", ".join(f"{c}: {v.strip()}" for c, v in zip("xyzw", vec.group(1).split(","))) + " }"
            lines.append(f"\tfor i in 0..{count} {{ d.{f}[i] = {lit} }}")
        elif vec:
            lit = "Float4 { " + ", ".join(f"{c}: {v.strip()}" for c, v in zip("xyzw", vec.group(1).split(","))) + " }"
            lines.append(f"\td.{f} = {lit}")
        elif re.fullmatch(r"\w+ \{ 0 \}", value) or re.fullmatch(r"-?0(\.0)?", value):
            continue
        elif re.fullmatch(r"-?\d+(\.\d+)?", value):
            lines.append(f"\td.{f} = {value}")
        else:
            raise SystemExit(f"gen.py: cannot write the default {expr} of {sname}.{f}")
    return lines


def generate():
    text = HEADER.read_text()
    structs = parse_structs(text)
    slices = parse_slices(text)
    functions = parse_functions(text)
    constants = parse_constants(text)
    defaults = parse_defaults()

    h = ["/* Generated by goose_gfx/clarus/gen.py from src/gfx/gfx_api.h. Do not edit. */",
         "#ifndef GSC_GFX_H", "#define GSC_GFX_H", "",
         '#include "../../src/gfx/gfx_api.h"', '#include "gsc_shader.h"', ""]
    for name in structs:
        h.append(f"typedef {name} {camel(name)};")
    h.append("")

    c = ["// Generated by goose_gfx/clarus/gen.py from src/gfx/gfx_api.h. Do not edit.",
         "// The boundary of the gfx layer: its structs, constants and functions, as gfx.clarus uses them.", "",
         'include "gsc_gfx.h"', ""]
    for name, fields in structs.items():
        plain = all(t in SCALARS and n is None for _, t, n in fields)
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
            if ctype == "gs_gfx_bytes" or ctype in slices:
                elem = "uint8_t" if ctype == "gs_gfx_bytes" else slices[ctype]
                write = pname == "out"
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
        short = name[len("gs_gfx_"):]
        ret = ret.strip()
        h.append(f"static inline {ret} gsc_gfx_{short}({', '.join(cparams) or 'void'}) "
                 f"{{ {'' if ret == 'void' else 'return '}{name}({', '.join(call)}); }}")
        rt = "" if ret == "void" else f" -> {ctype_clarus(ret)}"
        c.append(f"private extern fn gsc_gfx_{short}({', '.join(cl)}){rt}")
    c.append("")

    for name, fields in structs.items():
        sname = camel(name)
        lines = default_lines(sname, fields, defaults.get(sname, []))
        if not lines:
            continue
        fname = re.sub(r"(?<!^)([A-Z])", r"_\1", sname).lower()
        c += [f"// {sname} with the field defaults gfx.goose gives it.",
              f"fn {fname}() -> {sname} {{", f"\tvar d = {sname} {{ .. }}", *lines, "\treturn d", "}", ""]

    h += ["", "#endif", ""]
    return {"gsc_gfx.h": "\n".join(h), "api.clarus": "\n".join(c)}


def main():
    files = generate()
    if "--check" in sys.argv:
        stale = [n for n, s in files.items() if not (OUT / n).exists() or (OUT / n).read_text() != s]
        if stale:
            sys.exit(f"gen.py: {', '.join(stale)} out of date with src/gfx/gfx_api.h; run python clarus/gen.py")
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for n, s in files.items():
        (OUT / n).write_text(s)
    print(f"gen.py: wrote {', '.join(files)} in {OUT}")


if __name__ == "__main__":
    main()
