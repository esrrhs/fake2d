#!/usr/bin/env python3
"""Migrate a fake2d demo from file-level mutable locals to a NativeObject.

FakeLua treats every file-level `local` as a const binding, so the old
`local score = 0 + 0` idiom no longer compiles: a script dies at
"cannot reassign file-level constant 'score'". The sanctioned replacement is a
host-side NativeObject whose fields read and write like an ordinary table.

This tool moves exactly the locals that are actually reassigned somewhere
outside their own declaration line. Names that are never reassigned stay put --
they are constants and FakeLua is happy with them.

  python3 tools/migrate_state.py scripts/platformer_demo.lua
"""

import re
import sys

KEY = "state"


def fail(msg):
    print(msg, file=sys.stderr)
    raise SystemExit(1)


def strip_comment(line):
    """Split a line into (code, comment) at the first `--` outside strings."""
    for start, end in outside_strings(line):
        idx = line[start:end].find("--")
        if idx >= 0:
            cut = start + idx
            return line[:cut], line[cut:]
    return line, ""


def outside_strings(line):
    """Yield (start, end) spans of the line that are not inside a string."""
    spans = []
    i = 0
    while i < len(line):
        if line[i] == '"':
            j = line.find('"', i + 1)
            if j < 0:
                break
            i = j + 1
            continue
        if line[i] == "'":
            j = line.find("'", i + 1)
            if j < 0:
                break
            i = j + 1
            continue
        start = i
        while i < len(line) and line[i] not in "\"'":
            i += 1
        spans.append((start, i))
    return spans


def qualify(line, names, obj):
    """Rewrite bare occurrences of `names` to `obj.name` outside strings."""
    code, comment = strip_comment(line)
    if not code.strip():
        return line
    alt = "|".join(re.escape(n) for n in names)
    pattern = re.compile(r"(?<![\w.])\b(" + alt + r")\b")
    pieces = []
    pos = 0
    for start, end in outside_strings(code):
        seg = code[start:end]
        pieces.append(code[pos:start])
        pieces.append(pattern.sub(lambda m: f"{obj}.{m.group(1)}", seg))
        pos = end
    pieces.append(code[pos:])
    result = "".join(pieces) + comment
    # A table constructor key is not a variable: `{ rows = 8 }` must stay put.
    # Only strip right after `{` -- stripping after a comma would undo the
    # second name of a multi-assignment like `a.x, a.y = 1, 2`.
    result = re.sub(r"\{\s*" + re.escape(obj) + r"\.(\w+)\s*=", r"{\1 =", result)
    return result


DECL = re.compile(r"^local\s+([\w,\s]+?)\s*=\s*(.+)$")


def declared_names(line):
    """Names declared by a file-level `local a, b = x, y` line, or None."""
    m = DECL.match(line)
    if not m or m.group(1).strip().startswith("function"):
        return None
    return [n.strip() for n in m.group(1).split(",") if n.strip()]


def main():
    path = sys.argv[1]
    src = open(path).read()
    if "new_global_obj" in src:
        print(f"{path}: already migrated")
        return
    lines = src.split("\n")

    # 1. every file-level local name (tables included)
    file_locals = []
    for line in lines:
        names = declared_names(line)
        if names:
            file_locals.extend(names)

    # 2. names assigned anywhere outside their own declaration line are mutable
    mutable = set()
    for idx, line in enumerate(lines):
        code, _ = strip_comment(line)
        if code.startswith("local "):
            continue
        for name in file_locals:
            if re.search(rf"(?<![\w.]){name}\s*=[^=]", code) or \
               re.search(rf"(?<![\w.]){name}\s*,[\w\s,]*=", code):
                mutable.add(name)

    # 3. never move the object itself or helper functions; pick a free name
    global KEY
    # The object binding must not collide with any local in the file, at any
    # scope: FakeLua rejects `local state` inside a function when `state` is
    # already a file-level binding.
    taken = set(re.findall(r"\blocal\s+(\w+)", src))
    taken.update(re.findall(r"\bfor\s+(\w+)\s*=", src))
    for candidate in ("S", "ST", "GS", "STATE_OBJ"):
        if candidate not in taken:
            KEY = candidate
            break
    else:
        fail(f"{path}: no free object name")
    mutable = {n for n in mutable if n != KEY}
    if not mutable:
        print(f"{path}: nothing mutable at file level")
        return

    # 4. capture the initialisers before rewriting the declarations away
    seeds = []
    out = []
    for line in lines:
        names = declared_names(line)
        if names and all(n in mutable for n in names):
            m = DECL.match(line)
            rhs = [r.strip() for r in split_top(m.group(2))]
            if len(rhs) == 1 and len(names) > 1:
                rhs = rhs * len(names)
            for i, n in enumerate(names):
                seeds.append((n, rhs[i] if i < len(rhs) else "0"))
            continue
        out.append(line)

    # 5. qualify every remaining use
    out = [qualify(l, sorted(mutable, key=len, reverse=True), KEY) for l in out]

    # 6. emit the object + a seeding function, and call it from the init guard
    key = path.split("/")[-1].replace(".lua", "")
    block = [
        f"-- FakeLua binds file-level locals as const, so everything that changes",
        f"-- across frames lives in a host-side NativeObject instead (see",
        f"-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.",
        f"local {KEY} = new_global_obj(\"{key}\", \"state\")",
        "",
        f"local function reset_state()",
    ]
    for n, expr in seeds:
        block.append(f"    {KEY}.{n} = {expr}")
    block += [f"end", ""]

    text = "\n".join(out)
    # insert the block after the last file-level declaration group: put it right
    # before the first function definition that is not a plain constant
    anchor = re.search(r"^local function ", text, re.M)
    if not anchor:
        fail(f"{path}: no insertion anchor found")
    text = text[:anchor.start()] + "\n".join(block) + "\n" + text[anchor.start():]

    # call reset_state() first thing in the first-frame guard
    guard = re.search(r"^(\s*)if not " + KEY + r"\.inited then\s*$", text, re.M)
    if guard:
        text = (text[:guard.end()] + "\n" + guard.group(1) + "    reset_state()"
                + text[guard.end():])
    else:
        print(f"{path}: WARNING no `if not inited then` guard; seed it by hand")
    open(path, "w").write(text)
    print(f"{path}: moved {len(mutable)} locals into a NativeObject: "
          f"{', '.join(sorted(mutable))}")


def split_top(expr):
    """Split a comma-separated initializer list at top level only."""
    parts, depth, cur, instr = [], 0, "", None
    for ch in expr:
        if instr:
            cur += ch
            if ch == instr:
                instr = None
            continue
        if ch in "\"'":
            instr = ch
        elif ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
            continue
        cur += ch
    parts.append(cur)
    return parts


if __name__ == "__main__":
    main()
