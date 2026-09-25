# njin

## Scope: 2D top-down and platformer games only

njin exists to make two kinds of game: **top-down** (action, adventure, RPG,
twin-stick shooter) and **side-view platformer**. Judge every engine change by
whether one of those two needs it.

- Build for them: tilemaps, sprite animation, AABB collision with tile maps and
  entities (`collision_move` for both gravity and 8-way movement), cameras that
  follow and clamp to the level, Tiled/LDtk levels, particles, game-feel FX,
  menus, save games.
- Out of scope unless the user asks: 3D, isometric or hexagonal maps, rigid-body
  physics simulation (Box2D and the like), networking, general-purpose editor
  tooling. Do not propose these as next steps.
- When a feature has a top-down and a platformer flavour (movement, camera,
  collision response), make sure it serves both, and test it in both shapes.
- Level editors (Tiled, LDtk) follow the same scope: square-grid maps only.
  Non-orthogonal Tiled maps are refused, not approximated. Editor features
  worth supporting are the ones these games use: tile layers, IntGrid,
  auto-layers, objects/entities with properties, spawn points, triggers,
  collision layers. Do not add isometric, staggered or hexagonal support.
- The raylib/EnTT stack and the public API conventions stay as they are; this
  is about what to build, not how.

## Token cost: use the graphify graph for orientation

A knowledge graph of the repo lives in `graphify-out/` (local, git-ignored).
It costs far less to ask it than to search: `graphify query` returns a scoped
subgraph within a ~2000-token budget, where a grep sweep or an Explore agent
reads whole files.

Use it to **find out where things are**:
- Before Grep, Glob, an Explore agent or reading several files to answer "where
  is X handled", "what calls Y", "how do A and B connect", "which files does
  feature Z touch": run `graphify query "<question>"`, or
  `graphify path "<A>" "<B>"` / `graphify explain "<concept>"`. Then read only
  the files and lines the answer names (`src=... loc=L123`).
- Query in the terms the code uses (`key_consume`, `collision_move`), not in
  prose; a vague question returns a noisy, truncated subgraph. If it says
  TRUNCATED, narrow the question before raising `--budget`.
- Tell any subagent that explores code to start with `graphify query`; hooks
  and this file do not reach it on their own.

Do **not** use it when you already know where to look: editing a known
function, fixing a compile error at a given line, following a stack trace.
Read that file directly. A query there only adds tokens.

Never read `GRAPH_REPORT.md` (30 KB), `graph.html` or `graph.json` (MBs) whole.
Use them only for a broad architecture review, and then only the sections you
need. The graph is a map of structure: confirm a symbol with a Read before
relying on it, and if a result names something that is not on disk the graph
is stale.

Keeping it fresh, cheapest first:
- **Code changes:** run `graphify update .` (AST only, no LLM, about 5 s, no
  token cost) once per batch of edits, before a query about code you changed
  and before committing. Not after every edit. It rebuilds the clusters, so
  hand-picked community names are lost; that is fine.
- **Doc changes** (`docs/pages/*.md`, `CLAUDE.md`): these need `/graphify
  --update`, which spends LLM tokens (30 changed docs cost about 250k
  subagent tokens). Run it only when the user asks for the docs in the graph,
  once for the whole batch. Never after each page. Never a full rebuild and
  never `--mode deep`.
- The AST extractor fails on `src/engine/api/njin_log.h` (syntax error at line
  79), so the graph has no symbols from it: grep that file instead.

## Line endings: LF only

Every text file in this repo uses LF (`\n`). Never write CRLF (`\r\n`, shows as `^M`).
`.gitattributes` and `.editorconfig` already enforce this for git and editors;
these rules cover scripted edits, which bypass both.

- Prefer the Edit/Write tools for file changes.
- Python on Windows writes CRLF in text mode. Always pass `newline='\n'`:
  `open(path, 'w', newline='\n')`. Same for `Path.write_text(..., newline='\n')`.
- Do not use PowerShell `Set-Content`/`Out-File` to write source files.
- After any scripted edit, check that no CR slipped in:
  `grep -rlI $'\r' src CMakeLists.txt` must print nothing. Fix with
  `sed -i 's/\r$//' <file>`.
