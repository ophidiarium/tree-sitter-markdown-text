# tree-sitter-markdown-text

## Generating the parser

- **Always use `npm run generate`** — it pins `--abi 14`. Bare `tree-sitter generate` with CLI ≥0.26 defaults to ABI 15, which breaks `go-tree-sitter v0.24.0` consumers with `Incompatible language version 15. Expected minimum 13, maximum 14`.
- **Fresh-clone flow**: `npm install` fails on a fresh checkout because `node-gyp-build` can't find `src/parser.c`. Ignore that failure — the tree-sitter CLI is still installed. Run `npm run generate` next, which unblocks cargo/go/swift/node builds.
- The `go-compat` CI job fails any PR where `src/parser.c` `LANGUAGE_VERSION` != 14.
- Only the committed `src/parser.c` needs ABI 14. The npm/crates/pypi publish workflows regenerate at the CLI default (ABI 15); those runtimes accept newer ABIs.
- Always commit `src/parser.c`, `src/grammar.json`, `src/node-types.json`, `src/tree_sitter/parser.h`, and `src/tree_sitter/array.h` together. They must come from the same CLI invocation.

## Testing

- Corpus: `tree-sitter test` (optionally `-i <regex>` to filter).
- Fixture parse check: `tree-sitter parse test/fixtures/<file>.md` — exit non-zero on `ERROR` / `MISSING`.
- Reliable error count in scripts: `grep -cE '\(ERROR|\(MISSING' output.txt || true` (bare `grep -c` returns exit 1 on zero matches).
- Each corpus case needs content that doesn't depend on a trailing newline to close — add a `Trailing paragraph.` line after the node under test. Tree-sitter `test` does not append a newline to case sources.
- **Full local sweep** (mirrors CI): `npm run generate && tree-sitter test && npm run lint && npm run typecheck && clang-tidy src/scanner.c -- -std=c11 -Isrc && cargo test && CGO_ENABLED=1 go test ./... && bash go-compat/acceptance.sh`.

## Scanner (src/scanner.c)

- Block-only external scanner. Token set declared in the `TokenType` enum at the top; mirrors the `externals` array in `grammar.js`.
- **Prefer surgical edits over rewrites.** A wholesale refactor (rename `s`→`scanner`, factor helpers out of `parse_minus`/`parse_plus`) silently regressed task-list marker recognition with no clang-tidy/compile warning to pinpoint why. Apply one-category-at-a-time edits and run `tree-sitter test` after each batch.
- `.clang-tidy` is strict: `WarningsAsErrors: '*'` for `clang-analyzer-*,bugprone-*,cert-*,performance-*,portability-*,readability-*,modernize-*,misc-unused-parameters,misc-redundant-expression,misc-misleading-identifier`. CI fails any regression.
- Many parse_* functions are wrapped in `// NOLINTBEGIN(...) ... // NOLINTEND(...)` for `readability-function-cognitive-complexity` and `readability-identifier-length` — this is deliberate. The `s` parameter convention is repo-wide; splitting the state-machine functions obscures them. Do not remove the NOLINTs unless you're replacing the whole function.
- Named constants live in the enum at the top of the file (`TAB_STOP`, `MAX_NON_CODE_INDENT`, `ATX_HEADING_LEVELS`, `HTML_TAG_NAME_*`, …). Use them rather than bare integers when adding new code.
- When adding a new external token, also extend `paragraph_interrupt_symbols[]` at module scope — that table is indexed by `TokenType` and determines which tokens can interrupt a paragraph.

## Grammar enrichments on top of upstream

Three structural departures from upstream `tree-sitter-markdown`:

1. `atx_heading` and `setext_heading` expose the marker/underline as a `level` field so heading depth is directly queryable: `(atx_heading level: _ @lvl)`.
2. `blank_line` is a public node (upstream's `_blank_line`). Required for file-level metrics like BLANK LOC.
3. HTML block type 2 (`<!-- ... -->`) is aliased to `html_comment_block` inside `html_block` so a single query can target block-level comments for Markdown-CLOC.

If you extend the grammar further, preserve these three. Corpus assertions in `test/corpus/{headings,blank_lines,html_blocks}.txt` cover them.

## Versioning and release

- `tree-sitter.json` `metadata.version` tracks the **grammar** version. `package.json` / `Cargo.toml` / `Makefile` track package release versions and are **intentionally decoupled** — do not sync them unless asked.
- Pushing a `v*` tag fires three publish workflows: crates.io, npm, pypi. Each reads the version from `tree-sitter.json` at publish time.

## CI jobs

- `test` — `tree-sitter test` (corpus).
- `parse-fixtures` — iterates every `*.md` in `test/fixtures/` and requires **zero** `ERROR`/`MISSING` nodes per file. Drop a new `.md` into `test/fixtures/` to add it to this gate.
- `go-compat` — uses pinned `go-tree-sitter v0.24.0`; the canary that catches ABI bumps.
- `swift-compat` — runs `swift build` on ubuntu + macOS. **Local `swift build` fails on macOS with Command Line Tools only** (PackageDescription link error). Needs full Xcode. Don't chase it locally; the CI job is the gate.
- `lint` — ESLint on `grammar.js`; only runs when `grammar.js`, `eslint.config.mjs`, `tsconfig.json`, `package.json`, or `package-lock.json` change. Also runs `tsc --noEmit`.
- `clang-tidy` — static-analyzes `src/scanner.c`. Any new scanner edit must leave zero warnings.

## Grammar/style patterns

- `grammar.js` is an ES module (`"type": "module"` in package.json) and uses `import`/`export default grammar(...)`. Node's `fs`/`url` are imported as default namespace objects (`import nodeFs from 'node:fs'`) rather than destructured — `@stylistic` and `eslint-config-treesitter` set conflicting `object-curly-spacing` values, so `eslint --fix` oscillates between states. Side-step by not destructuring.
- HTML entity list lives in `html_entities.json` at repo root. Keep in sync with `https://html.spec.whatwg.org/multipage/entities.json` when bumping.
- `queries/injections.scm` does NOT include a `markdown_inline` injection — this grammar does not ship a second inline pass. Consumers that need emphasis/links/etc. should use a different grammar alongside.
