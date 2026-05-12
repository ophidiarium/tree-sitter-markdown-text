#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Portable compile-time assertion. C11's `_Static_assert` would be cleaner,
// but `binding.gyp` can't pass `/std:c11` to MSVC without colliding with the
// `/std:c++20` that node-addon-api forces on the shared C/C++ target, so
// fall back to the negative-sized-array trick that every C dialect accepts.
#define TS_MD_STATIC_ASSERT(cond, tag) \
    typedef char ts_md_static_assert_##tag[(cond) ? 1 : -1]

enum {
    // Tab stop used when counting columns for indentation.
    TAB_STOP = 4,
    // Minimum indentation (spaces) that makes a line an indented code block.
    INDENTED_CODE_INDENT = 4,
    // Maximum indentation before a line starts being a list-item continuation.
    MAX_NON_CODE_INDENT = 3,
    // Minimum fence length for a fenced code block.
    FENCED_CODE_MIN_FENCE = 3,
    // Minimum run of `*`/`_`/`-` that forms a thematic break.
    THEMATIC_BREAK_MIN = 3,
    // Number of ATX heading levels (H1..H6).
    ATX_HEADING_LEVELS = 6,
    // Maximum number of digits allowed in an ordered-list marker.
    ORDERED_LIST_MAX_DIGITS = 9,
    // Metadata fences (`---` / `+++`) are exactly three characters wide.
    METADATA_FENCE_WIDTH = 3,
    // Initial capacity for the open-blocks stack.
    OPEN_BLOCKS_INITIAL_CAPACITY = 8,
    // Length of the CDATA prefix (`<![CDATA[`).
    CDATA_PREFIX_LEN = 9,
    // Maximum number of characters captured for HTML tag-name matching.
    HTML_TAG_NAME_MAX = 10,
    // Buffer holds up to HTML_TAG_NAME_MAX plus a null terminator.
    HTML_TAG_NAME_BUFFER = 11,
    // Sentinel written into name_length when a tag name overflows the buffer.
    HTML_TAG_NAME_TOO_LONG = 12,
    // Serialized integers are little-endian.
    SERIALIZED_BYTE_BITS = 8,
    SERIALIZED_U32_BYTE_1_SHIFT = 8,
    SERIALIZED_U32_BYTE_2_SHIFT = 16,
    SERIALIZED_U32_BYTE_3_SHIFT = 24,
    // Fixed header bytes that `serialize`/`deserialize` write/read before the
    // trailing block-byte payload: state, matched, indentation, column,
    // fenced_code_block_delimiter_length.
    SERIALIZED_HEADER_SIZE = 10,
    SERIALIZED_BLOCK_SIZE = 1,
};

// For explanation of the tokens see grammar.js
typedef enum {
    LINE_ENDING,
    SOFT_LINE_ENDING,
    BLOCK_CLOSE,
    BLOCK_CONTINUATION,
    BLOCK_QUOTE_START,
    INDENTED_CHUNK_START,
    ATX_H1_MARKER,
    ATX_H2_MARKER,
    ATX_H3_MARKER,
    ATX_H4_MARKER,
    ATX_H5_MARKER,
    ATX_H6_MARKER,
    SETEXT_H1_UNDERLINE,
    SETEXT_H2_UNDERLINE,
    THEMATIC_BREAK,
    LIST_MARKER_MINUS,
    LIST_MARKER_PLUS,
    LIST_MARKER_STAR,
    LIST_MARKER_PARENTHESIS,
    LIST_MARKER_DOT,
    LIST_MARKER_MINUS_DONT_INTERRUPT,
    LIST_MARKER_PLUS_DONT_INTERRUPT,
    LIST_MARKER_STAR_DONT_INTERRUPT,
    LIST_MARKER_PARENTHESIS_DONT_INTERRUPT,
    LIST_MARKER_DOT_DONT_INTERRUPT,
    TASK_LIST_MARKER_CHECKED,
    TASK_LIST_MARKER_UNCHECKED,
    MATH_INLINE_OPEN_DELIMITER,
    MATH_INLINE_CLOSE_DELIMITER,
    FENCED_CODE_BLOCK_START_BACKTICK,
    FENCED_CODE_BLOCK_START_TILDE,
    BLANK_LINE_START,
    FENCED_CODE_BLOCK_END_BACKTICK,
    FENCED_CODE_BLOCK_END_TILDE,
    HTML_BLOCK_1_START,
    HTML_BLOCK_1_END,
    HTML_BLOCK_2_START,
    HTML_BLOCK_3_START,
    HTML_BLOCK_4_START,
    HTML_BLOCK_5_START,
    HTML_BLOCK_6_START,
    HTML_BLOCK_7_START,
    CLOSE_BLOCK,
    NO_INDENTED_CHUNK,
    ERROR,
    TRIGGER_ERROR,
    TOKEN_EOF,
    MINUS_METADATA,
    PLUS_METADATA,
    PIPE_TABLE_START,
    PIPE_TABLE_LINE_ENDING,
    SCANNER_TOKEN_TYPE_COUNT,
} TokenType;

// Description of a block on the block stack.
//
// LIST_ITEM is a list item with minimal indentation (content begins at indent
// level 2) while LIST_ITEM_MAX_INDENTATION represents a list item with maximal
// indentation without being considered a indented code block.
//
// ANONYMOUS represents any block that whose close is not handled by the
// external s.
typedef enum {
    BLOCK_QUOTE,
    INDENTED_CODE_BLOCK,
    LIST_ITEM,
    LIST_ITEM_1_INDENTATION,
    LIST_ITEM_2_INDENTATION,
    LIST_ITEM_3_INDENTATION,
    LIST_ITEM_4_INDENTATION,
    LIST_ITEM_5_INDENTATION,
    LIST_ITEM_6_INDENTATION,
    LIST_ITEM_7_INDENTATION,
    LIST_ITEM_8_INDENTATION,
    LIST_ITEM_9_INDENTATION,
    LIST_ITEM_10_INDENTATION,
    LIST_ITEM_11_INDENTATION,
    LIST_ITEM_12_INDENTATION,
    LIST_ITEM_13_INDENTATION,
    LIST_ITEM_14_INDENTATION,
    LIST_ITEM_MAX_INDENTATION,
    FENCED_CODE_BLOCK,
    ANONYMOUS,
} Block;

// Determines if a character is punctuation as defined by the markdown spec.
// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool is_punctuation(char chr) {
    return (chr >= '!' && chr <= '/') || (chr >= ':' && chr <= '@') ||
           (chr >= '[' && chr <= '`') || (chr >= '{' && chr <= '~');
}

