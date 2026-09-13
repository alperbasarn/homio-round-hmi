#!/usr/bin/env python3
"""BLD-42 companion to check-component-graph.py: every first-party component that includes a header
owned by another first-party component must name that component in REQUIRES or PRIV_REQUIRES.

    python3 tools/check-include-edges.py esp-idf

Exit 0 when clean, 1 when a component includes what it does not require.
"""
import os
import re
import sys

# BLD-42 acceptance helper: every first-party component that includes a header owned by another
# first-party component must name that component in REQUIRES or PRIV_REQUIRES.
root = sys.argv[1]
comps = {}
for name in sorted(os.listdir(os.path.join(root, "components"))):
    d = os.path.join(root, "components", name)
    if os.path.isfile(os.path.join(d, "CMakeLists.txt")):
        comps[name] = d
comps["main"] = os.path.join(root, "main")

owner = {}
for name, d in comps.items():
    for dp, _, files in os.walk(d):
        if "third_party" in dp or "managed_components" in dp:
            continue
        for f in files:
            if f.endswith((".h", ".hpp")):
                owner.setdefault(f, set()).add(name)
                rel = os.path.relpath(os.path.join(dp, f), d).replace("\\", "/")
                if rel.startswith("include/"):
                    owner.setdefault(rel[len("include/"):], set()).add(name)

inc_re = re.compile(r'#include\s+"([^"]+)"')
KEYWORDS = r'\b(SRCS|SRC_DIRS|INCLUDE_DIRS|PRIV_INCLUDE_DIRS|REQUIRES|PRIV_REQUIRES|EMBED_FILES|EMBED_TXTFILES|KCONFIG|KCONFIG_PROJBUILD|LDFRAGMENTS|WHOLE_ARCHIVE)\b'

def declared_deps(cmake_text):
    declared = set()
    parts = re.split(KEYWORDS, cmake_text)
    for i, part in enumerate(parts):
        if part in ("REQUIRES", "PRIV_REQUIRES") and i + 1 < len(parts):
            block = parts[i + 1].split(")")[0]
            declared.update(t.strip('"') for t in block.split())
    return declared

problems = 0
for name, d in comps.items():
    cm = open(os.path.join(d, "CMakeLists.txt"), encoding="utf-8").read()
    declared = declared_deps(cm)
    used = set()
    for dp, _, files in os.walk(d):
        if "third_party" in dp:
            continue
        for f in files:
            if f.endswith((".cpp", ".h", ".c", ".hpp")):
                text = open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()
                for inc in inc_re.findall(text):
                    key = inc if inc in owner else os.path.basename(inc)
                    for o in owner.get(key, ()):
                        if o != name:
                            used.add(o)
    missing = sorted(u for u in used if u not in declared)
    if missing:
        problems += 1
        print(f"{name}: includes headers of {missing} but does not require them")
print(f"{len(comps)} first-party components checked; {problems} with undeclared include edges")
sys.exit(1 if problems else 0)
