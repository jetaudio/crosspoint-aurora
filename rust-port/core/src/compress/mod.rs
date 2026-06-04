//! Decompression (zero-allocation).

pub mod inflate;

pub use inflate::{inflate, InflateError};
