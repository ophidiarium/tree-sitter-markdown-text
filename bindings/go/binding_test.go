package tree_sitter_markdown_text_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_markdown_text "github.com/ophidiarium/tree-sitter-markdown-text/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_markdown_text.Language())
	if language == nil {
		t.Errorf("Error loading Markdown grammar")
	}
}