static bool is_ascii_digit(int32_t codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

static bool is_ascii_alpha(int32_t codepoint) {
    return (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z');
}

static bool is_ascii_alnum(int32_t codepoint) {
    return is_ascii_alpha(codepoint) || is_ascii_digit(codepoint);
}

static char ascii_tolower(int32_t codepoint) {
    if (codepoint >= 'A' && codepoint <= 'Z') {
        return (char)(codepoint - 'A' + 'a');
    }
    return (char)codepoint;
}

static size_t max_serialized_blocks(void) {
    return ((size_t)TREE_SITTER_SERIALIZATION_BUFFER_SIZE -
            (size_t)SERIALIZED_HEADER_SIZE) /
           (size_t)SERIALIZED_BLOCK_SIZE;
}

// Returns the indentation level which lines of a list item should have at
// minimum. Should only be called with blocks for which `is_list_item` returns
// true.
static uint16_t list_item_indentation(Block block) {
    return (uint16_t)(block - LIST_ITEM + 2);
}

enum {
    NUM_HTML_TAG_NAMES_RULE_1 = 3,
    NUM_HTML_TAG_NAMES_RULE_7 = 62,
};

static const char *const HTML_TAG_NAMES_RULE_1[NUM_HTML_TAG_NAMES_RULE_1] = {
    "pre", "script", "style"};

static const char *const HTML_TAG_NAMES_RULE_7[NUM_HTML_TAG_NAMES_RULE_7] = {
    "address",  "article",    "aside",  "base",     "basefont", "blockquote",
    "body",     "caption",    "center", "col",      "colgroup", "dd",
    "details",  "dialog",     "dir",    "div",      "dl",       "dt",
    "fieldset", "figcaption", "figure", "footer",   "form",     "frame",
    "frameset", "h1",         "h2",     "h3",       "h4",       "h5",
    "h6",       "head",       "header", "hr",       "html",     "iframe",
    "legend",   "li",         "link",   "main",     "menu",     "menuitem",
    "nav",      "noframes",   "ol",     "optgroup", "option",   "p",
    "param",    "section",    "source", "summary",  "table",    "tbody",
    "td",       "tfoot",      "th",     "thead",    "title",    "tr",
    "track",    "ul"};

// For explanation of the tokens see grammar.js. Designated initializers keep
// this table tied to TokenType names rather than enum positions.
static const bool paragraph_interrupt_symbols[SCANNER_TOKEN_TYPE_COUNT] = {
    [BLOCK_QUOTE_START] = true,
    [ATX_H1_MARKER] = true,
    [ATX_H2_MARKER] = true,
    [ATX_H3_MARKER] = true,
    [ATX_H4_MARKER] = true,
    [ATX_H5_MARKER] = true,
    [ATX_H6_MARKER] = true,
    [SETEXT_H1_UNDERLINE] = true,
    [SETEXT_H2_UNDERLINE] = true,
    [THEMATIC_BREAK] = true,
    [LIST_MARKER_MINUS] = true,
    [LIST_MARKER_PLUS] = true,
    [LIST_MARKER_STAR] = true,
    [LIST_MARKER_PARENTHESIS] = true,
    [LIST_MARKER_DOT] = true,
    [FENCED_CODE_BLOCK_START_BACKTICK] = true,
    [FENCED_CODE_BLOCK_START_TILDE] = true,
    [BLANK_LINE_START] = true,
    [HTML_BLOCK_1_START] = true,
    [HTML_BLOCK_2_START] = true,
    [HTML_BLOCK_3_START] = true,
    [HTML_BLOCK_4_START] = true,
    [HTML_BLOCK_5_START] = true,
    [HTML_BLOCK_6_START] = true,
    [PIPE_TABLE_START] = true,
};

// State bitflags used with `Scanner.state`.
enum {
    STATE_MATCHING = 1U << 0U,            // Currently matching at the beginning of a line.
    STATE_WAS_SOFT_LINE_BREAK = 1U << 1U, // Last line break was inside a paragraph.
    STATE_CLOSE_BLOCK = 1U << 4U,         // Block should be closed after next line break.
    STATE_ALL = STATE_MATCHING | STATE_WAS_SOFT_LINE_BREAK | STATE_CLOSE_BLOCK,
};
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)

TS_MD_STATIC_ASSERT(ATX_H6_MARKER == ATX_H1_MARKER + (ATX_HEADING_LEVELS - 1),
                    atx_markers_contiguous);
TS_MD_STATIC_ASSERT(SCANNER_TOKEN_TYPE_COUNT == PIPE_TABLE_LINE_ENDING + 1,
                    token_type_count_trails_enum);
TS_MD_STATIC_ASSERT(ANONYMOUS <= UINT8_MAX,
                    block_fits_in_one_byte);
TS_MD_STATIC_ASSERT(
    SERIALIZED_HEADER_SIZE <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
    serialized_header_fits_in_buffer);

typedef struct {
    // A stack of open blocks in the current parse state
    struct {
        size_t size;
        size_t capacity;
        Block *items;
    } open_blocks;

    // Parser state flags
    uint8_t state;
    // Number of blocks that have been matched so far. Only changes during
    // matching and is reset after every line ending.
    size_t matched;
    // Consumed but "unused" indentation. Sometimes a tab needs to be "split" to
    // be used in multiple tokens.
    uint16_t indentation;
    // The current column. Used to decide how many spaces a tab should equal
    uint8_t column;
    // The delimiter length of the currently open fenced code block
    uint32_t fenced_code_block_delimiter_length;

    bool simulate;
} Scanner;

typedef struct {
    size_t open_blocks_size;
    uint8_t state;
    size_t matched;
    uint16_t indentation;
    uint8_t column;
    uint32_t fenced_code_block_delimiter_length;
    bool simulate;
} ScannerSnapshot;

typedef struct {
    TokenType result_symbol;
    uint16_t extra_indentation;
    uint16_t marker_width_adjust;
} ListMarker;

typedef struct {
    char delimiter;
    TokenType result_symbol;
} MetadataFence;

// NOLINTNEXTLINE(readability-identifier-length)
static ScannerSnapshot snapshot_scanner(const Scanner *s) {
    ScannerSnapshot snapshot = {
        .open_blocks_size = s->open_blocks.size,
        .state = s->state,
        .matched = s->matched,
        .indentation = s->indentation,
        .column = s->column,
        .fenced_code_block_delimiter_length =
            s->fenced_code_block_delimiter_length,
        .simulate = s->simulate,
    };
    return snapshot;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void restore_scanner(Scanner *s, ScannerSnapshot snapshot) {
    s->open_blocks.size = snapshot.open_blocks_size;
    s->state = snapshot.state;
    s->matched = snapshot.matched;
    s->indentation = snapshot.indentation;
    s->column = snapshot.column;
    s->fenced_code_block_delimiter_length =
        snapshot.fenced_code_block_delimiter_length;
    s->simulate = snapshot.simulate;
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool restore_scanner_and_return(Scanner *s, ScannerSnapshot snapshot,
                                       bool result) {
    restore_scanner(s, snapshot);
    return result;
}

static void add_indentation(uint16_t *indentation, size_t amount) {
    if ((size_t)UINT16_MAX - (size_t)(*indentation) < amount) {
        *indentation = UINT16_MAX;
    } else {
        *indentation = (uint16_t)(*indentation + amount);
    }
}

// NOLINTNEXTLINE(readability-identifier-length) — `s`/`b` are the scanner/block conventions throughout this file
static bool push_block(Scanner *s, Block b) {
    size_t max_blocks = max_serialized_blocks();
    if (s->open_blocks.size >= max_blocks) {
        return false;
    }
    if (s->open_blocks.size == s->open_blocks.capacity) {
        size_t capacity = s->open_blocks.capacity != 0U
                              ? s->open_blocks.capacity << 1U
                              : (size_t)OPEN_BLOCKS_INITIAL_CAPACITY;
        if (capacity > max_blocks) {
            capacity = max_blocks;
        }
        void *tmp = ts_realloc(s->open_blocks.items,
                               sizeof(Block) * capacity);
        if (tmp == NULL) {
            return false;
        }
        s->open_blocks.items = tmp;
        s->open_blocks.capacity = capacity;
    }

    s->open_blocks.items[s->open_blocks.size++] = b;
    return true;
}

// NOLINTNEXTLINE(readability-identifier-length)
static inline Block pop_block(Scanner *s) {
    return s->open_blocks.items[--s->open_blocks.size];
}

// NOLINTNEXTLINE(readability-identifier-length)
static void write_u16(char *buffer, unsigned *size, uint16_t value) {
    buffer[(*size)++] = (char)(value & UINT8_MAX);
    buffer[(*size)++] = (char)(value >> SERIALIZED_BYTE_BITS);
}

// NOLINTNEXTLINE(readability-identifier-length)
static void write_u32(char *buffer, unsigned *size, uint32_t value) {
    buffer[(*size)++] = (char)(value & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_1_SHIFT) & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_2_SHIFT) & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_3_SHIFT) & UINT8_MAX);
}

// NOLINTNEXTLINE(readability-identifier-length)
static uint16_t read_u16(const char *buffer, unsigned *size) {
    uint16_t byte_0 = (uint8_t)buffer[(*size)++];
    uint16_t byte_1 = (uint8_t)buffer[(*size)++];
    return (uint16_t)(byte_0 | (uint16_t)(byte_1 << SERIALIZED_BYTE_BITS));
}

