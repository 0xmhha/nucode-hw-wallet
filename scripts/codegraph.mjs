/**
 * 코드 의존성 그래프 — 계층 위반과 순환을 찾는다.
 *
 * 추측하지 않는다.
 *   TypeScript : 컴파일러 AST 를 걸어 import/export 를 뽑는다
 *   C          : `cc -MM` 로 전처리기가 실제로 여는 헤더를 받고,
 *                `nm` 으로 오브젝트의 정의/미정의 심볼을 받는다
 *
 *   node scripts/codegraph.mjs            계층 요약 + 위반
 *   node scripts/codegraph.mjs --dot      Graphviz DOT
 *   node scripts/codegraph.mjs --files    파일 단위 간선까지
 */
import { readFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join, relative, resolve, extname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ts = (await import(join(ROOT, 'sdk/node_modules/typescript/lib/typescript.js'))).default;

const args = new Set(process.argv.slice(2));

/* ── 계층 정의 ───────────────────────────────────────────────────────────
   위에서부터 순서대로 매칭한다. 아래 화살표는 "이 계층이 의존해도 되는 곳". */
const LAYERS = [
  { id: 'fw.core',    match: /^firmware\/wallet\/src\/app\//,            may: ['fw.core', 'fw.crypto'] },
  { id: 'fw.port',    match: /^firmware\/wallet\/src\/port\//,           may: ['fw.port', 'fw.core', 'fw.crypto'] },
  { id: 'fw.entry',   match: /^firmware\/wallet\/src\/main\.c$/,         may: ['fw.core', 'fw.port'] },
  { id: 'fw.crypto',  match: /^firmware\/nuwallet\/src\/(crypto|micro-ecc)\//, may: ['fw.crypto'] },
  { id: 'fw.chains',  match: /^firmware\/nuwallet\/src\/chains\//,       may: ['fw.chains', 'fw.crypto'] },
  { id: 'fw.arduino', match: /^firmware\/nuwallet\/(nuwallet\.ino|config\.h|src\/(controller|storage|transport|ui)\/)/,
                      may: ['fw.arduino', 'fw.crypto', 'fw.chains'] },
  { id: 'fw.test',    match: /^firmware\/test\//,                        may: ['*'] },
  { id: 'fw.demo',    match: /^firmware\/(src|arduino)\//,               may: ['*'] },

  { id: 'sdk.codec',  match: /^sdk\/src\/(protocol|rlp|address|wordlist|networks|types|web-bluetooth)\./, may: ['sdk.codec'] },
  { id: 'sdk.link',   match: /^sdk\/src\/(transport|browser)\./,         may: ['sdk.codec', 'sdk.link'] },
  { id: 'sdk.client', match: /^sdk\/src\/(client|pin|solana)\./,         may: ['sdk.codec', 'sdk.link', 'sdk.client'] },
  { id: 'sdk.dapp',   match: /^sdk\/src\/provider\./,                    may: ['sdk.codec', 'sdk.link', 'sdk.client', 'sdk.dapp'] },
  { id: 'sdk.barrel', match: /^sdk\/src\/index\./,                       may: ['*'] },
  { id: 'sdk.test',   match: /^sdk\/test\//,                             may: ['*'] },

  { id: 'web.shell',  match: /^app\/(layout|version|components)/,         may: ['sdk.barrel', 'web.shell'] },
  { id: 'web.setup',  match: /^app\/(page\.tsx|setup\/)/,                 may: ['sdk.barrel', 'web.shell'] },
  { id: 'web.dapp',   match: /^app\/dapp\//,                              may: ['sdk.barrel', 'web.shell'] },
  { id: 'web.debug',  match: /^app\/debug\//,                             may: ['sdk.barrel', 'web.shell'] },
];

const layerOf = (p) => LAYERS.find((l) => l.match.test(p))?.id ?? 'unknown';
const rules = Object.fromEntries(LAYERS.map((l) => [l.id, l.may]));

/* ── 파일 수집 ─────────────────────────────────────────────────────────── */
function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const name of readdirSync(dir)) {
    if (name === 'node_modules' || name === 'build' || name === 'dist' || name === '.git') continue;
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else out.push(relative(ROOT, p));
  }
  return out;
}
const ALL = [...walk(join(ROOT, 'app')), ...walk(join(ROOT, 'sdk/src')),
             ...walk(join(ROOT, 'sdk/test')), ...walk(join(ROOT, 'firmware'))]
  .filter((p) => !p.includes('micro-ecc'));

const TSF = ALL.filter((p) => /\.(ts|tsx|mts)$/.test(p) && !p.endsWith('.d.ts'));
const CF  = ALL.filter((p) => /\.(c|h|cpp|ino)$/.test(p));

/* ── TypeScript: 컴파일러 AST 로 import 추출 ───────────────────────────── */
const edges = [];        // { from, to, kind }
const exportsOf = {};    // file -> [심볼]

function resolveTs(fromFile, spec) {
  let base;
  if (spec.startsWith('@/')) base = join(ROOT, spec.slice(2));
  else if (spec.startsWith('.')) base = resolve(ROOT, dirname(fromFile), spec);
  else return null;                                  // 외부 패키지
  base = base.replace(/\.js$/, '');                  // ESM 확장자 표기
  for (const c of [base + '.ts', base + '.tsx', base, join(base, 'index.ts')]) {
    if (existsSync(c) && statSync(c).isFile()) return relative(ROOT, c);
  }
  return null;
}

for (const f of TSF) {
  const src = ts.createSourceFile(f, readFileSync(join(ROOT, f), 'utf8'),
                                  ts.ScriptTarget.Latest, true);
  const names = [];
  const visit = (n) => {
    if ((ts.isImportDeclaration(n) || ts.isExportDeclaration(n)) &&
        n.moduleSpecifier && ts.isStringLiteral(n.moduleSpecifier)) {
      const to = resolveTs(f, n.moduleSpecifier.text);
      if (to) edges.push({ from: f, to, kind: ts.isImportDeclaration(n) ? 'import' : 're-export' });
      else edges.push({ from: f, to: `pkg:${n.moduleSpecifier.text}`, kind: 'external' });
    }
    const mods = ts.canHaveModifiers(n) ? (ts.getModifiers(n) ?? []) : [];
    if (mods.some((m) => m.kind === ts.SyntaxKind.ExportKeyword)) {
      if (ts.isVariableStatement(n)) {
        for (const d of n.declarationList.declarations) names.push(d.name.getText(src));
      } else if (n.name) names.push(n.name.getText(src));
    }
    ts.forEachChild(n, visit);
  };
  visit(src);
  exportsOf[f] = names;
}

/* ── C: 컴파일러가 알려주는 인클루드 그래프 ────────────────────────────── */
const CC_INCLUDE = ['-I', join(ROOT, 'firmware/nuwallet/src')];
for (const f of CF) {
  if (/\.(cpp|ino)$/.test(f)) continue;              // Arduino 헤더 없이는 전처리 불가
  let out;
  try {
    out = execFileSync('cc', ['-MM', '-MG', ...CC_INCLUDE, join(ROOT, f)],
                       { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  } catch { continue; }
  for (const tok of out.replace(/\\\n/g, ' ').split(/\s+/).slice(1)) {
    if (!tok || tok.endsWith(':')) continue;
    const abs = resolve(ROOT, tok);
    if (!abs.startsWith(ROOT)) continue;
    const rel = relative(ROOT, abs);
    if (rel === f || rel.includes('micro-ecc')) continue;
    edges.push({ from: f, to: rel, kind: 'include' });
  }
}
/* .cpp/.ino 는 전처리가 안 되므로 #include 지시문만 읽는다 (근사치임을 표시) */
for (const f of CF.filter((p) => /\.(cpp|ino)$/.test(p))) {
  const src = readFileSync(join(ROOT, f), 'utf8');
  for (const m of src.matchAll(/^\s*#\s*include\s+"([^"]+)"/gm)) {
    const abs = resolve(ROOT, dirname(f), m[1]);
    if (!existsSync(abs)) continue;
    edges.push({ from: f, to: relative(ROOT, abs), kind: 'include~' });
  }
}

/* ── 집계 ──────────────────────────────────────────────────────────────── */
const layerEdges = new Map();
const violations = [];
const seen = new Set();

const isSource = (p) => /\.(ts|tsx|mts|c|h|cpp|ino)$/.test(p) && existsSync(join(ROOT, p));

for (const e of edges) {
  if (e.to.startsWith('pkg:') || !isSource(e.to)) continue;   // css·시스템 헤더 제외
  const a = layerOf(e.from), b = layerOf(e.to);
  if (a === b) continue;
  const key = `${a}->${b}`;
  layerEdges.set(key, (layerEdges.get(key) ?? 0) + 1);
  const allowed = rules[a] ?? [];
  if (!allowed.includes('*') && !allowed.includes(b)) {
    const vk = `${a}|${b}|${e.from}|${e.to}`;
    if (!seen.has(vk)) { seen.add(vk); violations.push({ ...e, a, b }); }
  }
}

/* 순환 (파일 단위) */
const adj = new Map();
for (const e of edges) {
  if (e.to.startsWith('pkg:')) continue;
  if (!adj.has(e.from)) adj.set(e.from, new Set());
  adj.get(e.from).add(e.to);
}
const cycles = [];
const color = new Map();
const stack = [];
function dfs(u) {
  color.set(u, 1); stack.push(u);
  for (const v of adj.get(u) ?? []) {
    if (color.get(v) === 1) cycles.push([...stack.slice(stack.indexOf(v)), v]);
    else if (!color.has(v)) dfs(v);
  }
  stack.pop(); color.set(u, 2);
}
for (const u of adj.keys()) if (!color.has(u)) dfs(u);

/* ── 출력 ──────────────────────────────────────────────────────────────── */
if (args.has('--dot')) {
  console.log('digraph codegraph {\n  rankdir=LR; node [shape=box, style=rounded];');
  const byLayer = {};
  for (const f of [...TSF, ...CF]) (byLayer[layerOf(f)] ??= []).push(f);
  for (const [l, fs] of Object.entries(byLayer)) {
    console.log(`  subgraph "cluster_${l}" {\n    label="${l}"; style=dashed;`);
    for (const f of fs) console.log(`    "${f}";`);
    console.log('  }');
  }
  for (const e of edges) if (!e.to.startsWith('pkg:')) console.log(`  "${e.from}" -> "${e.to}";`);
  console.log('}');
  process.exit(0);
}

const counts = {};
for (const f of [...TSF, ...CF]) counts[layerOf(f)] = (counts[layerOf(f)] ?? 0) + 1;

console.log('\n계층별 파일 수');
for (const l of LAYERS) if (counts[l.id]) console.log(`  ${l.id.padEnd(12)} ${String(counts[l.id]).padStart(3)}`);
if (counts.unknown) console.log(`  ${'unknown'.padEnd(12)} ${String(counts.unknown).padStart(3)}`);

console.log('\n계층 간 의존 (간선 수)');
for (const [k, n] of [...layerEdges].sort((x, y) => y[1] - x[1])) {
  const [a, b] = k.split('->');
  const bad = !(rules[a] ?? []).includes('*') && !(rules[a] ?? []).includes(b);
  console.log(`  ${bad ? '✗' : ' '} ${a.padEnd(12)} → ${b.padEnd(12)} ${String(n).padStart(3)}`);
}

if (args.has('--symbols')) {
  /* 파일 안의 책임 혼재는 import 그래프가 못 본다. 크기와 공개 심볼 수로 재는다. */
  console.log('\n파일 크기 · 공개 심볼');
  const rows = [];
  for (const f of [...TSF, ...CF]) {
    const loc = readFileSync(join(ROOT, f), 'utf8').split('\n').length;
    const ex = exportsOf[f]?.length ?? 0;
    rows.push({ f, l: layerOf(f), loc, ex });
  }
  rows.sort((a, b) => b.loc - a.loc);
  for (const r of rows.slice(0, 22)) {
    const flag = r.loc > 400 ? '⚠' : r.ex > 14 ? '·' : ' ';
    console.log(`  ${flag} ${String(r.loc).padStart(4)}줄  ${String(r.ex).padStart(2)}심볼  ${r.l.padEnd(11)} ${r.f}`);
  }
}

if (args.has('--files')) {
  console.log('\n파일 간선');
  for (const e of edges.filter((x) => !x.to.startsWith('pkg:'))) {
    console.log(`  ${e.from}  →  ${e.to}  [${e.kind}]`);
  }
}

console.log(`\n계층 위반 ${violations.length}건`);
for (const v of violations) console.log(`  ✗ ${v.a} → ${v.b}\n      ${v.from}\n      → ${v.to}`);

const uniq = [...new Set(cycles.map((c) => c.join(' → ')))];
console.log(`\n순환 ${uniq.length}건`);
for (const c of uniq) console.log(`  ↻ ${c}`);

process.exitCode = violations.length || uniq.length ? 1 : 0;
