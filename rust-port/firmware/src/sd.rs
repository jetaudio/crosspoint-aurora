//! SD-card access over the shared SPI bus (FAT via `embedded-sdmmc`).
//!
//! Ports the role of the C++ `SDCardManager`: bring up the card on SPI2 (CS=12,
//! shared SCLK/MOSI/MISO with the display) and read a book file into a buffer.
//!
//! NOTE (hardware): the shared bus runs at the display's 40 MHz, matching the
//! C++ `SDCardManager` (`SPI_FQ = 40_000_000`). Some SD cards want a ≤400 kHz
//! init phase; if a card fails to enumerate on real hardware, that bus-speed
//! split is the first thing to revisit. Everything here degrades gracefully —
//! any failure returns 0 bytes and the caller falls back to the bundled sample.

use embedded_hal::delay::DelayNs;
use embedded_hal::spi::SpiDevice;
use embedded_sdmmc::{
    Mode, SdCard, ShortFileName, TimeSource, Timestamp, VolumeIdx, VolumeManager,
};

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

/// Mount volume 0, find the first regular file in the root directory, and read
/// up to `buf.len()` bytes of it into `buf`. Returns the number of bytes read
/// (0 on any error — no card, no FAT volume, empty root, or a read failure).
///
/// Consumes the SPI device so its shared-bus borrow is released when we return.
pub fn load_first_book<SPI, D>(spi: SPI, delay: D, buf: &mut [u8]) -> usize
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

    // Capture the first non-directory entry's name.
    let mut chosen: Option<ShortFileName> = None;
    if root
        .iterate_dir(|entry| {
            if chosen.is_none() && !entry.attributes.is_directory() {
                chosen = Some(entry.name.clone());
            }
        })
        .is_err()
    {
        return 0;
    }

    let name = match chosen {
        Some(n) => n,
        None => return 0,
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
