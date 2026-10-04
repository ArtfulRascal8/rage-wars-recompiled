"""Bounded personal-path and credential marker gate; not a complete privacy audit."""
from pathlib import Path
import argparse,json,re,hashlib
PATTERNS={
 "personal_path":re.compile(r"(?:[A-Za-z]:[\\/](?:Users|home)[\\/]|/home/|/Users/)",re.I),
 "private_key":re.compile(r"-----BEGIN (?:[A-Z ]+)?PRIVATE KEY-----"),
 "github_token":re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})"),
}
def inspect(path):
 data=path.read_bytes()
 texts=[data.decode("latin1"),data.decode("utf-16le",errors="ignore"),data[1:].decode("utf-16le",errors="ignore")]
 hits={name:sum(len(pattern.findall(text)) for text in texts) for name,pattern in PATTERNS.items()}
 return {"sha256":hashlib.sha256(data).hexdigest().upper(),"bytes":len(data),"markers":hits}
def main():
 p=argparse.ArgumentParser();p.add_argument("path",type=Path);p.add_argument("--report",type=Path);a=p.parse_args()
 files=sorted(x for x in a.path.rglob("*") if x.is_file()) if a.path.is_dir() else [a.path]
 report={str(x.relative_to(a.path)) if a.path.is_dir() else x.name:inspect(x) for x in files}
 result={"scope":"Personal path, private-key and GitHub-token markers, ASCII and both UTF-16 alignments","files":report}
 if a.report:a.report.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
 failures={name:value["markers"] for name,value in report.items() if any(value["markers"].values())}
 print(json.dumps({"files_checked":len(files),"failures":failures}))
 if failures:raise SystemExit(1)
if __name__=="__main__":main()
