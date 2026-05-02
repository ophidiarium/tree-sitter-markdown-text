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

### Footnotes

Here is a paragraph with a footnote reference[^note].

[^note]: This is the footnote definition body.

### Math block

$$
E = mc^2
$$

### Directive block

:::note
This is a note directive block.
:::

### Image block

![Alt text](./images/diagram.png)

### HTML

<div class="note">A block html element.</div>

<!-- this is an HTML block comment -->

### Callouts (GFM alerts)

> [!NOTE]
> This is a note callout.

> [!WARNING] inline warning text
> body line

> A plain block quote (not a callout).

### MDX JSX block

<Component prop="value" />

<MyBlock>
body
</MyBlock>

### Inline structural nodes

This paragraph has *emphasis*, **strong**, ~~strikethrough~~, and `inline code`.

It also has a [link](https://example.com), an ![image](./pic.png), an autolink <https://example.com>, and an email <user@example.com>.

Reference docs: see camelCase, snake_case_id, version 1.2.3, and path src/lib.rs.

Math like $E=mc^2$ inline and a footnote ref[^s1].

Raw HTML: <span>inline</span> and MDX like <Name prop="x"/> or {expr}.

[^s1]: footnote body.

### Trailing paragraph

Final paragraph to close the document.
