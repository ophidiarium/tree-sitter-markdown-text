// swift-tools-version:5.3

import PackageDescription

let package = Package(
    name: "TreeSitterMarkdownText",
    products: [
        .library(name: "TreeSitterMarkdownText", targets: ["TreeSitterMarkdownText"]),
    ],
    dependencies: [
        .package(name: "SwiftTreeSitter", url: "https://github.com/tree-sitter/swift-tree-sitter", from: "0.25.0"),
    ],
    targets: [
        .target(
            name: "TreeSitterMarkdownText",
            dependencies: [],
            path: ".",
            exclude: [
                ".editorconfig",
                ".envrc",
                ".gitattributes",
                ".github",
                ".zed",
                "binding.gyp",
                "bindings/c",
                "bindings/go",
                "bindings/node",
                "bindings/python",
                "bindings/rust",
                "Cargo.lock",
                "Cargo.toml",
                "CMakeLists.txt",
                "eslint.config.mjs",
                "flake.lock",
                "flake.nix",
                "go-compat",
                "go.mod",
                "go.sum",
                "grammar.js",
                "html_entities.json",
                "LICENSE",
                "Makefile",
                "package-lock.json",
                "package.json",
                "pyproject.toml",
                "queries.go",
                "queries_test.go",
                "README.md",
                "setup.py",
                "src/grammar.json",
                "src/node-types.json",
                "test",
                "tree-sitter.json",
            ],
            sources: [
                "src/parser.c",
                "src/scanner.c",
            ],
            resources: [
                .copy("queries"),
            ],
            publicHeadersPath: "bindings/swift",
            cSettings: [.headerSearchPath("src")],
        ),
        .testTarget(
            name: "TreeSitterMarkdownTextTests",
            dependencies: [
                "SwiftTreeSitter",
                "TreeSitterMarkdownText",
            ],
            path: "bindings/swift/TreeSitterMarkdownTextTests",
        ),
    ],
    cLanguageStandard: .c11
)
