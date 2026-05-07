#!/usr/bin/env python3
# Verifies every #include "..." path matches the actual filesystem case.
# Catches issues like upstream's lowercase "g_playstats.h" when the file is "g_PlayStats.h",
# which silently builds on Windows but fails on case-sensitive filesystems.
import os, re, sys

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
SKIP_DIRS = {'.git', 'build', 'build2', 'build3', '__pycache__'}
SOURCE_EXTS = ('.c', '.cpp', '.cc', '.h', '.hpp')

def collect_sources(root='.'):
    files = []
    for d, dirs, fs in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
        for f in fs:
            if f.endswith(SOURCE_EXTS):
                files.append(os.path.join(d, f))
    return files

def main():
    sources = collect_sources()

    # Map lowercase basename -> set of actual basenames seen.
    by_basename = {}
    for s in sources:
        bn = os.path.basename(s)
        by_basename.setdefault(bn.lower(), set()).add(bn)

    mismatches = []
    for src in sources:
        try:
            with open(src, errors='replace') as fp:
                for lineno, line in enumerate(fp, 1):
                    m = INCLUDE_RE.match(line)
                    if not m:
                        continue
                    inc_bn = os.path.basename(m.group(1))
                    actual = by_basename.get(inc_bn.lower())
                    if actual is None:
                        continue  # external / system header
                    if inc_bn not in actual:
                        mismatches.append((src, lineno, m.group(1), sorted(actual)[0]))
        except UnicodeDecodeError:
            continue

    for src, lineno, inc, actual in mismatches:
        print(f"{src}:{lineno}: includes '{inc}' but file is '{actual}'")

    return 1 if mismatches else 0

if __name__ == '__main__':
    sys.exit(main())
