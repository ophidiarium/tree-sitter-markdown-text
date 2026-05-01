package tree_sitter_markdown_text_test

import (
	"testing"

	markdown "github.com/ophidiarium/tree-sitter-markdown-text"
)

func TestHighlightsQueryCompiles(t *testing.T) {
	if markdown.HighlightsQuery == "" {
		t.Fatal("HighlightsQuery is empty")
	}
	query, err := markdown.GetHighlightsQuery()
	if err != nil {
		t.Fatalf("highlights query failed to compile: %v", err)
	}
	defer query.Close()

	names := query.CaptureNames()
	if len(names) == 0 {
		t.Fatal("highlights query has no captures")
	}
}

func TestInjectionsQueryCompiles(t *testing.T) {
	if markdown.InjectionsQuery == "" {
		t.Fatal("InjectionsQuery is empty")
	}
	query, err := markdown.GetInjectionsQuery()
	if err != nil {
		t.Fatalf("injections query failed to compile: %v", err)
	}
	defer query.Close()
}
