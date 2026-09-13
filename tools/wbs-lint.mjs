#!/usr/bin/env node
// WBS lint (BLD-39): parses the task tables in dev_notes/mvc_refactor_wbs.md and fails on
// unknown dependency ids, backward cross-phase edges, cycles, a section-1 roll-up that does
// not match the rows, or a section-7 critical-path total that does not match the longest path.
//
//   node tools/wbs-lint.mjs [path/to/wbs.md] [--path]   (--path prints the longest chain as section-7 rows)
//
// Exit 0: clean. Exit 1: at least one finding (all findings are printed).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const wantPath = args.includes('--path');
const file = args.find((a) => !a.startsWith('--')) ?? path.join(here, '..', 'dev_notes', 'mvc_refactor_wbs.md');
const text = fs.readFileSync(file, 'utf8');
const lines = text.split('\n');

const ID_RE = /\b(?:DISP|SCRN|CTRL|INPT|NET|APP|PERI|BLD|MDL|MEDIA|PCKNOB|UIX|LED|OTA)-\d{2,3}[a-z]?\b/g;
const ROW_RE = /^\| ((?:DISP|SCRN|CTRL|INPT|NET|APP|PERI|BLD|MDL|MEDIA|PCKNOB|UIX|LED|OTA)-\d{2,3}[a-z]?) \|/;
const NUM_RE = /^\d*\.?\d+$/;

const findings = [];
const tasks = new Map(); // id -> {id, phase, hours, deps, line}

let section = null; // number of the current "## N." section
let phase = null;   // current phase digit inside section 3
for (let i = 0; i < lines.length; i++) {
  const l = lines[i];
  const sec = l.match(/^## (\d+)\./);
  if (sec) { section = +sec[1]; phase = section === 4 ? 3 : null; continue; }
  const ph = l.match(/^### Phase (\d)[a-z]? —/);
  if (ph && section === 3) { phase = +ph[1]; continue; }
  if (section !== 3 && section !== 4) continue;
  const m = l.match(ROW_RE);
  if (!m) continue;
  if (phase === null) { findings.push(`line ${i + 1}: task row ${m[1]} outside any phase header`); continue; }
  const cells = l.split('|').map((c) => c.trim().replace(/\*\*/g, ''));
  // cells[0] is '' (leading pipe); cells[1] id; the hours cell is the first numeric cell at index >= 3
  let hIdx = -1;
  for (let k = 3; k < cells.length; k++) if (NUM_RE.test(cells[k])) { hIdx = k; break; }
  if (hIdx < 0) { findings.push(`line ${i + 1}: ${m[1]} has no parsable hours cell`); continue; }
  const hours = parseFloat(cells[hIdx]);
  const depsCell = cells[hIdx + 1] ?? '';
  const deps = depsCell.match(ID_RE) ?? [];
  if (tasks.has(m[1])) findings.push(`line ${i + 1}: duplicate task row ${m[1]} (first at line ${tasks.get(m[1]).line})`);
  tasks.set(m[1], { id: m[1], phase, hours, deps, line: i + 1 });
}

// 1. unknown deps, 2. backward edges
for (const t of tasks.values()) {
  for (const d of t.deps) {
    const dep = tasks.get(d);
    if (!dep) { findings.push(`${t.id} (line ${t.line}) depends on unknown id ${d}`); continue; }
    if (dep.phase > t.phase) findings.push(`backward edge: ${t.id} (Phase ${t.phase}) depends on ${d} (Phase ${dep.phase})`);
  }
}

// 3. cycles (iterative DFS with colours)
{
  const colour = new Map();
  const stack = [];
  const visit = (id) => {
    colour.set(id, 1); stack.push(id);
    for (const d of tasks.get(id).deps) {
      if (!tasks.has(d)) continue;
      const c = colour.get(d) ?? 0;
      if (c === 1) findings.push(`cycle: ${[...stack.slice(stack.indexOf(d)), d].join(' -> ')}`);
      else if (c === 0) visit(d);
    }
    stack.pop(); colour.set(id, 2);
  };
  for (const id of tasks.keys()) if ((colour.get(id) ?? 0) === 0) visit(id);
}

// 4. section-1 roll-up
const perPhase = {};
for (const t of tasks.values()) {
  perPhase[t.phase] ??= { tasks: 0, hours: 0 };
  perPhase[t.phase].tasks += 1; perPhase[t.phase].hours += t.hours;
}
let s1rows = 0;
for (const l of lines) {
  const m = l.match(/^\| ([0-7]) \| .* \| (\d+) \| ([\d.]+) \|$/);
  if (!m) continue;
  s1rows++;
  const p = +m[1], want = perPhase[p] ?? { tasks: 0, hours: 0 };
  if (+m[2] !== want.tasks || Math.abs(+m[3] - want.hours) > 0.001)
    findings.push(`section 1 Phase ${p} says ${m[2]} tasks / ${m[3]}h but the rows sum to ${want.tasks} / ${want.hours.toFixed(1)}h`);
}
if (s1rows !== 8) findings.push(`section 1 has ${s1rows} phase rows, expected 8`);

// 5. longest path vs section 7 total (only meaningful on an acyclic graph)
const hasCycle = findings.some((f) => f.startsWith('cycle:'));
const memo = new Map();
const prev = new Map();
const inProgress = new Set();
const longest = (id) => {
  if (memo.has(id)) return memo.get(id);
  if (inProgress.has(id)) return 0; // cycle already reported above; break the recursion
  inProgress.add(id);
  const t = tasks.get(id);
  let best = 0, bestDep = null;
  for (const d of t.deps) if (tasks.has(d)) { const ld = longest(d); if (ld > best) { best = ld; bestDep = d; } }
  inProgress.delete(id);
  prev.set(id, bestDep);
  const v = best + t.hours; memo.set(id, v); return v;
};
let crit = 0, critEnd = null;
if (!hasCycle) {
  for (const id of tasks.keys()) { const v = longest(id); if (v > crit) { crit = v; critEnd = id; } }
  if (wantPath && critEnd) {
    const chain = [];
    for (let id = critEnd; id; id = prev.get(id)) chain.unshift(id);
    let cum = 0;
    chain.forEach((id, i) => {
      const t = tasks.get(id); cum += t.hours;
      const deps = t.deps.length ? `${id} deps \`${t.deps.join(', ')}\`` : `${id} has no predecessor`;
      const last = i === chain.length - 1;
      console.log(`| ${i + 1} | ${id} | ${t.hours.toFixed(1)} | ${last ? `**${cum.toFixed(1)}**` : cum.toFixed(1)} | ${deps} |`);
    });
  }
  const totalMatch = text.match(/Total on the path: ([\d.]+)h/);
  if (!totalMatch) findings.push('section 7 has no "Total on the path: Xh" sentence');
  else if (Math.abs(parseFloat(totalMatch[1]) - crit) > 0.001)
    findings.push(`section 7 states ${totalMatch[1]}h on the critical path but the longest path over the tables is ${crit.toFixed(1)}h (ending at ${critEnd})`);
} else {
  findings.push('critical path not computed: the dependency graph has a cycle');
}

const totalHours = [...tasks.values()].reduce((s, t) => s + t.hours, 0);
console.log(`${tasks.size} ids / ${totalHours.toFixed(1)}h across ${Object.keys(perPhase).length} phases; longest path ${crit.toFixed(1)}h ending at ${critEnd}`);
if (findings.length) {
  console.log(`\n${findings.length} finding(s):`);
  for (const f of findings) console.log('  - ' + f);
  process.exit(1);
}
console.log('clean');
