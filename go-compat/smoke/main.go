package main

import (
	"fmt"
	"os"
	"strings"

	sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_markdown_text "github.com/ophidiarium/tree-sitter-markdown-text/bindings/go"
)

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}

func main() {
	lang := sitter.NewLanguage(tree_sitter_markdown_text.Language())
	if lang == nil || lang.Inner == nil {
		fail("Language() returned nil")
	}

	fmt.Printf("grammar ABI version: %d\n", lang.Version())

	parser := sitter.NewParser()
	defer parser.Close()

	if err := parser.SetLanguage(lang); err != nil {
		fail("SetLanguage failed: %v", err)
	}

	sanityCheck(parser)
	metricsRegressionCheck(parser)

	fmt.Println("go consumer compatibility: ok")
}

func sanityCheck(parser *sitter.Parser) {
	source := []byte("# Title\n\nSome paragraph text.\n")

	tree := parser.Parse(source, nil)
	if tree == nil {
		fail("Parse returned nil tree")
	}
	defer tree.Close()

	root := tree.RootNode()
	if root.Kind() != "document" {
		fail("root kind = %q, want %q", root.Kind(), "document")
	}
	if root.HasError() {
		fail("root node has parse errors: %s", root.ToSexp())
	}
}

// metricsRegressionCheck asserts that the parse tree exposes the node kinds
// mehen's Markdown metric relies on. Each released grammar must keep producing
// them; breaking them regresses the real consumer.
func metricsRegressionCheck(parser *sitter.Parser) {
	source := []byte("# Heading\n\n" +
		"A paragraph with some text.\n\n" +
		"- item one\n- item two\n\n" +
		"```go\nfunc main() {}\n```\n\n" +
		"<!-- a block html comment -->\n")

	tree := parser.Parse(source, nil)
	if tree == nil {
		fail("metrics regression: Parse returned nil tree")
	}
	defer tree.Close()

	root := tree.RootNode()
	if root.HasError() {
		fail("metrics regression: parse errors in %q\n%s", string(source), root.ToSexp())
	}

	sexp := root.ToSexp()
	requireKinds := []string{
		"atx_heading",
		"paragraph",
		"list",
		"list_item",
		"fenced_code_block",
		"blank_line",
		"html_comment_block",
	}
	for _, kind := range requireKinds {
		if !strings.Contains(sexp, "("+kind) {
			fail("metrics regression: missing node kind %q in parse tree\n%s", kind, sexp)
		}
	}
}
