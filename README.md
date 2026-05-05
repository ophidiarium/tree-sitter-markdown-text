# tree-sitter-markdown-text

Markdown grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), shaped so that its AST lines up with the [textlint `TxtNode`](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) model.

Parses `.md` (and `.markdown`, `.mdown`, `.mkd`, `.mkdn`) files into a concrete syntax tree covering the full CommonMark block structure plus common extensions (GFM pipe tables, task lists, GFM alerts, YAML/TOML front matter, Pandoc math and directive blocks, footnotes, MDX JSX). Inline content is surfaced as structured children of the `inline` wrapper: classified tokens (`word_token`, `numeric_token`, `identifier_like_token`, `path_like_token`) and punctuation-class nodes (`terminator`, `separator`, `bracket`, `operator_like`), plus inline structural nodes (`emphasis`, `strong`, `strikethrough`, `link`, `image`, `autolink`, `inline_code`, `html_inline`, `math_inline`, `mdx_jsx_inline`, `footnote_reference`).

## Features

### Block nodes

- **Document structure** &mdash; `document`, nested `section` wrappers around ATX headings, `paragraph`, `blank_line` (as a first-class node).
- **Headings** &mdash; ATX (`#`..`######`) and setext (`===`/`---`) with the heading level exposed as a `level` field on both `atx_heading` and `setext_heading`.
- **Code blocks** &mdash; indented code blocks and fenced code blocks (backtick and tilde), with `info_string`/`language` children for the GFM language tag.
- **Math blocks** &mdash; Pandoc/GitLab/KaTeX display math (`$$…$$`) as a dedicated `math_block` with `math_block_delimiter`/`math_block_content` children.
- **Lists** &mdash; unordered (`+`/`-`/`*`) and ordered (`1.`/`1)`) list markers. GFM task list items are promoted to `task_list_item` (distinct from `list_item`), with `task_list_marker_checked`/`task_list_marker_unchecked` markers.
- **Block quotes and callouts** &mdash; nested quotes and lazy continuations. A block quote whose first paragraph begins with `[!NOTE]` / `[!TIP]` / `[!IMPORTANT]` / `[!WARNING]` / `[!CAUTION]` (or any uppercase-only label) is surfaced as `callout` with a `callout_type` field.
- **Thematic breaks** &mdash; `---`, `***`, `___`.
- **HTML blocks** &mdash; all 7 CommonMark HTML block types; block-level HTML comments are aliased to `html_comment_block` for easy metric extraction.
- **MDX JSX blocks** &mdash; shallow `mdx_jsx_block` for lines that start with an MDX-style JSX element (`<Component ...>`, `<Component/>`, `</Component>`). Component-style mixed-case names disambiguate from all-caps HTML blocks such as `<DIV>`.
- **Pipe tables** &mdash; `pipe_table` with `pipe_table_header`, `pipe_table_delimiter_row`, `pipe_table_row`, `pipe_table_cell`, `pipe_table_align_left`/`pipe_table_align_right`.
- **Link reference definitions** &mdash; `link_reference_definition` with `link_label`/`link_destination`/`link_title` children.
- **Footnote definitions** &mdash; `footnote_definition` (`[^id]: …`) with a `footnote_label` child.
- **Directive blocks** &mdash; generic container directives (`:::name … :::`, per remark-directive / MyST / Pandoc fenced divs) as `directive_block` with `directive_block_delimiter`/`directive_name`/`directive_block_content` children.
- **Image blocks** &mdash; a paragraph consisting of a single block-level image (`![alt](dest)` on its own line) is surfaced as `image_block` with `link_label`/`link_destination` children.
- **Front matter** &mdash; YAML (`---` fenced) as `minus_metadata`, TOML (`+++` fenced) as `plus_metadata`.

### Inline nodes (children of the `inline` wrapper)

