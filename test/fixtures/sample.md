---
title: Sample Document
author: Ophidiarium
---

# Top-level heading

This is a paragraph with **bold** and *italic* text, plus a [link](https://example.com).

## Second-level heading

Setext-style heading
====================

Another setext
--------------

### Lists

- unordered one
- unordered two
  - nested item
  - [x] completed task
  - [ ] open task

1. ordered one
2. ordered two

### Code blocks

An indented block:

    indented code line 1
    indented code line 2

A fenced block with language:

```go
package main

func main() {
    println("hello")
}
```

A fenced block without language:

~~~
plain text
multiple lines
~~~

### Block quotes

> a block quote
> with multiple lines
>
> > nested block quote

### Thematic break

---

### Pipe table

| Col A | Col B | Col C |
|:------|:-----:|------:|
| a1    | b1    | c1    |
| a2    | b2    | c2    |

### Link reference

[example][example-ref]

[example-ref]: https://example.com "Example Site"

### HTML

<div class="note">A block html element.</div>

<!-- this is an HTML block comment -->

### Trailing paragraph

Final paragraph to close the document.
