# textlint TxtNode ↔ tree-sitter-markdown-text mapping

This grammar's node types are close to — but not byte-identical with — the textlint [TxtNode](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) AST. The mapping below uses textlint's official remark/mdast → TxtNode table from [`markdown-syntax-map.ts`](https://github.com/textlint/textlint/blob/master/packages/%40textlint/markdown-to-ast/src/mapping/markdown-syntax-map.ts) as the source of truth.

## Block-level nodes (covered by this grammar)

| textlint TxtNode | mdast key | `tree-sitter-markdown-text` node | Notes |
|---|---|---|---|
| `Document` | `root` | `document` | Root node. |
| `Paragraph` | `paragraph` | `paragraph` | Children are a single opaque `inline`. |
| `BlockQuote` | `blockquote` | `block_quote` | Nested quotes supported. |
| `List` | `list` | `list` | Ordered vs unordered distinguishable via the `list_marker_{dot,minus,plus,star,parenthesis}` child of each `list_item`. |
| `ListItem` | `listItem` | `list_item` | Task list items carry `task_list_marker_checked` / `task_list_marker_unchecked`. |
| `Header` (+ `depth`) | `heading` | `atx_heading` / `setext_heading` | Both expose the marker/underline via a `level` field (`atx_h1_marker`..`atx_h6_marker`, `setext_h1_underline`/`setext_h2_underline`). |
| `CodeBlock` (+ `lang`) | `code` | `fenced_code_block` / `indented_code_block` | Language from `fenced_code_block → info_string → language`. |
| `HtmlBlock` | `HtmlBlock` | `html_block` | HTML comment blocks are additionally aliased to `html_comment_block`. |
| `HorizontalRule` | `thematicBreak` | `thematic_break` | |
| `Yaml` | `yaml` | `minus_metadata` | `---` fenced front matter. |
| `Toml` | `toml` | `plus_metadata` | `+++` fenced front matter. |
| `Table` | `table` | `pipe_table` | GFM pipe tables only. |
| `TableRow` | `tableRow` | `pipe_table_row` / `pipe_table_header` | Header is the first row; this grammar surfaces it as a separate `pipe_table_header` node. |
| `TableCell` | `tableCell` | `pipe_table_cell` | |
| `Definition` / `ReferenceDef` | `definition` | `link_reference_definition` | With `link_label` / `link_destination` / `link_title` children. |

## Textlint nodes this grammar does NOT surface

- `Json` (`json` front matter) — uncommon; not handled by the external scanner.
- All inline nodes: `Str` (`text`), `Break`, `Emphasis`, `Strong`, `Html`, `Link`, `Image`, `Code` (`inlineCode`), `Delete`, `LinkReference`, `ImageReference`, `FootnoteReference`. Paragraph and heading content is emitted as a single opaque `inline` leaf; a separate inline grammar would be required to structure it.

## Extra nodes (no textlint counterpart)

- `section` — wrapper that groups an ATX heading with the blocks that follow it up to the next heading of equal/higher rank. Useful for document outline queries; harmless to ignore.
- `blank_line` — empty line, exposed publicly so file-level metrics can count BLANK LOC directly.
- `block_continuation` — zero-width marker used by the external scanner when a container block continues on a new line. Ignore for AST purposes.
- `html_comment_block` — alias around CommonMark HTML block type 2 inside `html_block`. Handy for Markdown-CLOC metrics.
