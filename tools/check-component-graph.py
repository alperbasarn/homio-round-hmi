#!/usr/bin/env python3
"""BLD-45: report-only dump of the first-party component dependency graph.

Reads a build's project_description.json, prints every REQUIRES / PRIV_REQUIRES
edge between first-party components (those under the project directory), and
lists any cycles. Never fails the build: exit code is always 0.

    python tools/check-component-graph.py <build_dir>/project_description.json
"""
import json
import os
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print("[component-graph] usage: check-component-graph.py <project_description.json>")
        return 0
    try:
        with open(sys.argv[1], encoding="utf-8") as fh:
            desc = json.load(fh)
    except Exception as exc:  # report-only tool: never fail the build
        print(f"[component-graph] could not read {sys.argv[1]}: {exc}")
        return 0

    project_path = os.path.normpath(desc.get("project_path", ""))
    info = desc.get("build_component_info", {})
    first_party = {
        name for name, comp in info.items()
        if os.path.normpath(comp.get("dir", "")).startswith(project_path)
    }

    edges = []  # (src, dst, kind)
    for name in sorted(first_party):
        comp = info[name]
        for dst in comp.get("reqs", []):
            if dst in first_party:
                edges.append((name, dst, "public"))
        for dst in comp.get("priv_reqs", []):
            if dst in first_party:
                edges.append((name, dst, "private"))

    print(f"[component-graph] {desc.get('project_name', '?')} target={desc.get('target', '?')} "
          f"first-party components={len(first_party)} edges={len(edges)}")
    for src, dst, kind in edges:
        print(f"[component-graph]   {src} -> {dst} ({kind})")

    # cycle detection over first-party edges (public and private alike)
    adj = {n: [] for n in first_party}
    for src, dst, _ in edges:
        adj[src].append(dst)
    colour = {}
    stack = []
    cycles = []

    def visit(node):
        colour[node] = 1
        stack.append(node)
        for nxt in adj[node]:
            c = colour.get(nxt, 0)
            if c == 1:
                cycles.append(stack[stack.index(nxt):] + [nxt])
            elif c == 0:
                visit(nxt)
        stack.pop()
        colour[node] = 2

    for node in sorted(first_party):
        if colour.get(node, 0) == 0:
            visit(node)

    if cycles:
        print(f"[component-graph] {len(cycles)} cycle(s) among first-party components:")
        for cyc in cycles:
            print("[component-graph]   " + " -> ".join(cyc))
    else:
        print("[component-graph] no cycles among first-party components")
    return 0


if __name__ == "__main__":
    sys.exit(main())
