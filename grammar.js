// Markdown block-structure grammar for tree-sitter.
//
// Derived from https://github.com/tree-sitter-grammars/tree-sitter-markdown
// (the `split_parser` branch, `tree-sitter-markdown` block grammar + `common/common.js`).
//
// Only concerns block structure per the CommonMark Spec
// (https://spec.commonmark.org/0.30/#blocks-and-inlines). Inline content
// (emphasis, links, code spans) is exposed as an opaque `inline` leaf.
//
// Extensions always enabled: YAML front matter, TOML front matter, pipe tables,
// GFM task lists. Other upstream extensions (tags, wiki links, LaTeX, strikethrough)
// are inline-only or niche and are intentionally excluded.

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

import nodeFs from 'node:fs';
import nodeUrl from 'node:url';

const PUNCTUATION_CHARACTERS_REGEX = '!-/:-@\\[-`\\{-~';
const PUNCTUATION_CHARACTERS_ARRAY = [
  '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<',
  '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~',
];
const PRECEDENCE_LEVEL_LINK = 10;

export default grammar({
  name: 'markdown',

  rules: {
    document: ($) => seq(
      optional(choice($.minus_metadata, $.plus_metadata)),
      alias(prec.right(repeat($._block_not_section)), $.section),
      repeat($.section),
    ),

    // Common building blocks formerly in common/common.js.

    backslash_escape: ($) => $._backslash_escape,
    _backslash_escape: ($) => new RegExp('\\\\[' + PUNCTUATION_CHARACTERS_REGEX + ']'),

    entity_reference: ($) => html_entity_regex(),
    numeric_character_reference: ($) => /&#([0-9]{1,7}|[xX][0-9a-fA-F]{1,6});/,

    link_label: ($) => seq('[', repeat1(choice(
      $._text_inline_no_link,
      $.backslash_escape,
      $.entity_reference,
      $.numeric_character_reference,
      $._soft_line_break,
    )), ']'),

    link_destination: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, choice(
      seq('<', repeat(choice($._text_no_angle, $.backslash_escape, $.entity_reference, $.numeric_character_reference)), '>'),
      seq(
        choice(
          $._word,
          punctuation_without($, ['<', '(', ')']),
          $.backslash_escape,
          $.entity_reference,
          $.numeric_character_reference,
          $._link_destination_parenthesis,
        ),
        repeat(choice(
          $._word,
          punctuation_without($, ['(', ')']),
          $.backslash_escape,
          $.entity_reference,
          $.numeric_character_reference,
          $._link_destination_parenthesis,
        )),
      ),
    )),
    _link_destination_parenthesis: ($) => seq('(', repeat(choice(
      $._word,
      punctuation_without($, ['(', ')']),
      $.backslash_escape,
      $.entity_reference,
      $.numeric_character_reference,
      $._link_destination_parenthesis,
    )), ')'),
    _text_no_angle: ($) => choice($._word, punctuation_without($, ['<', '>']), $._whitespace),
    link_title: ($) => choice(
      seq('"', repeat(choice(
        $._word,
        punctuation_without($, ['"']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), '"'),
      seq('\'', repeat(choice(
        $._word,
        punctuation_without($, ['\'']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), '\''),
      seq('(', repeat(choice(
        $._word,
        punctuation_without($, ['(', ')']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), ')'),
    ),

    _newline_token: ($) => /\n|\r\n?/,

    // Needed for compatibility with upstream common rules that referenced it
    // from the inline grammar.
    _last_token_punctuation: ($) => choice(),

    // BLOCK STRUCTURE

    // All blocks. Every block contains a trailing newline.
    _block: ($) => choice(
      $._block_not_section,
      $.section,
    ),
    _block_not_section: ($) => choice(
      alias($._setext_heading1, $.setext_heading),
      alias($._setext_heading2, $.setext_heading),
      $.paragraph,
      $.indented_code_block,
      $.block_quote,
      $.thematic_break,
      $.list,
      $.fenced_code_block,
      $.blank_line,
      $.html_block,
      $.link_reference_definition,
      $.pipe_table,
    ),
    section: ($) => choice($._section1, $._section2, $._section3, $._section4, $._section5, $._section6),
    _section1: ($) => prec.right(seq(
      alias($._atx_heading1, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4, $._section3, $._section2), $.section),
        $._block_not_section,
      )),
    )),
    _section2: ($) => prec.right(seq(
      alias($._atx_heading2, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4, $._section3), $.section),
        $._block_not_section,
      )),
    )),
    _section3: ($) => prec.right(seq(
      alias($._atx_heading3, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4), $.section),
        $._block_not_section,
      )),
    )),
    _section4: ($) => prec.right(seq(
      alias($._atx_heading4, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5), $.section),
        $._block_not_section,
      )),
    )),
    _section5: ($) => prec.right(seq(
      alias($._atx_heading5, $.atx_heading),
      repeat(choice(
        alias($._section6, $.section),
        $._block_not_section,
      )),
    )),
    _section6: ($) => prec.right(seq(
      alias($._atx_heading6, $.atx_heading),
      repeat($._block_not_section),
    )),

    // LEAF BLOCKS

    // A thematic break. This is currently handled by the external scanner but maybe could be
    // parsed using normal tree-sitter rules.
    //
    // https://github.github.com/gfm/#thematic-breaks
    thematic_break: ($) => seq($._thematic_break, choice($._newline, $._eof)),

    // An ATX heading. The heading level (h1..h6) is exposed as a `level` field
    // that binds the `atx_h{N}_marker` child. A textlint-shaped consumer can
    // read it directly via `(atx_heading level: _ @lvl)`.
    //
    // https://github.github.com/gfm/#atx-headings
    _atx_heading1: ($) => prec(1, seq(
      field('level', $.atx_h1_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading2: ($) => prec(1, seq(
      field('level', $.atx_h2_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading3: ($) => prec(1, seq(
      field('level', $.atx_h3_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading4: ($) => prec(1, seq(
      field('level', $.atx_h4_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading5: ($) => prec(1, seq(
      field('level', $.atx_h5_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading6: ($) => prec(1, seq(
      field('level', $.atx_h6_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading_content: ($) => prec(1, seq(
      optional($._whitespace),
      field('heading_content', alias($._line, $.inline)),
    )),

    // A setext heading. The heading level is the underline kind, exposed as a
    // `level` field for parity with ATX headings.
    //
    // https://github.github.com/gfm/#setext-headings
    _setext_heading1: ($) => seq(
      field('heading_content', $.paragraph),
      field('level', $.setext_h1_underline),
      choice($._newline, $._eof),
    ),
    _setext_heading2: ($) => seq(
      field('heading_content', $.paragraph),
      field('level', $.setext_h2_underline),
      choice($._newline, $._eof),
    ),

    // An indented code block. An indented code block is made up of indented chunks and blank
    // lines. The indented chunks are handled by the external scanner.
    //
    // https://github.github.com/gfm/#indented-code-blocks
    indented_code_block: ($) => prec.right(seq($._indented_chunk, repeat(choice($._indented_chunk, $.blank_line)))),
    _indented_chunk: ($) => seq($._indented_chunk_start, repeat(choice($._line, $._newline)), $._block_close, optional($.block_continuation)),

    // A fenced code block. Fenced code blocks are mainly handled by the external scanner. In
    // case of backtick code blocks the external scanner also checks that the info string is
    // proper.
    //
    // https://github.github.com/gfm/#fenced-code-blocks
    fenced_code_block: ($) => prec.right(choice(
      seq(
        alias($._fenced_code_block_start_backtick, $.fenced_code_block_delimiter),
        optional($._whitespace),
        optional($.info_string),
        $._newline,
        optional($.code_fence_content),
        optional(seq(alias($._fenced_code_block_end_backtick, $.fenced_code_block_delimiter), $._close_block, $._newline)),
        $._block_close,
      ),
      seq(
        alias($._fenced_code_block_start_tilde, $.fenced_code_block_delimiter),
        optional($._whitespace),
        optional($.info_string),
        $._newline,
        optional($.code_fence_content),
        optional(seq(alias($._fenced_code_block_end_tilde, $.fenced_code_block_delimiter), $._close_block, $._newline)),
        $._block_close,
      ),
    )),
    code_fence_content: ($) => repeat1(choice($._newline, $._line)),
    info_string: ($) => choice(
      seq($.language, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
      seq(
        repeat1(choice('{', '}')),
        optional(choice(
          seq($.language, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
          seq($._whitespace, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
        )),
      ),
    ),
    language: ($) => prec.right(repeat1(choice($._word, punctuation_without($, ['{', '}', ',']), $.backslash_escape, $.entity_reference, $.numeric_character_reference))),

    // An HTML block. Type 2 (`<!-- ... -->`) is aliased to `html_comment_block`
    // so consumers computing markdown-CLOC can target comments directly. Other
    // types remain as `html_block`; injections/HTML parsers should do the rest.
    //
    // https://github.github.com/gfm/#html-blocks
    html_block: ($) => prec(1, seq(optional($._whitespace), choice(
      $._html_block_1,
      alias($._html_block_2, $.html_comment_block),
      $._html_block_3,
      $._html_block_4,
      $._html_block_5,
      $._html_block_6,
      $._html_block_7,
    ))),
    _html_block_1: ($) => build_html_block($, $._html_block_1_start, $._html_block_1_end),
    _html_block_2: ($) => build_html_block($, $._html_block_2_start, '-->'),
    _html_block_3: ($) => build_html_block($, $._html_block_3_start, '?>'),
    _html_block_4: ($) => build_html_block($, $._html_block_4_start, '>'),
    _html_block_5: ($) => build_html_block($, $._html_block_5_start, ']]>'),
    _html_block_6: ($) => build_html_block($, $._html_block_6_start, seq($._newline, $.blank_line)),
    _html_block_7: ($) => build_html_block($, $._html_block_7_start, seq($._newline, $.blank_line)),

    // A link reference definition.
    //
    // https://github.github.com/gfm/#link-reference-definitions
    link_reference_definition: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, seq(
      optional($._whitespace),
      $.link_label,
      ':',
      optional(seq(optional($._whitespace), optional(seq($._soft_line_break, optional($._whitespace))))),
      $.link_destination,
      optional(prec.dynamic(2 * PRECEDENCE_LEVEL_LINK, seq(
        choice(
          seq($._whitespace, optional(seq($._soft_line_break, optional($._whitespace)))),
          seq($._soft_line_break, optional($._whitespace)),
        ),
        optional($._no_indented_chunk),
        $.link_title,
      ))),
      choice($._newline, $._soft_line_break, $._eof),
    )),
    _text_inline_no_link: ($) => choice($._word, $._whitespace, punctuation_without($, ['[', ']'])),

    // A paragraph.
    //
    // https://github.github.com/gfm/#paragraphs
    paragraph: ($) => seq(alias(repeat1(choice($._line, $._soft_line_break)), $.inline), choice($._newline, $._eof)),

    // A blank line including the following newline. Publicly named so metrics
    // like mehen's BLANK can target it directly.
    //
    // https://github.github.com/gfm/#blank-lines
    blank_line: ($) => seq($._blank_line_start, choice($._newline, $._eof)),

    // CONTAINER BLOCKS

    block_quote: ($) => seq(
      alias($._block_quote_start, $.block_quote_marker),
      optional($.block_continuation),
      repeat($._block),
      $._block_close,
      optional($.block_continuation),
    ),

    list: ($) => prec.right(choice(
      $._list_plus,
      $._list_minus,
      $._list_star,
      $._list_dot,
      $._list_parenthesis,
    )),
    _list_plus: ($) => prec.right(repeat1(alias($._list_item_plus, $.list_item))),
    _list_minus: ($) => prec.right(repeat1(alias($._list_item_minus, $.list_item))),
    _list_star: ($) => prec.right(repeat1(alias($._list_item_star, $.list_item))),
    _list_dot: ($) => prec.right(repeat1(alias($._list_item_dot, $.list_item))),
    _list_parenthesis: ($) => prec.right(repeat1(alias($._list_item_parenthesis, $.list_item))),
    list_marker_plus: ($) => choice($._list_marker_plus, $._list_marker_plus_dont_interrupt),
    list_marker_minus: ($) => choice($._list_marker_minus, $._list_marker_minus_dont_interrupt),
    list_marker_star: ($) => choice($._list_marker_star, $._list_marker_star_dont_interrupt),
    list_marker_dot: ($) => choice($._list_marker_dot, $._list_marker_dot_dont_interrupt),
    list_marker_parenthesis: ($) => choice($._list_marker_parenthesis, $._list_marker_parenthesis_dont_interrupt),
    _list_item_plus: ($) => seq(
      $.list_marker_plus,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_minus: ($) => seq(
      $.list_marker_minus,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_star: ($) => seq(
      $.list_marker_star,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_dot: ($) => seq(
      $.list_marker_dot,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_parenthesis: ($) => seq(
      $.list_marker_parenthesis,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_content: ($) => choice(
      prec(1, seq(
        $.blank_line,
        $.blank_line,
        $._close_block,
        optional($.block_continuation),
      )),
      repeat1($._block),
      prec(1, seq(
        choice($.task_list_marker_checked, $.task_list_marker_unchecked),
        $._whitespace,
        $.paragraph,
        repeat($._block),
      )),
    ),

    _newline: ($) => seq(
      $._line_ending,
      optional($.block_continuation),
    ),
    _soft_line_break: ($) => seq(
      $._soft_line_ending,
      optional($.block_continuation),
    ),
    _line: ($) => prec.right(repeat1(choice($._word, $._whitespace, punctuation_without($, [])))),
    _word: ($) => choice(
      new RegExp('[^' + PUNCTUATION_CHARACTERS_REGEX + ' \\t\\n\\r]+'),
      /\[[xX]\]/,
      /\[[ \t]\]/,
    ),
    _whitespace: ($) => /[ \t]+/,

    task_list_marker_checked: ($) => prec(1, /\[[xX]\]/),
    task_list_marker_unchecked: ($) => prec(1, /\[[ \t]\]/),

    pipe_table: ($) => prec.right(seq(
      $._pipe_table_start,
      alias($.pipe_table_row, $.pipe_table_header),
      $._newline,
      $.pipe_table_delimiter_row,
      repeat(seq($._pipe_table_newline, optional($.pipe_table_row))),
      choice($._newline, $._eof),
    )),

    _pipe_table_newline: ($) => seq(
      $._pipe_table_line_ending,
      optional($.block_continuation),
    ),

    pipe_table_delimiter_row: ($) => seq(
      optional(seq(
        optional($._whitespace),
        '|',
      )),
      repeat1(prec.right(seq(
        optional($._whitespace),
        $.pipe_table_delimiter_cell,
        optional($._whitespace),
        '|',
      ))),
      optional($._whitespace),
      optional(seq(
        $.pipe_table_delimiter_cell,
        optional($._whitespace),
      )),
    ),

    pipe_table_delimiter_cell: ($) => seq(
      optional(alias(':', $.pipe_table_align_left)),
      repeat1('-'),
      optional(alias(':', $.pipe_table_align_right)),
    ),

    pipe_table_row: ($) => seq(
      optional(seq(
        optional($._whitespace),
        '|',
      )),
      choice(
        seq(
          repeat1(prec.right(seq(
            choice(
              seq(
                optional($._whitespace),
                $.pipe_table_cell,
                optional($._whitespace),
              ),
              alias($._whitespace, $.pipe_table_cell),
            ),
            '|',
          ))),
          optional($._whitespace),
          optional(seq(
            $.pipe_table_cell,
            optional($._whitespace),
          )),
        ),
        seq(
          optional($._whitespace),
          $.pipe_table_cell,
          optional($._whitespace),
        ),
      ),
    ),

    pipe_table_cell: ($) => prec.right(seq(
      choice(
        $._word,
        $._backslash_escape,
        punctuation_without($, ['|']),
      ),
      repeat(choice(
        $._word,
        $._whitespace,
        $._backslash_escape,
        punctuation_without($, ['|']),
      )),
    )),
  },

  externals: ($) => [
    $._line_ending,
    $._soft_line_ending,
    $._block_close,
    $.block_continuation,

    $._block_quote_start,
    $._indented_chunk_start,
    $.atx_h1_marker,
    $.atx_h2_marker,
    $.atx_h3_marker,
    $.atx_h4_marker,
    $.atx_h5_marker,
    $.atx_h6_marker,
    $.setext_h1_underline,
    $.setext_h2_underline,
    $._thematic_break,
    $._list_marker_minus,
    $._list_marker_plus,
    $._list_marker_star,
    $._list_marker_parenthesis,
    $._list_marker_dot,
    $._list_marker_minus_dont_interrupt,
    $._list_marker_plus_dont_interrupt,
    $._list_marker_star_dont_interrupt,
    $._list_marker_parenthesis_dont_interrupt,
    $._list_marker_dot_dont_interrupt,
    $._fenced_code_block_start_backtick,
    $._fenced_code_block_start_tilde,
    $._blank_line_start,

    $._fenced_code_block_end_backtick,
    $._fenced_code_block_end_tilde,

    $._html_block_1_start,
    $._html_block_1_end,
    $._html_block_2_start,
    $._html_block_3_start,
    $._html_block_4_start,
    $._html_block_5_start,
    $._html_block_6_start,
    $._html_block_7_start,

    $._close_block,
    $._no_indented_chunk,

    $._error,
    $._trigger_error,
    $._eof,

    $.minus_metadata,
    $.plus_metadata,

    $._pipe_table_start,
    $._pipe_table_line_ending,
  ],
  precedences: ($) => [
    [$._setext_heading1, $._block],
    [$._setext_heading2, $._block],
    [$.indented_code_block, $._block],
  ],
  conflicts: ($) => [
    [$.link_reference_definition],
    [$.link_label, $._line],
    [$.link_reference_definition, $._line],
  ],
  extras: ($) => [],
});

// Returns a rule that matches all characters that count as punctuation inside
// markdown, besides a list of excluded punctuation characters. Calling this
// function with an empty list as the second argument returns a rule that
// matches all punctuation.
/**
 * @param {GrammarSymbols<string>} $
 * @param {string[]} chars
 */
function punctuation_without($, chars) {
  return seq(choice(...PUNCTUATION_CHARACTERS_ARRAY.filter((c) => !chars.includes(c))), optional($._last_token_punctuation));
}

// Constructs a regex that matches all html entity references. Source list is
// html_entities.json at repo root (kept in sync with
// https://html.spec.whatwg.org/multipage/entities.json).
/**
 * @returns {RegExp}
 */
function html_entity_regex() {
  const entitiesPath = nodeUrl.fileURLToPath(new URL('./html_entities.json', import.meta.url));
  const html_entities = JSON.parse(nodeFs.readFileSync(entitiesPath, 'utf8'));
  let s = '&(';
  s += Object.keys(html_entities).map((name) => name.substring(1, name.length - 1)).join('|');
  s += ');';
  return new RegExp(s);
}

// General-purpose structure for html blocks. Different kinds have different
// opening and closing conditions; the scanner takes care of the distinction
// via the start/end tokens.
/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} open
 * @param {RuleOrLiteral} close
 */
function build_html_block($, open, close) {
  return seq(
    open,
    repeat(choice(
      $._line,
      $._newline,
      seq(close, $._close_block),
    )),
    $._block_close,
    optional($.block_continuation),
  );
}
