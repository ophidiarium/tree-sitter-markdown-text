package tree_sitter_markdown_text

import (
	_ "embed"

	binding "github.com/ophidiarium/tree-sitter-markdown-text/bindings/go"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

//go:embed queries/highlights.scm
var HighlightsQuery string

//go:embed queries/injections.scm
var InjectionsQuery string

// GetLanguage returns the tree-sitter Language for Markdown.
func GetLanguage() *sitter.Language {
	return sitter.NewLanguage(binding.Language())
}

// GetHighlightsQuery compiles and returns the bundled highlights query.
func GetHighlightsQuery() (*sitter.Query, *sitter.QueryError) {
	return sitter.NewQuery(GetLanguage(), HighlightsQuery)
}

// GetInjectionsQuery compiles and returns the bundled injections query.
func GetInjectionsQuery() (*sitter.Query, *sitter.QueryError) {
	return sitter.NewQuery(GetLanguage(), InjectionsQuery)
}
