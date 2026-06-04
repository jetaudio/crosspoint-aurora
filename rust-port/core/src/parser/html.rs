//! Minimal HTML/XHTML → plain-text extraction, in place and zero-allocation.
//!
//! EPUB book content is XHTML, so turning markup into readable, paginatable text
//! is the core of HTML/EPUB support (minus the ZIP container). This strips tags,
//! decodes the common entities, collapses runs of whitespace, and turns
//! block-level tags into blank-line paragraph breaks so the existing
//! [`crate::parser::TextReader`] / [`crate::layout`] pipeline can flow it.
//!
//! Extraction is **in place**: the output is always ≤ the input (tags and
//! entities only shrink), so we rewrite the front of the same buffer and return
//! the new length — no second 32 KB buffer needed on the C3.

/// True for block-level tags that should become a paragraph break.
fn is_block_tag(name: &[u8]) -> bool {
    matches!(
        name,
        b"p" | b"br"
            | b"div"
            | b"h1"
            | b"h2"
            | b"h3"
            | b"h4"
            | b"h5"
            | b"h6"
            | b"li"
            | b"ul"
            | b"ol"
            | b"tr"
            | b"hr"
            | b"blockquote"
            | b"section"
            | b"article"
    )
}

/// Decode an entity body (the text between `&` and `;`) to a single byte, if we
/// recognise it. Codepoints above ASCII collapse to `?` (the reader font is
/// ASCII). Returns `None` for unknown entities (caller drops them).
fn decode_entity(body: &[u8]) -> Option<u8> {
    match body {
        b"amp" => Some(b'&'),
        b"lt" => Some(b'<'),
        b"gt" => Some(b'>'),
        b"quot" => Some(b'"'),
        b"apos" => Some(b'\''),
        b"nbsp" => Some(b' '),
        b"mdash" | b"ndash" => Some(b'-'),
        b"hellip" => Some(b'.'),
        b"lsquo" | b"rsquo" => Some(b'\''),
        b"ldquo" | b"rdquo" => Some(b'"'),
        _ => {
            // Numeric: &#NN; (decimal) or &#xHH; (hex).
            if let Some(rest) = body.strip_prefix(b"#") {
                let (radix, digits) = match rest.split_first() {
                    Some((b'x' | b'X', d)) => (16u32, d),
                    _ => (10u32, rest),
                };
                let mut cp: u32 = 0;
                if digits.is_empty() {
                    return None;
                }
                for &d in digits {
                    let v = (d as char).to_digit(radix)?;
                    cp = cp.checked_mul(radix)?.checked_add(v)?;
                }
                return Some(if cp < 0x80 { cp as u8 } else { b'?' });
            }
            None
        }
    }
}

/// Extract plain text from `buf[..len]` in place. Returns the new length.
pub fn extract_text_inplace(buf: &mut [u8], len: usize) -> usize {
    let len = len.min(buf.len());
    let mut i = 0; // read cursor
    let mut w = 0; // write cursor (always <= i)
    let mut pending_break = false; // a block tag was seen
    let mut pending_space = false; // whitespace was seen
    let mut wrote_any = false;

    // Emit the queued separator (paragraph break wins over a plain space) before
    // the next real character.
    macro_rules! flush_sep {
        () => {
            if pending_break {
                if wrote_any {
                    buf[w] = b'\n';
                    w += 1;
                    buf[w] = b'\n';
                    w += 1;
                }
                pending_break = false;
                pending_space = false;
            } else if pending_space {
                if wrote_any {
                    buf[w] = b' ';
                    w += 1;
                }
                pending_space = false;
            }
        };
    }

    while i < len {
        let c = buf[i];
        match c {
            b'<' => {
                // Read the tag name (letters after '<' or '</').
                let mut j = i + 1;
                if j < len && buf[j] == b'/' {
                    j += 1;
                }
                let name_start = j;
                while j < len && buf[j].is_ascii_alphanumeric() {
                    j += 1;
                }
                let name = &buf[name_start..j];
                let mut lower = [0u8; 12];
                let nl = name.len().min(lower.len());
                for k in 0..nl {
                    lower[k] = name[k].to_ascii_lowercase();
                }
                if is_block_tag(&lower[..nl]) {
                    pending_break = true;
                }
                // Skip to the closing '>'.
                while i < len && buf[i] != b'>' {
                    i += 1;
                }
                i += 1; // past '>'
            }
            b'&' => {
                // Read up to ';' (bounded).
                let mut j = i + 1;
                while j < len && buf[j] != b';' && j - i <= 10 {
                    j += 1;
                }
                if j < len && buf[j] == b';' {
                    if let Some(byte) = decode_entity(&buf[i + 1..j]) {
                        if byte == b' ' {
                            pending_space = true;
                        } else {
                            flush_sep!();
                            buf[w] = byte;
                            w += 1;
                            wrote_any = true;
                        }
                    }
                    i = j + 1;
                } else {
                    // Not an entity — treat '&' literally.
                    flush_sep!();
                    buf[w] = b'&';
                    w += 1;
                    wrote_any = true;
                    i += 1;
                }
            }
            b' ' | b'\t' | b'\r' | b'\n' => {
                pending_space = true;
                i += 1;
            }
            _ => {
                flush_sep!();
                buf[w] = c;
                w += 1;
                wrote_any = true;
                i += 1;
            }
        }
    }
    w
}

#[cfg(test)]
mod tests {
    use super::*;

    // Helper: run the extractor on a heap copy and return it as a String.
    fn run(html: &str) -> std::string::String {
        let mut buf = html.as_bytes().to_vec();
        let n = extract_text_inplace(&mut buf, html.len());
        std::string::String::from_utf8_lossy(&buf[..n]).into_owned()
    }

    #[test]
    fn strips_tags_and_collapses_space() {
        assert_eq!(run("<p>Hello   <b>world</b></p>"), "Hello world");
    }

    #[test]
    fn block_tags_become_paragraph_breaks() {
        assert_eq!(run("<p>One</p><p>Two</p>"), "One\n\nTwo");
        assert_eq!(run("A<br>B"), "A\n\nB");
    }

    #[test]
    fn decodes_entities() {
        assert_eq!(
            run("Tom &amp; Jerry &lt;3 &#65;&#x42;"),
            "Tom & Jerry <3 AB"
        );
        assert_eq!(run("a&nbsp;b"), "a b");
    }

    #[test]
    fn leading_breaks_do_not_emit() {
        // No paragraph break before the very first text.
        assert_eq!(run("<html><body><p>Hi</p></body></html>"), "Hi");
    }
}
