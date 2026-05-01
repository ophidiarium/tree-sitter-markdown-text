# tree-sitter-markdown-text

Markdown grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), shaped so that its AST lines up with the [textlint `TxtNode`](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) model.

Parses `.md` (and `.markdown`, `.mdown`, `.mkd`, `.mkdn`) files into a concrete syntax tree covering the full CommonMark block structure plus common extensions (GFM pipe tables, task lists, YAML/TOML front matter). Inline content (emphasis, links, code spans, …) is surfaced as opaque `inline` leaves — this grammar is intentionally block-only.

## Features

- **Document structure** &mdash; `document`, nested `section` wrappers around ATX headings, `paragraph`, `blank_line` (as a first-class node).
- **Headings** &mdash; ATX (`#`..`######`) and setext (`===`/`---`) with the heading level exposed as a `level` field on both `atx_heading` and `setext_heading`.
- **Code blocks** &mdash; indented code blocks and fenced code blocks (backtick and tilde), with `info_string`/`language` children for the GFM language tag.
- **Lists** &mdash; unordered (`+`/`-`/`*`) and ordered (`1.`/`1)`) list markers, GFM task list items (`[x]`/`[ ]`) as dedicated `task_list_marker_checked`/`task_list_marker_unchecked` nodes.
- **Block quotes** &mdash; including nested quotes and lazy continuations.
- **Thematic breaks** &mdash; `---`, `***`, `___`.
- **HTML blocks** &mdash; all 7 CommonMark HTML block types; block-level HTML comments are aliased to `html_comment_block` for easy metric extraction.
- **Pipe tables** &mdash; `pipe_table` with `pipe_table_header`, `pipe_table_delimiter_row`, `pipe_table_row`, `pipe_table_cell`, `pipe_table_align_left`/`pipe_table_align_right`.
- **Link reference definitions** &mdash; `link_reference_definition` with `link_label`/`link_destination`/`link_title` children.
- **Front matter** &mdash; YAML (`---` fenced) as `minus_metadata`, TOML (`+++` fenced) as `plus_metadata`.
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

The grammar is structurally close to the textlint block AST — every block-level `TxtNode` type (`Document`, `Paragraph`, `BlockQuote`, `List`, `ListItem`, `Header`, `CodeBlock`, `HtmlBlock`, `HorizontalRule`, `Table`/`TableRow`/`TableCell`, `Definition`, `Yaml`, `Toml`) has a direct counterpart here. Names stay snake_case per the tree-sitter convention; consumers map names themselves. See [docs/textlint-mapping.md](docs/textlint-mapping.md) for the full table.

Inline textlint nodes (`Str`, `Emphasis`, `Strong`, `Link`, `Image`, `Code`, `Break`, `Html`, `Delete`, `LinkReference`, `ImageReference`, `FootnoteReference`) are intentionally out of scope: this grammar stops at `inline` leaves.

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
