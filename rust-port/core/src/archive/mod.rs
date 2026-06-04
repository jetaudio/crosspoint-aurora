//! Archive containers (zero-allocation, read-only).

pub mod zip;

pub use zip::{extract_first_html, ZipError};
