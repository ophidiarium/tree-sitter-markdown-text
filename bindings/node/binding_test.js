import assert from "node:assert";
import { test } from "node:test";
import Parser from "tree-sitter";

async function createParser() {
  const parser = new Parser();
  const { default: language } = await import("./index.js");
  parser.setLanguage(language);
  return parser;
}

function parseToString(parser, source) {
  return parser.parse(source).rootNode.toString();
}

function countOccurrences(text, pattern) {
  return (text.match(new RegExp(pattern, "g")) ?? []).length;
}

test("can load grammar", async () => {
  await createParser();
});

test("large indentation does not wrap into block starts", async () => {
  const parser = await createParser();
  const indentation = " ".repeat(259);

  for (const source of [`${indentation}# heading\n`, `${indentation}- item\n`]) {
    const tree = parseToString(parser, source);
    assert.match(tree, /\(indented_code_block/);
    assert.doesNotMatch(tree, /\(atx_heading|\(list/);
  }
});

test("long code fences require a closing fence of matching length", async () => {
  const parser = await createParser();
  const opener = "`".repeat(300);
  const shortCloser = "`".repeat(299);
  const tree = parseToString(parser, `${opener}\nbody\n${shortCloser}\nafter\n`);

  assert.match(tree, /\(fenced_code_block/);
  assert.equal(countOccurrences(tree, "fenced_code_block_delimiter"), 1);
});

test("huge ordered marker prefixes remain plain paragraphs", async () => {
  const parser = await createParser();
  const tree = parseToString(parser, `${"1".repeat(1000)}. item\n`);

  assert.match(tree, /\(paragraph/);
  assert.doesNotMatch(tree, /\(list|\(ERROR|\(MISSING/);
});
