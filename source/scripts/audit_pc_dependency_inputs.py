#!/usr/bin/env python3
"""Fail closed on external build inputs; audit generated MSVC projects and actual read logs."""
from pathlib import Path
import argparse
import json
import os
import xml.etree.ElementTree as ET

def within(path, roots):
    return any(path.is_relative_to(root) for root in roots)

def project_inputs(root):
    found = set()
    for project in root.rglob("*.vcxproj"):
        tree = ET.parse(project)
        for node in tree.iter():
            tag = node.tag.rsplit("}", 1)[-1]
            if tag in {"ClCompile", "ClInclude", "ResourceCompile", "CustomBuild", "None"} and "Include" in node.attrib:
                values = [node.attrib["Include"]]
            elif tag in {"AdditionalIncludeDirectories", "AdditionalLibraryDirectories", "AdditionalDependencies"}:
                values = (node.text or "").split(";")
            else:
                continue
            for value in values:
                value = value.strip().removeprefix("/WHOLEARCHIVE:")
                if "$" in value or "%" in value or not value:
                    continue
                path = Path(value)
                if path.is_absolute():
                    found.add(path.resolve())
                elif tag in {"ClCompile", "ClInclude", "ResourceCompile", "CustomBuild", "None"}:
                    found.add((project.parent / path).resolve())
    return found

def read_inputs(root):
    found = set()
    for log in root.rglob("*.read.*.tlog"):
        # MSVC file tracking logs carry UTF-16 BOMs.
        data = log.read_bytes()
        text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8", errors="strict")
        for line in text.splitlines():
            for value in line.lstrip("^").split("|"):
                path = Path(value)
                if path.is_absolute():
                    found.add(path.resolve())
    return found

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-read-logs", action="store_true")
    args = parser.parse_args()
    # Explicit closure: integration/build/cache plus installed Windows/MSVC tools.
    # This does not modify filesystem ACLs or claim an OS security sandbox.
    roots = [args.repo.resolve(), args.build.resolve(), args.dependencies.resolve(),
             Path(os.environ.get("SystemRoot", "C:/Windows")).resolve(),
             Path("C:/Program Files/Microsoft Visual Studio/2022").resolve(),
             Path("C:/Program Files (x86)/Windows Kits").resolve()]
    declared = project_inputs(args.build) | project_inputs(args.dependencies / "openxr-build")
    read = read_inputs(args.build) | read_inputs(args.dependencies / "openxr-build")
    failures = sorted(str(path) for path in declared | read if not within(path, roots))
    require_logs_failed = args.require_read_logs and not read_inputs(args.build)
    queue = sorted(path.name for path in read if "concurrentqueue" in path.parts)
    result = {"scope": "MSVC declared inputs and tracked actual file reads; no OS sandbox claim",
              "declared_input_count": len(declared), "actual_read_input_count": len(read),
              "unexpected_inputs": failures, "concurrentqueue_headers_read": queue,
              "game_read_logs_missing": bool(require_logs_failed),
              "passed": not failures and not require_logs_failed}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    if not result["passed"]:
        raise SystemExit(1)

if __name__ == "__main__":
    main()
