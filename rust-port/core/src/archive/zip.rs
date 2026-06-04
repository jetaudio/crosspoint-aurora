//! Minimal read-only ZIP reader for EPUB containers (zero-allocation).
//!
//! EPUB is a ZIP of XHTML. This finds an entry in the central directory and
//! extracts it — STORED entries are copied, DEFLATE entries go through
//! [`crate::compress::inflate`] straight into the caller's buffer. Operates on a
//! whole-archive `&[u8]`; the firmware reads a (small) EPUB into a buffer and
//! calls in here.
//!
//! Spine ordering (reading order from the OPF) is a future refinement; for now
//! [`extract_first_html`] returns the first XHTML/HTML entry, which is the first
//! readable chapter for most simple EPUBs.

use crate::compress::inflate;

const SIG_EOCD: u32 = 0x0605_4b50;
const SIG_CENTRAL: u32 = 0x0201_4b50;
const SIG_LOCAL: u32 = 0x0403_4b50;

const METHOD_STORED: u16 = 0;
const METHOD_DEFLATE: u16 = 8;

#[derive(Debug, PartialEq, Eq)]
pub enum ZipError {
    NotZip,
    NotFound,
    Unsupported,
    Corrupt,
    Inflate,
}

fn rd_u16(b: &[u8], at: usize) -> Option<u16> {
    let s = b.get(at..at + 2)?;
    Some(u16::from_le_bytes([s[0], s[1]]))
}
fn rd_u32(b: &[u8], at: usize) -> Option<u32> {
    let s = b.get(at..at + 4)?;
    Some(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}

/// Locate the End-Of-Central-Directory record, scanning back from the end.
fn find_eocd(zip: &[u8]) -> Option<usize> {
    if zip.len() < 22 {
        return None;
    }
    // EOCD is 22 bytes + a comment (usually empty); scan the last 64 KB+22.
    let scan_from = zip.len().saturating_sub(22 + 0xFFFF);
    for at in (scan_from..=zip.len() - 22).rev() {
        if rd_u32(zip, at) == Some(SIG_EOCD) {
            return Some(at);
        }
    }
    None
}

/// A located central-directory entry.
struct Entry {
    method: u16,
    comp_size: usize,
    local_offset: usize,
}

/// Find the first central-directory entry whose name passes `pred`.
fn find_entry<F: Fn(&[u8]) -> bool>(zip: &[u8], pred: F) -> Result<Entry, ZipError> {
    let eocd = find_eocd(zip).ok_or(ZipError::NotZip)?;
    let count = rd_u16(zip, eocd + 10).ok_or(ZipError::Corrupt)?;
    let mut at = rd_u32(zip, eocd + 16).ok_or(ZipError::Corrupt)? as usize;

    for _ in 0..count {
        if rd_u32(zip, at) != Some(SIG_CENTRAL) {
            return Err(ZipError::Corrupt);
        }
        let method = rd_u16(zip, at + 10).ok_or(ZipError::Corrupt)?;
        let comp_size = rd_u32(zip, at + 20).ok_or(ZipError::Corrupt)? as usize;
        let name_len = rd_u16(zip, at + 28).ok_or(ZipError::Corrupt)? as usize;
        let extra_len = rd_u16(zip, at + 30).ok_or(ZipError::Corrupt)? as usize;
        let comment_len = rd_u16(zip, at + 32).ok_or(ZipError::Corrupt)? as usize;
        let local_offset = rd_u32(zip, at + 42).ok_or(ZipError::Corrupt)? as usize;
        let name = zip
            .get(at + 46..at + 46 + name_len)
            .ok_or(ZipError::Corrupt)?;

        if pred(name) {
            return Ok(Entry {
                method,
                comp_size,
                local_offset,
            });
        }
        at += 46 + name_len + extra_len + comment_len;
    }
    Err(ZipError::NotFound)
}

/// Extract `entry`'s bytes into `out`. Returns the decompressed length.
fn extract(zip: &[u8], entry: &Entry, out: &mut [u8]) -> Result<usize, ZipError> {
    let lo = entry.local_offset;
    if rd_u32(zip, lo) != Some(SIG_LOCAL) {
        return Err(ZipError::Corrupt);
    }
    // The local header repeats name/extra lengths (may differ from central).
    let name_len = rd_u16(zip, lo + 26).ok_or(ZipError::Corrupt)? as usize;
    let extra_len = rd_u16(zip, lo + 28).ok_or(ZipError::Corrupt)? as usize;
    let data_at = lo + 30 + name_len + extra_len;
    let comp = zip
        .get(data_at..data_at + entry.comp_size)
        .ok_or(ZipError::Corrupt)?;

    match entry.method {
        METHOD_STORED => {
            if comp.len() > out.len() {
                return Err(ZipError::Corrupt);
            }
            out[..comp.len()].copy_from_slice(comp);
            Ok(comp.len())
        }
        METHOD_DEFLATE => inflate(comp, out).map_err(|_| ZipError::Inflate),
        _ => Err(ZipError::Unsupported),
    }
}

/// True if `name` ends with an HTML/XHTML extension (case-insensitive).
fn is_html_name(name: &[u8]) -> bool {
    let ends = |suffix: &[u8]| {
        name.len() >= suffix.len()
            && name[name.len() - suffix.len()..]
                .iter()
                .zip(suffix)
                .all(|(a, b)| a.to_ascii_lowercase() == *b)
    };
    ends(b".xhtml") || ends(b".html") || ends(b".htm")
}

/// Extract the first XHTML/HTML entry of an EPUB/ZIP into `out`, returning its
/// decompressed length. The bytes are raw (X)HTML — run them through
/// [`crate::parser::html::extract_text_inplace`] to get readable text.
pub fn extract_first_html(zip: &[u8], out: &mut [u8]) -> Result<usize, ZipError> {
    let entry = find_entry(zip, is_html_name)?;
    extract(zip, &entry, out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::vec::Vec;

    // Build a minimal ZIP with one entry, choosing STORED or DEFLATE.
    fn build_zip(name: &[u8], content: &[u8], deflate: bool) -> Vec<u8> {
        use flate2::write::DeflateEncoder;
        use flate2::Compression;
        use std::io::Write;

        let (method, stored): (u16, Vec<u8>) = if deflate {
            let mut enc = DeflateEncoder::new(Vec::new(), Compression::new(6));
            enc.write_all(content).unwrap();
            (8, enc.finish().unwrap())
        } else {
            (0, content.to_vec())
        };

        let mut z = Vec::new();
        let put16 = |z: &mut Vec<u8>, v: u16| z.extend_from_slice(&v.to_le_bytes());
        let put32 = |z: &mut Vec<u8>, v: u32| z.extend_from_slice(&v.to_le_bytes());

        // Local file header.
        let local_offset = z.len() as u32;
        put32(&mut z, SIG_LOCAL);
        put16(&mut z, 20); // version
        put16(&mut z, 0); // flags
        put16(&mut z, method);
        put16(&mut z, 0); // time
        put16(&mut z, 0); // date
        put32(&mut z, 0); // crc (unchecked by our reader)
        put32(&mut z, stored.len() as u32);
        put32(&mut z, content.len() as u32);
        put16(&mut z, name.len() as u16);
        put16(&mut z, 0); // extra
        z.extend_from_slice(name);
        z.extend_from_slice(&stored);

        // Central directory.
        let cd_offset = z.len() as u32;
        put32(&mut z, SIG_CENTRAL);
        put16(&mut z, 20);
        put16(&mut z, 20);
        put16(&mut z, 0);
        put16(&mut z, method);
        put16(&mut z, 0);
        put16(&mut z, 0);
        put32(&mut z, 0);
        put32(&mut z, stored.len() as u32);
        put32(&mut z, content.len() as u32);
        put16(&mut z, name.len() as u16);
        put16(&mut z, 0); // extra
        put16(&mut z, 0); // comment
        put16(&mut z, 0); // disk
        put16(&mut z, 0); // int attr
        put32(&mut z, 0); // ext attr
        put32(&mut z, local_offset);
        z.extend_from_slice(name);
        let cd_size = z.len() as u32 - cd_offset;

        // EOCD.
        put32(&mut z, SIG_EOCD);
        put16(&mut z, 0);
        put16(&mut z, 0);
        put16(&mut z, 1); // entries this disk
        put16(&mut z, 1); // entries total
        put32(&mut z, cd_size);
        put32(&mut z, cd_offset);
        put16(&mut z, 0); // comment len
        z
    }

    #[test]
    fn extracts_deflate_entry() {
        let html = b"<html><body><p>Chapter one.</p></body></html>";
        let zip = build_zip(b"OEBPS/chapter1.xhtml", html, true);
        let mut out = std::vec![0u8; 256];
        let n = extract_first_html(&zip, &mut out).unwrap();
        assert_eq!(&out[..n], html);
    }

    #[test]
    fn extracts_stored_entry() {
        let html = b"<p>stored</p>";
        let zip = build_zip(b"index.html", html, false);
        let mut out = std::vec![0u8; 64];
        let n = extract_first_html(&zip, &mut out).unwrap();
        assert_eq!(&out[..n], html);
    }

    #[test]
    fn missing_html_errors() {
        let zip = build_zip(b"mimetype", b"application/epub+zip", false);
        let mut out = std::vec![0u8; 64];
        assert_eq!(extract_first_html(&zip, &mut out), Err(ZipError::NotFound));
    }

    #[test]
    fn not_a_zip_errors() {
        let mut out = std::vec![0u8; 16];
        assert_eq!(
            extract_first_html(b"not a zip", &mut out),
            Err(ZipError::NotZip)
        );
    }
}
