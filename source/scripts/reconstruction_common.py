"""Pinned reconstruction data operations; never use existing generated inputs."""
from pathlib import Path
import difflib
import hashlib
import json
import re
from prepare_demo_source import mask, end_pair

FUNCTION = re.compile(r"RECOMP_FUNC void (\w+)\([^\n]*\) \{")

def require(condition, message):
    if not condition:
        raise ValueError(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))

def save(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")

def safe_path(root, relative):
    path = (root / relative).resolve()
    require(not Path(relative).is_absolute() and path.is_relative_to(root.resolve()),
            "Path escapes owned source/output: " + relative)
    return path

def split_functions(folder):
    functions, layouts = {}, {}
    for path in sorted(folder.glob("funcs_*.c")):
        source = path.read_text(encoding="utf-8")
        code, segments, offset = mask(source), [], 0
        for match in FUNCTION.finditer(source):
            name = match.group(1)
            end = end_pair(code, match.end() - 1, "{", "}")
            require(name not in functions, "Duplicate generated function: " + name)
            segments.extend([{"literal": source[offset:match.start()]}, {"function": name}])
            functions[name] = source[match.start():end]
            offset = end
        segments.append({"literal": source[offset:]})
        layouts[path.name] = segments
    return functions, layouts

def make_edit(before, after):
    old, new = before.splitlines(keepends=True), after.splitlines(keepends=True)
    changes = []
    for tag, a, b, c, d in difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes():
        if tag != "equal":
            changes.append({"line": a, "delete": old[a:b], "insert": new[c:d]})
    return {"before_sha256": digest(before.encode()), "after_sha256": digest(after.encode()),
            "changes": changes}

def apply_edit(source, edit):
    require(digest(source.encode()) == edit["before_sha256"], "Patch preimage differs")
    lines = source.splitlines(keepends=True)
    for change in reversed(edit["changes"]):
        start = change["line"]
        require(lines[start:start + len(change["delete"])] == change["delete"], "Patch context differs")
        lines[start:start + len(change["delete"])] = change["insert"]
    result = "".join(lines)
    require(digest(result.encode()) == edit["after_sha256"], "Patch result differs")
    return result

def write_candidate(path, text, record):
    # Explicit bytes retain the candidate's LF/CRLF convention independently of host defaults.
    require(record["newline"] in ("lf", "crlf"), "Unsupported newline convention")
    raw = text.replace("\n", "\r\n").encode("utf-8") if record["newline"] == "crlf" else text.encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)

def verify_inventory(root, records):
    for relative, record in records.items():
        path = safe_path(root, relative)
        require(path.is_file() and digest(path.read_bytes()) == record["sha256"],
                "Pinned file differs or is missing: " + relative)

def verify_source_candidate(root):
    manifest = load(root / "source-manifest.json")
    require(manifest["schema"] == "xr64.private-reconstruction-source.v1", "Wrong source manifest")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
    require(actual == set(manifest["files"]) | {"source-manifest.json"}, "Source candidate inventory differs")
    require(not any(p.is_symlink() for p in root.rglob("*")), "Source candidate links are forbidden")
    verify_inventory(root, manifest["files"])
    return manifest
