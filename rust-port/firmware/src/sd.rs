//! SD-card access over the shared SPI bus (FAT via `embedded-sdmmc`).
//!
//! Ports the role of the C++ `SDCardManager`: bring up the card on SPI2 (CS=12,
//! shared SCLK/MOSI/MISO with the display), list the root directory, and read a
//! chosen book file into a buffer.
//!
//! NOTE (hardware): the shared bus runs at the display's 40 MHz, matching the
//! C++ `SDCardManager` (`SPI_FQ = 40_000_000`). Some SD cards want a ≤400 kHz
//! init phase; if a card fails to enumerate on real hardware, that bus-speed
//! split is the first thing to revisit. Everything here degrades gracefully —
//! any failure lists nothing / reads 0 bytes and the caller falls back to the
//! bundled sample.

use core::fmt::Write as _;

use crosspoint_core::ui::Menu;
use embedded_hal::delay::DelayNs;
use embedded_hal::spi::SpiDevice;
use embedded_sdmmc::{Mode, SdCard, TimeSource, Timestamp, VolumeIdx, VolumeManager};

/// No real-time clock on the X4 ADC path — FAT timestamps are not needed just to
/// read files, so hand the filesystem a fixed epoch.
pub struct NoClock;

impl TimeSource for NoClock {
    fn get_timestamp(&self) -> Timestamp {
        Timestamp {
            year_since_1970: 0,
            zero_indexed_month: 0,
            zero_indexed_day: 0,
            hours: 0,
            minutes: 0,
            seconds: 0,
        }
    }
}

/// Mount volume 0 and fill `menu` with the names of the regular files in the
/// root directory. Returns the number of files listed (0 on any error).
/// Consumes the SPI device so its shared-bus borrow is released on return.
pub fn list_files<SPI, D>(spi: SPI, delay: D, menu: &mut Menu) -> usize
where
    SPI: SpiDevice,
    D: DelayNs,
{
    menu.clear();
    let sdcard = SdCard::new(spi, delay);
    let volume_mgr = VolumeManager::new(sdcard, NoClock);
    let volume = match volume_mgr.open_volume(VolumeIdx(0)) {
        Ok(v) => v,
        Err(_) => return 0,
    };
    let root = match volume.open_root_dir() {
        Ok(d) => d,
        Err(_) => return 0,
    };

    let mut count = 0;
    let _ = root.iterate_dir(|entry| {
        if entry.attributes.is_directory() {
            return;
        }
        let mut name: heapless::String<16> = heapless::String::new();
        if write!(name, "{}", entry.name).is_ok() && menu.push(&name) {
            count += 1;
        }
    });
    count
}

/// Mount volume 0 and read up to `buf.len()` bytes of the named root file into
/// `buf`. Returns the number of bytes read (0 on any error). Consumes the SPI
/// device so its shared-bus borrow is released on return.
pub fn load_named<SPI, D>(spi: SPI, delay: D, name: &str, buf: &mut [u8]) -> usize
where
    SPI: SpiDevice,
    D: DelayNs,
{
    let sdcard = SdCard::new(spi, delay);
    let volume_mgr = VolumeManager::new(sdcard, NoClock);
    let volume = match volume_mgr.open_volume(VolumeIdx(0)) {
        Ok(v) => v,
        Err(_) => return 0,
    };
    let root = match volume.open_root_dir() {
        Ok(d) => d,
        Err(_) => return 0,
    };
    let file = match root.open_file_in_dir(name, Mode::ReadOnly) {
        Ok(f) => f,
        Err(_) => return 0,
    };

    let mut total = 0;
    while total < buf.len() && !file.is_eof() {
        match file.read(&mut buf[total..]) {
            Ok(0) => break,
            Ok(n) => total += n,
            Err(_) => break,
        }
    }
    total
}

/// Settings file name (8.3) in the SD root: line_height (u16 LE) + margin (u16 LE).
const SETTINGS_FILE: &str = "SETTINGS.CFG";

/// Load persisted reader settings `(line_height, margin)` from the SD root, or
/// `None` if absent / unreadable. Consumes the SPI device.
pub fn load_settings<SPI, D>(spi: SPI, delay: D) -> Option<(u16, u16)>
where
    SPI: SpiDevice,
    D: DelayNs,
{
    let sdcard = SdCard::new(spi, delay);
    let volume_mgr = VolumeManager::new(sdcard, NoClock);
    let volume = volume_mgr.open_volume(VolumeIdx(0)).ok()?;
    let root = volume.open_root_dir().ok()?;
    let file = root.open_file_in_dir(SETTINGS_FILE, Mode::ReadOnly).ok()?;
    let mut buf = [0u8; 4];
    let mut total = 0;
    while total < 4 && !file.is_eof() {
        match file.read(&mut buf[total..]) {
            Ok(0) => break,
            Ok(n) => total += n,
            Err(_) => return None,
        }
    }
    if total < 4 {
        return None;
    }
    Some((
        u16::from_le_bytes([buf[0], buf[1]]),
        u16::from_le_bytes([buf[2], buf[3]]),
    ))
}

/// Persist reader settings to the SD root. Returns true on success. Consumes the
/// SPI device.
pub fn save_settings<SPI, D>(spi: SPI, delay: D, line_height: u16, margin: u16) -> bool
where
    SPI: SpiDevice,
    D: DelayNs,
{
    let sdcard = SdCard::new(spi, delay);
    let volume_mgr = VolumeManager::new(sdcard, NoClock);
    let volume = match volume_mgr.open_volume(VolumeIdx(0)) {
        Ok(v) => v,
        Err(_) => return false,
    };
    let root = match volume.open_root_dir() {
        Ok(d) => d,
        Err(_) => return false,
    };
    let file = match root.open_file_in_dir(SETTINGS_FILE, Mode::ReadWriteCreateOrTruncate) {
        Ok(f) => f,
        Err(_) => return false,
    };
    let mut bytes = [0u8; 4];
    bytes[0..2].copy_from_slice(&line_height.to_le_bytes());
    bytes[2..4].copy_from_slice(&margin.to_le_bytes());
    let ok = file.write(&bytes).is_ok();
    // Closing flushes the directory entry + FAT; without it the write is lost.
    ok && file.close().is_ok()
}