- **Classified text tokens** &mdash; `text_span` wraps runs of classified tokens: `word_token` (Unicode alphabetic), `numeric_token` (integers, decimals, versions), `identifier_like_token` (camelCase / PascalCase / snake_case), `path_like_token` (paths with `/` separators or dotted identifiers).
- **Punctuation classes** &mdash; every punctuation lexeme is classified: `terminator` (`.`, `?`, `!`, `。`, `…`), `separator` (`,`, `;`, `:`), `bracket` (`(`, `)`, `[`, `]`, `{`, `}`, `<`, `>`), `operator_like` (`::`, `->`, `=>`, `=`, `+`, `-`, `*`, `/`, `|`, `&`, and other punctuation).
- **Emphasis / strong / strikethrough** &mdash; `emphasis` (`*…*` or `_…_`), `strong` (`**…**` or `__…__`), `strikethrough` (`~~…~~`), each with a `_delimiter`/`_content`/`_delimiter` sub-tree.
- **Code spans** &mdash; `inline_code` with matched backtick-run delimiters (1 or 2 backticks).
- **Links and images** &mdash; `link` (inline, full-reference, collapsed-reference, shortcut-reference forms) and `image` (`![alt](dest)` or `![alt][ref]`). Both expose `link_label`/`link_destination`/`link_title` children.
- **Autolinks** &mdash; `autolink` with `uri` or `email` children for `<https://…>` and `<user@example.com>`.
- **Raw HTML inline** &mdash; `html_inline` with `html_open_tag`/`html_close_tag`/`html_comment`/`html_cdata`/`html_declaration`/`html_processing_instruction` children.
- **MDX JSX inline** &mdash; shallow `mdx_jsx_inline` with `mdx_jsx_open_tag`/`mdx_jsx_close_tag`/`mdx_jsx_expression` children.
- **Inline math** &mdash; `math_inline` (`$…$`) with `math_inline_delimiter`/`math_inline_content` children. Disambiguated from `math_block` (`$$…$$`).
- **Footnote references** &mdash; `footnote_reference` (`[^id]` inside prose) with a `footnote_reference_label` child.

- **Injections query** &mdash; ships a `queries/injections.scm` that injects into fenced-code-block info strings, HTML blocks, and front matter.

## Example

```markdown
# Heading

A paragraph with inline content.

- one
- two

```go
func main() {}
```
```

Parsed tree (abbreviated):

```
(document
  (section
    (atx_heading level: (atx_h1_marker) heading_content: (inline))
    (blank_line)
    (paragraph (inline))
    (blank_line)
    (list
      (list_item (list_marker_minus) (paragraph (inline)))
      (list_item (list_marker_minus) (paragraph (inline))))
    (blank_line)
    (fenced_code_block
      (fenced_code_block_delimiter)
      (info_string (language))
      (code_fence_content)
      (fenced_code_block_delimiter))))
```

## Relationship to textlint

The grammar is structurally close to the textlint AST. Every block-level `TxtNode` type has a direct counterpart here; inline `TxtNode` types (`Str`, `Emphasis`, `Strong`, `Link`, `Image`, `Code`, `Html`, `Delete`, `FootnoteReference`) also have direct counterparts as children of the `inline` wrapper. Names stay snake_case per the tree-sitter convention; consumers map names themselves. See [docs/textlint-mapping.md](docs/textlint-mapping.md) for the full table.

## Installation

### npm

```sh
npm install tree-sitter-markdown-text
```

### Cargo

```sh
cargo add tree-sitter-markdown-text
```

### PyPI

```sh
pip install tree-sitter-markdown-text
```

### Go

```go
import tree_sitter_markdown_text "github.com/ophidiarium/tree-sitter-markdown-text/bindings/go"
```

The root package also exports the bundled queries via `go:embed`:

```go
import markdown "github.com/ophidiarium/tree-sitter-markdown-text"

lang := markdown.GetLanguage()
query, _ := markdown.GetHighlightsQuery()
```

## Usage

### Node.js

```javascript
import Parser from "tree-sitter";
import Markdown from "tree-sitter-markdown-text";

const parser = new Parser();
parser.setLanguage(Markdown);

const tree = parser.parse("# hello\n");
console.log(tree.rootNode.toString());
```

### Rust

```rust
let mut parser = tree_sitter::Parser::new();
let language = tree_sitter_markdown_text::LANGUAGE;
parser.set_language(&language.into()).unwrap();

let tree = parser.parse("# hello\n", None).unwrap();
println!("{}", tree.root_node().to_sexp());
```

### Python

```python
from tree_sitter import Language, Parser
import tree_sitter_markdown_text

parser = Parser(Language(tree_sitter_markdown_text.language()))
tree = parser.parse(b"# hello\n")
print(tree.root_node.sexp())
```

## Credits and references

- [tree-sitter-grammars/tree-sitter-markdown](https://github.com/tree-sitter-grammars/tree-sitter-markdown) &mdash; upstream grammar, specifically the `split_parser` branch's block grammar, which this grammar is derived from.
- [textlint TxtNode](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) &mdash; the AST shape this grammar targets for compatibility.
- [CommonMark Spec](https://spec.commonmark.org/) &mdash; the block structure this grammar implements.
- [Github Flavored Markdown](https://github.github.com/gfm/) &mdash; for the pipe-table and task-list extensions.

## License

[MIT](LICENSE)