// NOLINTNEXTLINE(readability-identifier-length)
static uint32_t read_u32(const char *buffer, unsigned *size) {
    uint32_t byte_0 = (uint8_t)buffer[(*size)++];
    uint32_t byte_1 = (uint8_t)buffer[(*size)++];
    uint32_t byte_2 = (uint8_t)buffer[(*size)++];
    uint32_t byte_3 = (uint8_t)buffer[(*size)++];
    return byte_0 | (byte_1 << SERIALIZED_U32_BYTE_1_SHIFT) |
           (byte_2 << SERIALIZED_U32_BYTE_2_SHIFT) |
           (byte_3 << SERIALIZED_U32_BYTE_3_SHIFT);
}

static bool is_valid_block_value(uint8_t value) {
    return value <= (uint8_t)ANONYMOUS;
}

// Write the whole state of a Scanner to a byte buffer.
// NOLINTNEXTLINE(readability-identifier-length)
static unsigned serialize(Scanner *s, char *buffer) {
    unsigned size = 0;
    buffer[size++] = (char)s->state;
    write_u16(buffer, &size, (uint16_t)s->matched);
    write_u16(buffer, &size, s->indentation);
    buffer[size++] = (char)s->column;
    write_u32(buffer, &size, s->fenced_code_block_delimiter_length);
    assert(size == SERIALIZED_HEADER_SIZE);
    size_t max_blocks = max_serialized_blocks();
    size_t blocks_count = s->open_blocks.size < max_blocks
                              ? s->open_blocks.size
                              : max_blocks;
    for (size_t i = 0; i < blocks_count; i++) {
        buffer[size++] = (char)s->open_blocks.items[i];
    }
    return size;
}

// Read the whole state of a Scanner from a byte buffer.
// `serialize` and `deserialize` should be fully symmetric.
// NOLINTNEXTLINE(readability-identifier-length)
static void deserialize(Scanner *s, const char *buffer, unsigned length) {
    s->open_blocks.size = 0;
    s->state = 0;
    s->matched = 0;
    s->indentation = 0;
    s->column = 0;
    s->fenced_code_block_delimiter_length = 0;
    // The serialized form is a fixed header followed by one byte per Block.
    // Validate all fields before applying them so corrupted buffers resume from
    // a clean state instead of a partially restored one.
    if (length < SERIALIZED_HEADER_SIZE) {
        return;
    }
    size_t blocks_count = (size_t)length - (size_t)SERIALIZED_HEADER_SIZE;
    if (blocks_count > max_serialized_blocks()) {
        return;
    }
    unsigned size = 0;
    uint8_t state = (uint8_t)buffer[size++];
    size_t matched = (size_t)read_u16(buffer, &size);
    uint16_t indentation = read_u16(buffer, &size);
    uint8_t column = (uint8_t)buffer[size++];
    uint32_t fenced_code_block_delimiter_length = read_u32(buffer, &size);
    assert(size == SERIALIZED_HEADER_SIZE);
    if ((state & (uint8_t)(~STATE_ALL)) != 0 || column >= TAB_STOP ||
        matched > blocks_count) {
        return;
    }
    size_t block_offset = size;
    for (size_t i = 0; i < blocks_count; i++) {
        if (!is_valid_block_value((uint8_t)buffer[block_offset + i])) {
            return;
        }
    }
    if (blocks_count > 0 && s->open_blocks.capacity < blocks_count) {
        void *tmp = ts_realloc(s->open_blocks.items,
                               sizeof(Block) * blocks_count);
        if (tmp == NULL) {
            return;
        }
        s->open_blocks.items = tmp;
        s->open_blocks.capacity = blocks_count;
    }
    s->state = state;
    s->matched = matched;
    s->indentation = indentation;
    s->column = column;
    s->fenced_code_block_delimiter_length =
        fenced_code_block_delimiter_length;
    for (size_t i = 0; i < blocks_count; i++) {
        s->open_blocks.items[i] = (Block)(uint8_t)buffer[block_offset + i];
    }
    s->open_blocks.size = blocks_count;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void mark_end(Scanner *s, TSLexer *lexer) {
    if (!s->simulate) {
        lexer->mark_end(lexer);
    }
}

// Convenience function to emit the error token. This is done to stop invalid
// parse branches. Specifically:
// 1. When encountering a newline after a line break that ended a paragraph, and
// no new block
//    has been opened.
// 2. When encountering a new block after a soft line break.
// 3. When a `$._trigger_error` token is valid, which is used to stop parse
// branches through
//    normal tree-sitter grammar rules.
//
// See also the `$._soft_line_break` and `$._paragraph_end_newline` tokens in
// grammar.js
static bool error(TSLexer *lexer) {
    lexer->result_symbol = ERROR;
    return true;
}

// Advance the lexer one character
// Also keeps track of the current column, counting tabs as spaces with tab stop
// 4 See https://github.github.com/gfm/#tabs
// NOLINTNEXTLINE(readability-identifier-length)
static size_t advance(Scanner *s, TSLexer *lexer) {
    size_t size = 1;
    if (lexer->lookahead == '\t') {
        size = (size_t)(TAB_STOP - s->column);
        s->column = 0;
    } else {
        s->column = (uint8_t)((s->column + 1) % TAB_STOP);
    }
    lexer->advance(lexer, false);
    return size;
}

// Try to match the given block, i.e. consume all tokens that belong to the
// block. These are
// 1. indentation for list items and indented code blocks
// 2. '>' for block quotes
// Returns true if the block is matched and false otherwise
// NOLINTNEXTLINE(readability-identifier-length,readability-function-cognitive-complexity)
static bool match(Scanner *s, TSLexer *lexer, Block block) {
    switch (block) {
        case INDENTED_CODE_BLOCK:
            while (s->indentation < INDENTED_CODE_INDENT) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            if (s->indentation >= INDENTED_CODE_INDENT &&
                lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                s->indentation -= INDENTED_CODE_INDENT;
                return true;
            }
            break;
        case LIST_ITEM:
        case LIST_ITEM_1_INDENTATION:
        case LIST_ITEM_2_INDENTATION:
        case LIST_ITEM_3_INDENTATION:
        case LIST_ITEM_4_INDENTATION:
        case LIST_ITEM_5_INDENTATION:
        case LIST_ITEM_6_INDENTATION:
        case LIST_ITEM_7_INDENTATION:
        case LIST_ITEM_8_INDENTATION:
        case LIST_ITEM_9_INDENTATION:
        case LIST_ITEM_10_INDENTATION:
        case LIST_ITEM_11_INDENTATION:
        case LIST_ITEM_12_INDENTATION:
        case LIST_ITEM_13_INDENTATION:
        case LIST_ITEM_14_INDENTATION:
        case LIST_ITEM_MAX_INDENTATION:
            while (s->indentation < list_item_indentation(block)) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            if (s->indentation >= list_item_indentation(block)) {
                s->indentation -= list_item_indentation(block);
                return true;
            }
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                s->indentation = 0;
                return true;
            }
            break;
        case BLOCK_QUOTE:
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                add_indentation(&s->indentation, advance(s, lexer));
            }
            if (lexer->lookahead == '>') {
                advance(s, lexer);
                s->indentation = 0;
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation,
                                    advance(s, lexer) - 1U);
                }
                return true;
            }
            break;
        case FENCED_CODE_BLOCK:
        case ANONYMOUS:
            return true;
        default:
            break;
    }
    return false;
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool push_list_item(Scanner *s, uint16_t block_offset) {
    uint16_t max_offset =
        (uint16_t)(LIST_ITEM_MAX_INDENTATION - LIST_ITEM);
    if (block_offset > max_offset) {
        return false;
    }
    return push_block(s, (Block)(LIST_ITEM + block_offset));
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool finish_list_marker(Scanner *s, TSLexer *lexer, ListMarker marker) {
    assert(marker.extra_indentation >= 1);
    marker.extra_indentation--;
    uint16_t block_offset = 0;
    if (marker.extra_indentation <= MAX_NON_CODE_INDENT) {
        add_indentation(&marker.extra_indentation, s->indentation);
        s->indentation = 0;
        block_offset = marker.extra_indentation;
    } else {
        block_offset = s->indentation;
        s->indentation = marker.extra_indentation;
    }
    add_indentation(&block_offset, marker.marker_width_adjust);
    if (!s->simulate && !push_list_item(s, block_offset)) {
        return false;
    }
    lexer->result_symbol = marker.result_symbol;
    return true;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void consume_line_ending(Scanner *s, TSLexer *lexer) {
    if (lexer->lookahead == '\r') {
        advance(s, lexer);
        if (lexer->lookahead == '\n') {
            advance(s, lexer);
        }
    } else {
        advance(s, lexer);
    }
}

// NOLINTNEXTLINE(readability-identifier-length)
static void skip_horizontal_space(Scanner *s, TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(s, lexer);
    }
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool scan_metadata_block(Scanner *s, TSLexer *lexer,
                                MetadataFence fence) {
    for (;;) {
        consume_line_ending(s, lexer);
        size_t delimiter_count = 0;
        while (lexer->lookahead == fence.delimiter) {
            delimiter_count++;
            advance(s, lexer);
        }
        if (delimiter_count == METADATA_FENCE_WIDTH) {
            skip_horizontal_space(s, lexer);
            if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
                consume_line_ending(s, lexer);
                mark_end(s, lexer);
                lexer->result_symbol = fence.result_symbol;
                return true;
            }
        }
        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               !lexer->eof(lexer)) {
            advance(s, lexer);
        }
        if (lexer->eof(lexer)) {
            return false;
        }
    }
}

// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_fenced_code_block(Scanner *s, const char delimiter,
                                    TSLexer *lexer, const bool *valid_symbols) {
    // count the number of backticks
    uint32_t level = 0;
    while (lexer->lookahead == delimiter) {
        advance(s, lexer);
        if (level < UINT32_MAX) {
            level++;
        }
    }
    mark_end(s, lexer);
    // If this is able to close a fenced code block then that is the only valid
    // interpretation. It can only close a fenced code block if the number of
    // backticks is at least the number of backticks of the opening delimiter.
    // Also it cannot be indented more than 3 spaces.
    if ((delimiter == '`' ? valid_symbols[FENCED_CODE_BLOCK_END_BACKTICK]
                          : valid_symbols[FENCED_CODE_BLOCK_END_TILDE]) &&
        s->indentation <= MAX_NON_CODE_INDENT &&
        level >= s->fenced_code_block_delimiter_length) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            s->fenced_code_block_delimiter_length = 0;
            lexer->result_symbol = delimiter == '`'
                                       ? FENCED_CODE_BLOCK_END_BACKTICK
                                       : FENCED_CODE_BLOCK_END_TILDE;
            return true;
        }
    }
    // If this could be the start of a fenced code block, check if the info
    // string contains any backticks.
    if ((delimiter == '`' ? valid_symbols[FENCED_CODE_BLOCK_START_BACKTICK]
                          : valid_symbols[FENCED_CODE_BLOCK_START_TILDE]) &&
        s->indentation <= MAX_NON_CODE_INDENT &&
        level >= FENCED_CODE_MIN_FENCE) {
        bool info_string_has_backtick = false;
        if (delimiter == '`') {
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
                   !lexer->eof(lexer)) {
                if (lexer->lookahead == '`') {
                    info_string_has_backtick = true;
                    break;
                }
                advance(s, lexer);
            }
        }
        // If it does not then choose to interpret this as the start of a fenced
        // code block.
        if (!info_string_has_backtick) {
            lexer->result_symbol = delimiter == '`'
                                       ? FENCED_CODE_BLOCK_START_BACKTICK
                                       : FENCED_CODE_BLOCK_START_TILDE;
            if (!s->simulate && !push_block(s, FENCED_CODE_BLOCK)) {
                return false;
            }
            // Remember the length of the delimiter for later, since we need it
            // to decide whether a sequence of backticks can close the block.
            s->fenced_code_block_delimiter_length = level;
            s->indentation = 0;
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length)
static bool parse_task_list_marker(Scanner *s, TSLexer *lexer,
                                   const bool *valid_symbols) {
    if (!(valid_symbols[TASK_LIST_MARKER_CHECKED] ||
          valid_symbols[TASK_LIST_MARKER_UNCHECKED]) ||
        lexer->lookahead != '[') {
        return false;
    }

    advance(s, lexer);
    bool checked = false;
    bool unchecked = false;
    if (lexer->lookahead == 'x' || lexer->lookahead == 'X') {
        checked = true;
    } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        unchecked = true;
    }
    if ((!checked || !valid_symbols[TASK_LIST_MARKER_CHECKED]) &&
        (!unchecked || !valid_symbols[TASK_LIST_MARKER_UNCHECKED])) {
        return false;
    }

    advance(s, lexer);
    if (lexer->lookahead != ']') {
        return false;
    }

    advance(s, lexer);
    if (lexer->lookahead != ' ' && lexer->lookahead != '\t' &&
        lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
        !lexer->eof(lexer)) {
        return false;
    }

    lexer->result_symbol = TASK_LIST_MARKER_UNCHECKED;
    if (checked) {
        lexer->result_symbol = TASK_LIST_MARKER_CHECKED;
    }
    return true;
}
// NOLINTEND(readability-identifier-length)


