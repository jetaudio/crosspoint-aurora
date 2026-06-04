//! CrossPoint core — pure `no_std` logic shared by the firmware.
//!
//! No esp-hal here: the e-ink driver is generic over `embedded-hal` traits and
//! every parser/layout/input routine is plain slice/arithmetic code, so this
//! crate builds and runs its unit tests on the host.
#![no_std]
// Forward-facing public API (font tables, page metrics, X3 constants) is not all
// consumed by the firmware yet as the C++ port progresses.
#![allow(dead_code)]

#[cfg(test)]
extern crate std;

pub mod driver;
pub mod input;
pub mod layout;
pub mod parser;
pub mod pins;
pub mod ui;
