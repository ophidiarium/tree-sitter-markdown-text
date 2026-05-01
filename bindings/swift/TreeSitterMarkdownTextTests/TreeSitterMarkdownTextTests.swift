import XCTest
import SwiftTreeSitter
import TreeSitterMarkdownText

final class TreeSitterMarkdownTextTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_markdown())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Markdown grammar")
    }
}