// NOLINTBEGIN(readability-identifier-length)
static bool parse_math_inline_delimiter(Scanner *s, TSLexer *lexer,
                                        const bool *valid_symbols) {
    if (!(valid_symbols[MATH_INLINE_OPEN_DELIMITER] ||
          valid_symbols[MATH_INLINE_CLOSE_DELIMITER]) ||
        lexer->lookahead != '$') {
        return false;
    }

    uint16_t start_indentation = s->indentation;
    uint8_t start_column = s->column;
    advance(s, lexer);

    if (valid_symbols[MATH_INLINE_OPEN_DELIMITER] &&
        lexer->lookahead != '$' && lexer->lookahead != ' ' &&
        lexer->lookahead != '\t' && lexer->lookahead != '\n' &&
        lexer->lookahead != '\r' && !lexer->eof(lexer)) {
        uint16_t delimiter_indentation = s->indentation;
        uint8_t delimiter_column = s->column;
        mark_end(s, lexer);

        int32_t previous = 0;
        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               !lexer->eof(lexer)) {
            if (lexer->lookahead == '$') {
                advance(s, lexer);
                bool previous_is_space = false;
                if (previous == ' ' || previous == '\t') {
                    previous_is_space = true;
                }
                if (!previous_is_space && !is_ascii_digit(lexer->lookahead)) {
                    s->indentation = delimiter_indentation;
                    s->column = delimiter_column;
                    lexer->result_symbol = MATH_INLINE_OPEN_DELIMITER;
                    return true;
                }
                break;
            }
            previous = lexer->lookahead;
            advance(s, lexer);
        }
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }

    if (valid_symbols[MATH_INLINE_CLOSE_DELIMITER] &&
        !is_ascii_digit(lexer->lookahead)) {
        lexer->result_symbol = MATH_INLINE_CLOSE_DELIMITER;
        return true;
    }

    s->indentation = start_indentation;
    s->column = start_column;
    return false;
}
// NOLINTEND(readability-identifier-length)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_star(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->indentation > MAX_NON_CODE_INDENT ||
        !(valid_symbols[LIST_MARKER_STAR] ||
          valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT] ||
          valid_symbols[THEMATIC_BREAK])) {
        return false;
    }
    advance(s, lexer);
    mark_end(s, lexer);
    // Otherwise count the number of stars permitting whitespaces between them.
    size_t star_count = 1;
    // Also remember how many stars there are before the first whitespace...
    // ...and how many spaces follow the first star.
    uint16_t extra_indentation = 0;
    for (;;) {
        if (lexer->lookahead == '*') {
            if (star_count == 1 && extra_indentation >= 1 &&
                valid_symbols[LIST_MARKER_STAR]) {
                // If we get to this point then the token has to be at least
                // this long. We need to call `mark_end` here in case we decide
                // later that this is a list item.
                mark_end(s, lexer);
            }
            star_count++;
            advance(s, lexer);
        } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            if (star_count == 1) {
                add_indentation(&extra_indentation, advance(s, lexer));
            } else {
                advance(s, lexer);
            }
        } else {
            break;
        }
    }
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
    bool dont_interrupt = false;
    if (star_count == 1 && line_end) {
        extra_indentation = 1;
        // line is empty so don't interrupt paragraphs if this is a list marker
        dont_interrupt = s->matched == s->open_blocks.size;
    }
    // If there were at least 3 stars then this could be a thematic break
    bool thematic_break = star_count >= 3 && line_end;
    // If there was a star and at least one space after that star then this
    // could be a list marker.
    bool list_marker_star = star_count >= 1 && extra_indentation >= 1;
    if (valid_symbols[THEMATIC_BREAK] && thematic_break &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        // If a thematic break is valid then it takes precedence
        lexer->result_symbol = THEMATIC_BREAK;
        mark_end(s, lexer);
        s->indentation = 0;
        return true;
    }
    if ((dont_interrupt ? valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT]
                        : valid_symbols[LIST_MARKER_STAR]) &&
        list_marker_star) {
        // List markers take precedence over emphasis markers
        // If star_count > 1 then we already called mark_end at the right point.
        // Otherwise the token should go until this point.
        if (star_count == 1) {
            mark_end(s, lexer);
        }
        return finish_list_marker(
            s, lexer,
            (ListMarker){
                .result_symbol = dont_interrupt
                                     ? LIST_MARKER_STAR_DONT_INTERRUPT
                                     : LIST_MARKER_STAR,
                .extra_indentation = extra_indentation,
                .marker_width_adjust = 0,
            });
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_thematic_break_underscore(Scanner *s, TSLexer *lexer,
                                            const bool *valid_symbols) {
    advance(s, lexer);
    mark_end(s, lexer);
    size_t underscore_count = 1;
    for (;;) {
        if (lexer->lookahead == '_') {
            underscore_count++;
            advance(s, lexer);
        } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        } else {
            break;
        }
    }
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
    if (s->indentation <= MAX_NON_CODE_INDENT && underscore_count >= 3 &&
        line_end && valid_symbols[THEMATIC_BREAK]) {
        lexer->result_symbol = THEMATIC_BREAK;
        mark_end(s, lexer);
        s->indentation = 0;
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_block_quote(Scanner *s, TSLexer *lexer,
                              const bool *valid_symbols) {
    if (valid_symbols[BLOCK_QUOTE_START] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        advance(s, lexer);
        s->indentation = 0;
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            add_indentation(&s->indentation, advance(s, lexer) - 1U);
        }
        lexer->result_symbol = BLOCK_QUOTE_START;
        if (!s->simulate && !push_block(s, BLOCK_QUOTE)) {
            return false;
        }
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_atx_heading(Scanner *s, TSLexer *lexer,
                              const bool *valid_symbols) {
    if (valid_symbols[ATX_H1_MARKER] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        mark_end(s, lexer);
        uint16_t level = 0;
        while (lexer->lookahead == '#' && level <= ATX_HEADING_LEVELS) {
            advance(s, lexer);
            level++;
        }
        if (level <= ATX_HEADING_LEVELS &&
            (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
             lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            lexer->result_symbol = ATX_H1_MARKER + (level - 1);
            s->indentation = 0;
            mark_end(s, lexer);
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_setext_underline(Scanner *s, TSLexer *lexer,
                                   const bool *valid_symbols) {
    if (valid_symbols[SETEXT_H1_UNDERLINE] &&
        s->indentation <= MAX_NON_CODE_INDENT &&
        s->matched == s->open_blocks.size) {
        mark_end(s, lexer);
        while (lexer->lookahead == '=') {
            advance(s, lexer);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            lexer->result_symbol = SETEXT_H1_UNDERLINE;
            mark_end(s, lexer);
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_plus(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_PLUS] ||
         valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT] ||
         valid_symbols[PLUS_METADATA])) {
        advance(s, lexer);
        if (valid_symbols[PLUS_METADATA] && lexer->lookahead == '+') {
            advance(s, lexer);
            if (lexer->lookahead != '+') {
                return false;
            }
            advance(s, lexer);
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                advance(s, lexer);
            }
            if (lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                return false;
            }
            ScannerSnapshot metadata_snapshot = snapshot_scanner(s);
            if (scan_metadata_block(
                    s, lexer,
                    (MetadataFence){.delimiter = '+',
                                    .result_symbol = PLUS_METADATA})) {
                return true;
            }
            restore_scanner(s, metadata_snapshot);
            return false;
        } else {
            uint16_t extra_indentation = 0;
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                add_indentation(&extra_indentation, advance(s, lexer));
            }
            bool dont_interrupt = false;
            if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
                extra_indentation = 1;
                dont_interrupt = true;
            }
            dont_interrupt =
                dont_interrupt && s->matched == s->open_blocks.size;
            if (extra_indentation >= 1 &&
                (dont_interrupt ? valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT]
                                : valid_symbols[LIST_MARKER_PLUS])) {
                return finish_list_marker(
                    s, lexer,
                    (ListMarker){
                        .result_symbol = dont_interrupt
                                             ? LIST_MARKER_PLUS_DONT_INTERRUPT
                                             : LIST_MARKER_PLUS,
                        .extra_indentation = extra_indentation,
                        .marker_width_adjust = 0,
                    });
            }
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_ordered_list_marker(Scanner *s, TSLexer *lexer,
                                      const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_PARENTHESIS] ||
         valid_symbols[LIST_MARKER_DOT] ||
         valid_symbols[LIST_MARKER_PARENTHESIS_DONT_INTERRUPT] ||
         valid_symbols[LIST_MARKER_DOT_DONT_INTERRUPT])) {
        size_t digits = 0;
        size_t marker_value = 0;
        while (is_ascii_digit(lexer->lookahead)) {
            if (digits < ORDERED_LIST_MAX_DIGITS) {
                marker_value =
                    (marker_value * 10U) + (size_t)(lexer->lookahead - '0');
            }
            digits++;
            advance(s, lexer);
        }
        if (digits >= 1 && digits <= ORDERED_LIST_MAX_DIGITS) {
            bool dont_interrupt = marker_value != 1U;
            bool dot = false;
            bool parenthesis = false;
            if (lexer->lookahead == '.') {
                advance(s, lexer);
                dot = true;
            } else if (lexer->lookahead == ')') {
                advance(s, lexer);
                parenthesis = true;
            }
            if (dot || parenthesis) {
                uint16_t extra_indentation = 0;
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&extra_indentation, advance(s, lexer));
                }
                bool line_end =
                    lexer->lookahead == '\n' || lexer->lookahead == '\r';
                if (line_end) {
                    extra_indentation = 1;
                    dont_interrupt = true;
                }
                dont_interrupt =
                    dont_interrupt && s->matched == s->open_blocks.size;
                TokenType result_symbol =
                    dot ? (dont_interrupt ? LIST_MARKER_DOT_DONT_INTERRUPT
                                          : LIST_MARKER_DOT)
                        : (dont_interrupt
                               ? LIST_MARKER_PARENTHESIS_DONT_INTERRUPT
                               : LIST_MARKER_PARENTHESIS);
                if (extra_indentation >= 1 && valid_symbols[result_symbol]) {
                    return finish_list_marker(
                        s, lexer,
                        (ListMarker){
                            .result_symbol = result_symbol,
                            .extra_indentation = extra_indentation,
                            .marker_width_adjust = (uint16_t)digits,
                        });
                }
            }
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_minus(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_MINUS] ||
         valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT] ||
         valid_symbols[SETEXT_H2_UNDERLINE] || valid_symbols[THEMATIC_BREAK] ||
         valid_symbols[MINUS_METADATA])) {
        mark_end(s, lexer);
        bool whitespace_after_minus = false;
        bool minus_after_whitespace = false;
        size_t minus_count = 0;
        uint16_t extra_indentation = 0;

        for (;;) {
            if (lexer->lookahead == '-') {
                if (minus_count == 1 && extra_indentation >= 1) {
                    mark_end(s, lexer);
                }
                minus_count++;
                advance(s, lexer);
                minus_after_whitespace = whitespace_after_minus;
            } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                if (minus_count == 1) {
                    add_indentation(&extra_indentation, advance(s, lexer));
                } else {
                    advance(s, lexer);
                }
                whitespace_after_minus = true;
            } else {
                break;
            }
        }
        bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
        bool dont_interrupt = false;
        if (minus_count == 1 && line_end) {
            extra_indentation = 1;
            dont_interrupt = true;
        }
        dont_interrupt = dont_interrupt && s->matched == s->open_blocks.size;
        bool thematic_break = minus_count >= 3 && line_end;
        bool underline =
            minus_count >= 1 && !minus_after_whitespace && line_end &&
            s->matched ==
                s->open_blocks
                    .size; // setext heading can not break lazy continuation
        bool list_marker_minus = minus_count >= 1 && extra_indentation >= 1;
        bool success = false;
        if (valid_symbols[SETEXT_H2_UNDERLINE] && underline) {
            lexer->result_symbol = SETEXT_H2_UNDERLINE;
            mark_end(s, lexer);
            s->indentation = 0;
            success = true;
        } else if (valid_symbols[THEMATIC_BREAK] &&
                   thematic_break) { // underline is false if list_marker_minus
                                     // is true
            lexer->result_symbol = THEMATIC_BREAK;
            mark_end(s, lexer);
            s->indentation = 0;
            success = true;
        } else if ((dont_interrupt
                        ? valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT]
                        : valid_symbols[LIST_MARKER_MINUS]) &&
                   list_marker_minus) {
            if (minus_count == 1) {
                mark_end(s, lexer);
            }
            return finish_list_marker(
                s, lexer,
                (ListMarker){
                    .result_symbol = dont_interrupt
                                         ? LIST_MARKER_MINUS_DONT_INTERRUPT
                                         : LIST_MARKER_MINUS,
                    .extra_indentation = extra_indentation,
                    .marker_width_adjust = 0,
                });
        }
        if (minus_count == 3 && (!minus_after_whitespace) && line_end &&
            valid_symbols[MINUS_METADATA]) {
            ScannerSnapshot metadata_snapshot = snapshot_scanner(s);
            if (scan_metadata_block(
                    s, lexer,
                    (MetadataFence){.delimiter = '-',
                                    .result_symbol = MINUS_METADATA})) {
                return true;
            }
            restore_scanner(s, metadata_snapshot);
        }
        if (success) {
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_html_block(Scanner *s, TSLexer *lexer,
                             const bool *valid_symbols) {
    if (!(valid_symbols[HTML_BLOCK_1_START] ||
          valid_symbols[HTML_BLOCK_1_END] ||
          valid_symbols[HTML_BLOCK_2_START] ||
          valid_symbols[HTML_BLOCK_3_START] ||
          valid_symbols[HTML_BLOCK_4_START] ||
          valid_symbols[HTML_BLOCK_5_START] ||
          valid_symbols[HTML_BLOCK_6_START] ||
          valid_symbols[HTML_BLOCK_7_START])) {
        return false;
    }
    advance(s, lexer);
    if (lexer->lookahead == '?' && valid_symbols[HTML_BLOCK_3_START] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        advance(s, lexer);
        lexer->result_symbol = HTML_BLOCK_3_START;
        if (!s->simulate && !push_block(s, ANONYMOUS)) {
            return false;
        }
        return true;
    }
    if (lexer->lookahead == '!') {
        // could be block 2
        advance(s, lexer);
        if (lexer->lookahead == '-') {
            advance(s, lexer);
            if (lexer->lookahead == '-' && valid_symbols[HTML_BLOCK_2_START] &&
                s->indentation <= MAX_NON_CODE_INDENT) {
                advance(s, lexer);
                lexer->result_symbol = HTML_BLOCK_2_START;
                if (!s->simulate && !push_block(s, ANONYMOUS)) {
                    return false;
                }
                return true;
            }
        } else if ('A' <= lexer->lookahead && lexer->lookahead <= 'Z' &&
                   valid_symbols[HTML_BLOCK_4_START] &&
                   s->indentation <= MAX_NON_CODE_INDENT) {
            advance(s, lexer);
            lexer->result_symbol = HTML_BLOCK_4_START;
            if (!s->simulate && !push_block(s, ANONYMOUS)) {
                return false;
            }
            return true;
        } else if (lexer->lookahead == '[') {
            advance(s, lexer);
            if (lexer->lookahead == 'C') {
                advance(s, lexer);
                if (lexer->lookahead == 'D') {
                    advance(s, lexer);
                    if (lexer->lookahead == 'A') {
                        advance(s, lexer);
                        if (lexer->lookahead == 'T') {
                            advance(s, lexer);
                            if (lexer->lookahead == 'A') {
                                advance(s, lexer);
                                if (lexer->lookahead == '[' &&
                                    valid_symbols[HTML_BLOCK_5_START] &&
                                    s->indentation <= MAX_NON_CODE_INDENT) {
                                    advance(s, lexer);
                                    lexer->result_symbol = HTML_BLOCK_5_START;
                                    if (!s->simulate &&
                                        !push_block(s, ANONYMOUS)) {
                                        return false;
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    bool starting_slash = lexer->lookahead == '/';
    if (starting_slash) {
        advance(s, lexer);
    }
    bool starts_with_ascii_uppercase =
        lexer->lookahead >= 'A' && lexer->lookahead <= 'Z';
    char name[HTML_TAG_NAME_BUFFER];
    size_t name_length = 0;
    while (is_ascii_alpha(lexer->lookahead)) {
        if (name_length < HTML_TAG_NAME_MAX) {
            name[name_length++] = ascii_tolower(lexer->lookahead);
        } else {
            name_length = HTML_TAG_NAME_TOO_LONG;
        }
        advance(s, lexer);
    }
    if (name_length == 0) {
        return false;
    }
    bool tag_closed = false;
    if (name_length < HTML_TAG_NAME_BUFFER) {
        name[name_length] = 0;
        bool next_symbol_valid =
            lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
            lexer->lookahead == '\n' || lexer->lookahead == '\r' ||
            lexer->lookahead == '>';
        if (next_symbol_valid) {
            // try block 1 names
            for (size_t i = 0; i < NUM_HTML_TAG_NAMES_RULE_1; i++) {
                if (strcmp(name, HTML_TAG_NAMES_RULE_1[i]) == 0) {
                    if (starting_slash) {
                        if (valid_symbols[HTML_BLOCK_1_END]) {
                            lexer->result_symbol = HTML_BLOCK_1_END;
                            return true;
                        }
                    } else if (valid_symbols[HTML_BLOCK_1_START] &&
                               s->indentation <= MAX_NON_CODE_INDENT) {
                        lexer->result_symbol = HTML_BLOCK_1_START;
                        if (!s->simulate && !push_block(s, ANONYMOUS)) {
                            return false;
                        }
                        return true;
                    }
                }
            }
        }
        if (!next_symbol_valid && lexer->lookahead == '/') {
            advance(s, lexer);
            if (lexer->lookahead == '>') {
                advance(s, lexer);
                tag_closed = true;
            }
        }
        if (next_symbol_valid || tag_closed) {
            // try block 2 names
            for (size_t i = 0; i < NUM_HTML_TAG_NAMES_RULE_7; i++) {
                if (strcmp(name, HTML_TAG_NAMES_RULE_7[i]) == 0 &&
                    valid_symbols[HTML_BLOCK_6_START] &&
                    s->indentation <= MAX_NON_CODE_INDENT) {
                    lexer->result_symbol = HTML_BLOCK_6_START;
                    if (!s->simulate && !push_block(s, ANONYMOUS)) {
                        return false;
                    }
                    return true;
                }
            }
        }
    }

    // Known HTML tags already matched case-insensitively above. Any remaining
    // ASCII-uppercase start tag falls through to MDX JSX instead of becoming a
    // generic CommonMark HTML block type 7.
    if (starts_with_ascii_uppercase) {
        return false;
    }

    if (!valid_symbols[HTML_BLOCK_7_START] ||
        s->indentation > MAX_NON_CODE_INDENT) {
        return false;
    }

    if (!tag_closed) {
        // tag name (continued)
        while (is_ascii_alnum(lexer->lookahead) ||
               lexer->lookahead == '-') {
            advance(s, lexer);
        }
        if (!starting_slash) {
            // attributes
            bool had_whitespace = false;
            for (;;) {
                // whitespace
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    had_whitespace = true;
                    advance(s, lexer);
                }
                if (lexer->lookahead == '/') {
                    advance(s, lexer);
                    break;
                }
                if (lexer->lookahead == '>') {
                    break;
                }
                // attribute name
                if (!had_whitespace) {
                    return false;
                }
                if (!is_ascii_alpha(lexer->lookahead) &&
                    lexer->lookahead != '_' && lexer->lookahead != ':') {
                    return false;
                }
                had_whitespace = false;
                advance(s, lexer);
                while (is_ascii_alnum(lexer->lookahead) ||
                       lexer->lookahead == '_' || lexer->lookahead == '.' ||
                       lexer->lookahead == ':' || lexer->lookahead == '-') {
                    advance(s, lexer);
                }
                // attribute value specification
                // optional whitespace
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    had_whitespace = true;
                    advance(s, lexer);
                }
                // =
                if (lexer->lookahead == '=') {
                    advance(s, lexer);
                    had_whitespace = false;
                    // optional whitespace
                    while (lexer->lookahead == ' ' ||
                           lexer->lookahead == '\t') {
                        advance(s, lexer);
                    }
                    // attribute value
                    if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
                        char delimiter = (char)lexer->lookahead;
                        advance(s, lexer);
                        while (lexer->lookahead != delimiter &&
                               lexer->lookahead != '\n' &&
                               lexer->lookahead != '\r' && !lexer->eof(lexer)) {
                            advance(s, lexer);
                        }
                        if (lexer->lookahead != delimiter) {
                            return false;
                        }
                        advance(s, lexer);
                    } else {
                        // unquoted attribute value
                        bool had_one = false;
                        while (lexer->lookahead != ' ' &&
                               lexer->lookahead != '\t' &&
                               lexer->lookahead != '"' &&
                               lexer->lookahead != '\'' &&
                               lexer->lookahead != '=' &&
                               lexer->lookahead != '<' &&
                               lexer->lookahead != '>' &&
                               lexer->lookahead != '`' &&
                               lexer->lookahead != '\n' &&
                               lexer->lookahead != '\r' && !lexer->eof(lexer)) {
                            advance(s, lexer);
                            had_one = true;
                        }
                        if (!had_one) {
                            return false;
                        }
                    }
                }
            }
        } else {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                advance(s, lexer);
            }
        }
        if (lexer->lookahead != '>') {
            return false;
        }
        advance(s, lexer);
    }
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(s, lexer);
    }
    if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
        lexer->result_symbol = HTML_BLOCK_7_START;
        if (!s->simulate && !push_block(s, ANONYMOUS)) {
            return false;
        }
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_pipe_table(Scanner *s, TSLexer *lexer,
                             const bool *valid_symbols) {
    if (!valid_symbols[PIPE_TABLE_START]) {
        return false;
    }

    // PIPE_TABLE_START is zero width
    mark_end(s, lexer);
    ScannerSnapshot snapshot = snapshot_scanner(s);
    // count number of cells
    size_t cell_count = 0;
    // also remember if we see starting and ending pipes, as empty headers have
    // to have both
    bool starting_pipe = false;
    bool ending_pipe = false;
    if (lexer->lookahead == '|') {
        starting_pipe = true;
        advance(s, lexer);
    }
    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' &&
           !lexer->eof(lexer)) {
        if (lexer->lookahead == '|') {
            cell_count++;
            ending_pipe = true;
            advance(s, lexer);
        } else {
            if (lexer->lookahead != ' ' && lexer->lookahead != '\t') {
                ending_pipe = false;
            }
            if (lexer->lookahead == '\\') {
                advance(s, lexer);
                if (is_punctuation((char)lexer->lookahead)) {
                    advance(s, lexer);
                }
            } else {
                advance(s, lexer);
            }
        }
    }
    if (cell_count == 0 && !(starting_pipe && ending_pipe)) {
        return restore_scanner_and_return(s, snapshot, false);
    }
    if (!ending_pipe) {
        cell_count++;
    }

    // check the following line for a delimiter row
    // parse a newline
    if (lexer->lookahead == '\n') {
        advance(s, lexer);
    } else if (lexer->lookahead == '\r') {
        advance(s, lexer);
        if (lexer->lookahead == '\n') {
            advance(s, lexer);
        }
    } else {
        return restore_scanner_and_return(s, snapshot, false);
    }
    s->indentation = 0;
    s->column = 0;
    for (;;) {
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            add_indentation(&s->indentation, advance(s, lexer));
        } else {
            break;
        }
    }
    s->simulate = true;
    size_t matched_temp = 0;
    while (matched_temp < s->open_blocks.size) {
        if (match(s, lexer, s->open_blocks.items[matched_temp])) {
            matched_temp++;
        } else {
            return restore_scanner_and_return(s, snapshot, false);
        }
    }

    // check if delimiter row has the same number of cells and at least one pipe
    size_t delimiter_cell_count = 0;
    if (lexer->lookahead == '|') {
        advance(s, lexer);
    }
    for (;;) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '|') {
            delimiter_cell_count++;
            advance(s, lexer);
            continue;
        }
        if (lexer->lookahead == ':') {
            advance(s, lexer);
            if (lexer->lookahead != '-') {
                return restore_scanner_and_return(s, snapshot, false);
            }
        }
        bool had_one_minus = false;
        while (lexer->lookahead == '-') {
            had_one_minus = true;
            advance(s, lexer);
        }
        if (had_one_minus) {
            delimiter_cell_count++;
        }
        if (lexer->lookahead == ':') {
            if (!had_one_minus) {
                return restore_scanner_and_return(s, snapshot, false);
            }
            advance(s, lexer);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '|') {
            if (!had_one_minus) {
                delimiter_cell_count++;
            }
            advance(s, lexer);
            continue;
        }
        if (lexer->lookahead != '\r' && lexer->lookahead != '\n') {
            return restore_scanner_and_return(s, snapshot, false);
        } else {
            break;
        }
    }
    // if the cell counts are not equal then this is not a table
    if (cell_count != delimiter_cell_count) {
        return restore_scanner_and_return(s, snapshot, false);
    }

    lexer->result_symbol = PIPE_TABLE_START;
    return restore_scanner_and_return(s, snapshot, true);
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool scan(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    // A normal tree-sitter rule decided that the current branch is invalid and
    // now "requests" an error to stop the branch
    if (valid_symbols[TRIGGER_ERROR]) {
        return error(lexer);
    }

    // Close the inner most block after the next line break as requested. See
    // `$._close_block` in grammar.js
    if (valid_symbols[CLOSE_BLOCK]) {
        s->state |= STATE_CLOSE_BLOCK;
        lexer->result_symbol = CLOSE_BLOCK;
        return true;
    }

    // if we are at the end of the file and there are still open blocks close
    // them all
    if (lexer->eof(lexer)) {
        if (valid_symbols[TOKEN_EOF]) {
            lexer->result_symbol = TOKEN_EOF;
            return true;
        }
        if (s->open_blocks.size > 0) {
            lexer->result_symbol = BLOCK_CLOSE;
            if (!s->simulate) {
                pop_block(s);

            }
            return true;
        }
        return false;
    }

    if (!(s->state & STATE_MATCHING)) {
        // Parse any preceeding whitespace and remember its length. This makes a
        // lot of parsing quite a bit easier.
        for (;;) {
            if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                add_indentation(&s->indentation, advance(s, lexer));
            } else {
                break;
            }
        }
        // We are not matching. This is where the parsing logic for most
        // "normal" token is. Most importantly parsing logic for the start of
        // new blocks.
        if (valid_symbols[INDENTED_CHUNK_START] &&
            !valid_symbols[NO_INDENTED_CHUNK]) {
            if (s->indentation >= INDENTED_CODE_INDENT &&
                lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                lexer->result_symbol = INDENTED_CHUNK_START;
                if (!s->simulate && !push_block(s, INDENTED_CODE_BLOCK)) {
                    return false;
                }
                s->indentation -= INDENTED_CODE_INDENT;
                return true;
            }
        }
        // Decide which tokens to consider based on the first non-whitespace
        // character
        switch (lexer->lookahead) {
            case '\r':
            case '\n':
                if (valid_symbols[BLANK_LINE_START]) {
                    // A blank line token is actually just 0 width, so do not
                    // consume the characters
                    lexer->result_symbol = BLANK_LINE_START;
                    return true;
                }
                break;
            case '`':
                // A backtick could mark the beginning or ending of a fenced
                // code block.
                return parse_fenced_code_block(s, '`', lexer, valid_symbols);
            case '~':
                // A tilde could mark the beginning or ending of a fenced code
                // block.
                return parse_fenced_code_block(s, '~', lexer, valid_symbols);
            case '*':
                // A star could either mark  a list item or a thematic break.
                // This code is similar to the code for '_' and '+'.
                return parse_star(s, lexer, valid_symbols);
            case '_':
                return parse_thematic_break_underscore(s, lexer, valid_symbols);
            case '>':
                // A '>' could mark the beginning of a block quote
                return parse_block_quote(s, lexer, valid_symbols);
            case '#':
                // A '#' could mark a atx heading
                return parse_atx_heading(s, lexer, valid_symbols);
            case '=':
                // A '=' could mark a setext underline
                return parse_setext_underline(s, lexer, valid_symbols);
            case '+':
                // A '+' could be a list marker
                return parse_plus(s, lexer, valid_symbols);
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                // A number could be a list marker (if followed by a dot or a
                // parenthesis)
                return parse_ordered_list_marker(s, lexer, valid_symbols);
            case '-':
                // A minus could mark a list marker, a thematic break or a
                // setext underline
                return parse_minus(s, lexer, valid_symbols);
            case '[':
                return parse_task_list_marker(s, lexer, valid_symbols);
            case '$':
                return parse_math_inline_delimiter(s, lexer, valid_symbols);
            case '<':
                // A < could mark the beginning of a html block
                return parse_html_block(s, lexer, valid_symbols);
            default:
                break;
        }
        if (lexer->lookahead != '\r' && lexer->lookahead != '\n' &&
            valid_symbols[PIPE_TABLE_START]) {
            return parse_pipe_table(s, lexer, valid_symbols);
        }
    } else { // we are in the state of trying to match all currently open blocks
        bool partial_success = false;
        while (s->matched < s->open_blocks.size) {
            if (s->matched + 1U == s->open_blocks.size &&
                (s->state & STATE_CLOSE_BLOCK)) {
                if (!partial_success) {
                    s->state &= ~STATE_CLOSE_BLOCK;

                }
                break;
            }
            if (match(s, lexer, s->open_blocks.items[s->matched])) {
                partial_success = true;
                s->matched++;
            } else {
                if (s->state & STATE_WAS_SOFT_LINE_BREAK) {
                    s->state &= (~STATE_MATCHING);
                }
                break;
            }
        }
        if (partial_success) {
            if (s->matched == s->open_blocks.size) {
                s->state &= (~STATE_MATCHING);
            }
            lexer->result_symbol = BLOCK_CONTINUATION;
            return true;
        }

        if (!(s->state & STATE_WAS_SOFT_LINE_BREAK)) {
            lexer->result_symbol = BLOCK_CLOSE;
            pop_block(s);
            if (s->matched == s->open_blocks.size) {
                s->state &= (~STATE_MATCHING);
            }
            return true;
        }
    }

    // The parser just encountered a line break. Setup the state correspondingly
    if ((valid_symbols[LINE_ENDING] || valid_symbols[SOFT_LINE_ENDING] ||
         valid_symbols[PIPE_TABLE_LINE_ENDING]) &&
        (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
        consume_line_ending(s, lexer);
        s->indentation = 0;
        s->column = 0;
        if (!(s->state & STATE_CLOSE_BLOCK) &&
            (valid_symbols[SOFT_LINE_ENDING] ||
             valid_symbols[PIPE_TABLE_LINE_ENDING])) {
            lexer->mark_end(lexer);
            for (;;) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            s->simulate = true;
            size_t matched_temp = s->matched;
            s->matched = 0;
            bool one_will_be_matched = false;
            while (s->matched < s->open_blocks.size) {
                if (match(s, lexer, s->open_blocks.items[s->matched])) {
                    s->matched++;
                    one_will_be_matched = true;
                } else {
                    break;
                }
            }
            bool all_will_be_matched = s->matched == s->open_blocks.size;
            ScannerSnapshot interrupt_snapshot = snapshot_scanner(s);
            bool paragraph_interrupted =
                !lexer->eof(lexer) && scan(s, lexer, paragraph_interrupt_symbols);
            restore_scanner(s, interrupt_snapshot);
            if (!paragraph_interrupted) {
                s->matched = matched_temp;
                // If the last line break ended a paragraph and no new block
                // opened, the last line break should have been a soft line
                // break Reset the counter for matched blocks
                s->matched = 0;
                s->indentation = 0;
                s->column = 0;
                // If there is at least one open block, we should be in the
                // matching state. Also set the matching flag if a
                // `$._soft_line_break_marker` can be emitted so it does get
                // emitted.
                if (one_will_be_matched) {
                    s->state |= STATE_MATCHING;
                } else {
                    s->state &= (~STATE_MATCHING);
                }
                if (valid_symbols[PIPE_TABLE_LINE_ENDING]) {
                    if (all_will_be_matched) {
                        lexer->result_symbol = PIPE_TABLE_LINE_ENDING;
                        return true;
                    }
                } else {
                    lexer->result_symbol = SOFT_LINE_ENDING;
                    // reset some state variables
                    s->state |= STATE_WAS_SOFT_LINE_BREAK;
                    return true;
                }
            } else {
                s->matched = matched_temp;
            }
            s->indentation = 0;
            s->column = 0;
        }
        if (valid_symbols[LINE_ENDING]) {
            // If the last line break ended a paragraph and no new block opened,
            // the last line break should have been a soft line break Reset the
            // counter for matched blocks
            s->matched = 0;
            // If there is at least one open block, we should be in the matching
            // state. Also set the matching flag if a
            // `$._soft_line_break_marker` can be emitted so it does get
            // emitted.
            if (s->open_blocks.size > 0) {
                s->state |= STATE_MATCHING;
            } else {
                s->state &= (~STATE_MATCHING);
            }
            // reset some state variables
            s->state &= (~STATE_WAS_SOFT_LINE_BREAK);
            lexer->result_symbol = LINE_ENDING;
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length)
void *tree_sitter_markdown_external_scanner_create(void) {
    Scanner *s = ts_malloc(sizeof(Scanner));
    if (s == NULL) {
        return NULL;
    }
    s->open_blocks.items = NULL;
    s->open_blocks.capacity = 0;
    deserialize(s, NULL, 0);
    return s;
}
// NOLINTEND(readability-identifier-length)

// NOLINTNEXTLINE(readability-identifier-length)
bool tree_sitter_markdown_external_scanner_scan(void *payload, TSLexer *lexer,
                                                const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    scanner->simulate = false;
    return scan(scanner, lexer, valid_symbols);
}

// NOLINTNEXTLINE(readability-identifier-length)
unsigned tree_sitter_markdown_external_scanner_serialize(void *payload,
                                                         char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, buffer);
}

// NOLINTNEXTLINE(readability-identifier-length)
void tree_sitter_markdown_external_scanner_deserialize(void *payload,
                                                       const char *buffer,
                                                       unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, buffer, length);
}

// NOLINTNEXTLINE(readability-identifier-length)
void tree_sitter_markdown_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    ts_free(scanner->open_blocks.items);
    ts_free(scanner);
}
