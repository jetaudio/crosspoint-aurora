//! Zero-allocation DEFLATE (RFC 1951) decompressor.
//!
//! A faithful Rust port of zlib's reference `puff.c`. Decompression writes
//! straight into the caller's output buffer; LZ77 back-references copy from the
//! data already written there, so the output buffer doubles as the 32 KB sliding
//! window and **no heap or scratch allocation is needed** — which is exactly
//! what lets EPUB (DEFLATE-compressed XHTML) fit the firmware's zero-alloc rule.
//!
//! This is the decompressor half of EPUB support; the ZIP container parser
//! ([`crate::archive::zip`]) locates the compressed bytes and calls in here.

const MAX_BITS: usize = 15;
const MAX_L_CODES: usize = 286;
const MAX_D_CODES: usize = 30;
const FIX_L_CODES: usize = 288;

/// Length code base values / extra bits for symbols 257..=285.
const LEN_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const LEN_EXTRA: [u8; 29] = [
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
/// Distance code base values / extra bits for symbols 0..=29.
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DIST_EXTRA: [u8; 30] = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13,
];
/// Order in which the dynamic-block code-length code lengths arrive.
const CLEN_ORDER: [usize; 19] = [
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
];

#[derive(Debug, PartialEq, Eq)]
pub enum InflateError {
    /// Ran out of input bits.
    Truncated,
    /// Output buffer too small.
    Overflow,
    /// Invalid block / Huffman / back-reference.
    Invalid,
}

struct BitReader<'a> {
    data: &'a [u8],
    /// Bit cursor (absolute bit index into `data`).
    bit: usize,
}

impl<'a> BitReader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, bit: 0 }
    }

    /// Read `n` bits LSB-first (n ≤ 25).
    fn bits(&mut self, n: u32) -> Result<u32, InflateError> {
        let mut val: u32 = 0;
        for i in 0..n {
            let byte = self.bit >> 3;
            if byte >= self.data.len() {
                return Err(InflateError::Truncated);
            }
            let b = (self.data[byte] >> (self.bit & 7)) & 1;
            val |= (b as u32) << i;
            self.bit += 1;
        }
        Ok(val)
    }

    fn align_to_byte(&mut self) {
        self.bit = (self.bit + 7) & !7;
    }
}

/// Canonical Huffman table (counts + symbol list), puff.c style.
struct Huffman {
    counts: [u16; MAX_BITS + 1],
    symbols: [u16; FIX_L_CODES],
}

impl Huffman {
    fn empty() -> Self {
        Self {
            counts: [0; MAX_BITS + 1],
            symbols: [0; FIX_L_CODES],
        }
    }

    /// Build from per-symbol code lengths.
    fn construct(lengths: &[u8]) -> Result<Self, InflateError> {
        let mut h = Huffman::empty();
        for &l in lengths {
            h.counts[l as usize] += 1;
        }
        // All-zero length table is allowed (e.g. an absent distance tree).
        if h.counts[0] as usize == lengths.len() {
            return Ok(h);
        }
        // Check for an over-subscribed or incomplete code.
        let mut left: i32 = 1;
        for len in 1..=MAX_BITS {
            left <<= 1;
            left -= h.counts[len] as i32;
            if left < 0 {
                return Err(InflateError::Invalid);
            }
        }
        // Offsets into the symbol table per length.
        let mut offs = [0u16; MAX_BITS + 2];
        for len in 1..=MAX_BITS {
            offs[len + 1] = offs[len] + h.counts[len];
        }
        for (sym, &len) in lengths.iter().enumerate() {
            if len != 0 {
                h.symbols[offs[len as usize] as usize] = sym as u16;
                offs[len as usize] += 1;
            }
        }
        Ok(h)
    }

