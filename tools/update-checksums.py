#!/usr/bin/env python3
"""Refresh sha512sums of *local* source files in the pmaports/ overlay APKBUILDs.

Remote sources (URLs) keep their existing checksum. Usage: tools/update-checksums.py [APKBUILD...]
(defaults to every APKBUILD under pmaports/).
"""
import hashlib, pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
files = [pathlib.Path(a) for a in sys.argv[1:]] or sorted((root / "pmaports").rglob("APKBUILD"))
for apkbuild in files:
    text = apkbuild.read_text()
    m = re.search(r'sha512sums="\n(.*?)"', text, re.S)
    if not m:
        continue
    out, changed = [], False
    for line in m.group(1).splitlines():
        digest, _, name = line.partition("  ")
        local = apkbuild.parent / name
        if local.is_file():
            new = hashlib.sha512(local.read_bytes()).hexdigest()
            changed |= new != digest
            digest = new
        out.append(f"{digest}  {name}")
    if changed:
        apkbuild.write_text(text[: m.start(1)] + "\n".join(out) + "\n" + text[m.end(1):])
        print(f"updated {apkbuild.relative_to(root)}")