    /// Decode one symbol (puff.c walk).
    fn decode(&self, br: &mut BitReader) -> Result<u16, InflateError> {
        let mut code: i32 = 0;
        let mut first: i32 = 0;
        let mut index: i32 = 0;
        for len in 1..=MAX_BITS {
            code |= br.bits(1)? as i32;
            let count = self.counts[len] as i32;
            if code - first < count {
                return Ok(self.symbols[(index + (code - first)) as usize]);
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        Err(InflateError::Invalid)
    }
}

/// Decompress a raw DEFLATE stream into `out`. Returns the number of bytes
/// written. `out` must be large enough for the full decompressed content.
pub fn inflate(input: &[u8], out: &mut [u8]) -> Result<usize, InflateError> {
    let mut br = BitReader::new(input);
    let mut pos = 0usize; // write position in `out`

    loop {
        let last = br.bits(1)?;
        let btype = br.bits(2)?;
        match btype {
            0 => stored(&mut br, out, &mut pos)?,
            1 => {
                let (lit, dist) = fixed_tables();
                inflate_block(&mut br, out, &mut pos, &lit, &dist)?;
            }
            2 => {
                let (lit, dist) = dynamic_tables(&mut br)?;
                inflate_block(&mut br, out, &mut pos, &lit, &dist)?;
            }
            _ => return Err(InflateError::Invalid),
        }
        if last == 1 {
            return Ok(pos);
        }
    }
}

fn stored(br: &mut BitReader, out: &mut [u8], pos: &mut usize) -> Result<(), InflateError> {
    br.align_to_byte();
    let len = br.bits(16)? as usize;
    let nlen = br.bits(16)?;
    if (len as u32) != (!nlen & 0xFFFF) {
        return Err(InflateError::Invalid);
    }
    for _ in 0..len {
        let b = br.bits(8)? as u8;
        if *pos >= out.len() {
            return Err(InflateError::Overflow);
        }
        out[*pos] = b;
        *pos += 1;
    }
    Ok(())
}

fn fixed_tables() -> (Huffman, Huffman) {
    let mut lit_lengths = [0u8; FIX_L_CODES];
    for (i, l) in lit_lengths.iter_mut().enumerate() {
        *l = match i {
            0..=143 => 8,
            144..=255 => 9,
            256..=279 => 7,
            _ => 8,
        };
    }
    let dist_lengths = [5u8; MAX_D_CODES];
    (
        Huffman::construct(&lit_lengths).unwrap(),
        Huffman::construct(&dist_lengths).unwrap(),
    )
}

fn dynamic_tables(br: &mut BitReader) -> Result<(Huffman, Huffman), InflateError> {
    let hlit = br.bits(5)? as usize + 257;
    let hdist = br.bits(5)? as usize + 1;
    let hclen = br.bits(4)? as usize + 4;
    if hlit > MAX_L_CODES || hdist > MAX_D_CODES {
        return Err(InflateError::Invalid);
    }

    // Code-length code lengths, in their shuffled order.
    let mut cl_lengths = [0u8; 19];
    for i in 0..hclen {
        cl_lengths[CLEN_ORDER[i]] = br.bits(3)? as u8;
    }
    let cl_huff = Huffman::construct(&cl_lengths)?;

    // Decode the literal/length + distance code lengths (one combined run).
    let mut lengths = [0u8; MAX_L_CODES + MAX_D_CODES];
    let total = hlit + hdist;
    let mut i = 0;
    while i < total {
        let sym = cl_huff.decode(br)?;
        match sym {
            0..=15 => {
                lengths[i] = sym as u8;
                i += 1;
            }
            16 => {
                // Repeat previous length 3..=6 times.
                if i == 0 {
                    return Err(InflateError::Invalid);
                }
                let prev = lengths[i - 1];
                let rep = br.bits(2)? + 3;
                for _ in 0..rep {
                    if i >= total {
                        return Err(InflateError::Invalid);
                    }
                    lengths[i] = prev;
                    i += 1;
                }
            }
            17 => {
                let rep = br.bits(3)? + 3;
                for _ in 0..rep {
                    if i >= total {
                        return Err(InflateError::Invalid);
                    }
                    lengths[i] = 0;
                    i += 1;
                }
            }
            18 => {
                let rep = br.bits(7)? + 11;
                for _ in 0..rep {
                    if i >= total {
                        return Err(InflateError::Invalid);
                    }
                    lengths[i] = 0;
                    i += 1;
                }
            }
            _ => return Err(InflateError::Invalid),
        }
    }

    let lit = Huffman::construct(&lengths[..hlit])?;
    let dist = Huffman::construct(&lengths[hlit..total])?;
    Ok((lit, dist))
}

fn inflate_block(
    br: &mut BitReader,
    out: &mut [u8],
    pos: &mut usize,
    lit: &Huffman,
    dist: &Huffman,
) -> Result<(), InflateError> {
    loop {
        let sym = lit.decode(br)?;
        if sym == 256 {
            return Ok(()); // end of block
        } else if sym < 256 {
            if *pos >= out.len() {
                return Err(InflateError::Overflow);
            }
            out[*pos] = sym as u8;
            *pos += 1;
        } else {
            // Length/distance pair.
            let s = sym as usize - 257;
            if s >= LEN_BASE.len() {
                return Err(InflateError::Invalid);
            }
            let len = LEN_BASE[s] as usize + br.bits(LEN_EXTRA[s] as u32)? as usize;
            let dsym = dist.decode(br)? as usize;
            if dsym >= DIST_BASE.len() {
                return Err(InflateError::Invalid);
            }
            let distance = DIST_BASE[dsym] as usize + br.bits(DIST_EXTRA[dsym] as u32)? as usize;
            if distance > *pos {
                return Err(InflateError::Invalid);
            }
            if *pos + len > out.len() {
                return Err(InflateError::Overflow);
            }
            // Copy from earlier output (the sliding window) — byte by byte so
            // overlapping copies (distance < len) replicate correctly.
            let mut src = *pos - distance;
            for _ in 0..len {
                out[*pos] = out[src];
                *pos += 1;
                src += 1;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::vec::Vec;

    // Compress with flate2 (raw deflate), decompress with ours, compare.
    fn roundtrip(data: &[u8]) {
        use flate2::write::DeflateEncoder;
        use flate2::Compression;
        use std::io::Write;

        for level in [0u32, 1, 6, 9] {
            let mut enc = DeflateEncoder::new(Vec::new(), Compression::new(level));
            enc.write_all(data).unwrap();
            let compressed = enc.finish().unwrap();

            let mut out = std::vec![0u8; data.len() + 16];
            let n =
                inflate(&compressed, &mut out).unwrap_or_else(|e| panic!("level {level}: {e:?}"));
            assert_eq!(&out[..n], data, "mismatch at level {level}");
        }
    }

    #[test]
    fn empty() {
        roundtrip(b"");
    }

    #[test]
    fn short_text() {
        roundtrip(b"Hello, world!");
    }

    #[test]
    fn repetitive_uses_backrefs() {
        roundtrip(b"abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc");
    }

    #[test]
    fn html_chapter() {
        let html = b"<html><body><p>It was the best of times, it was the worst of \
            times.</p><p>It was the age of wisdom, it was the age of foolishness.</p>\
            </body></html>";
        roundtrip(html);
    }

    #[test]
    fn larger_mixed() {
        let mut data = Vec::new();
        for i in 0..2000u32 {
            // Mix of repetition and variation to exercise both trees.
            let _ = core::fmt::Write::write_fmt(
                &mut Wrapper(&mut data),
                format_args!("line {} the quick brown fox\n", i % 50),
            );
        }
        roundtrip(&data);
    }

    // Tiny adapter so we can write formatted bytes into a Vec in the test.
    struct Wrapper<'a>(&'a mut Vec<u8>);
    impl core::fmt::Write for Wrapper<'_> {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            self.0.extend_from_slice(s.as_bytes());
            Ok(())
        }
    }
}
