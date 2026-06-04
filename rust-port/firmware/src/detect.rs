//! Runtime X3-vs-X4 device detection by I²C fingerprint.
//!
//! Ports `HalGPIO.cpp`'s `detectDeviceTypeWithFingerprint`: the X3 carries three
//! I²C devices the X4 lacks (BQ27220 fuel gauge, DS3231 RTC, QMI8658 IMU). We
//! probe their signatures on the X3 I²C bus (SDA=20, SCL=0, 400 kHz) twice; if
//! both passes see ≥2 of the three, it's an X3. Generic over the `embedded-hal`
//! I²C trait so the bus is set up (and torn down) by the caller — important
//! because SCL shares GPIO0 with the X4 battery ADC.

use embedded_hal::delay::DelayNs;
use embedded_hal::i2c::I2c;

const I2C_BQ27220: u8 = 0x55; // fuel gauge
const I2C_DS3231: u8 = 0x68; // RTC
const I2C_QMI8658: u8 = 0x6B; // IMU
const I2C_QMI8658_ALT: u8 = 0x6A; // IMU fallback address

/// BQ27220: Voltage() (reg 0x08, mV, LE) in a plausible LiPo range.
fn probe_bq27220<I: I2c>(i2c: &mut I) -> bool {
    let mut b = [0u8; 2];
    if i2c.write_read(I2C_BQ27220, &[0x08], &mut b).is_err() {
        return false;
    }
    let mv = u16::from_le_bytes(b);
    (2500..=5000).contains(&mv)
}

/// DS3231: seconds (reg 0x00, BCD) — tens 0..5, ones 0..9.
fn probe_ds3231<I: I2c>(i2c: &mut I) -> bool {
    let mut b = [0u8; 1];
    if i2c.write_read(I2C_DS3231, &[0x00], &mut b).is_err() {
        return false;
    }
    let tens = (b[0] >> 4) & 0x07;
    let ones = b[0] & 0x0F;
    tens <= 5 && ones <= 9
}

/// QMI8658: WHO_AM_I (reg 0x00) == 0x05, at either address.
fn probe_qmi8658<I: I2c>(i2c: &mut I) -> bool {
    let mut b = [0u8; 1];
    for addr in [I2C_QMI8658, I2C_QMI8658_ALT] {
        if i2c.write_read(addr, &[0x00], &mut b).is_ok() && b[0] == 0x05 {
            return true;
        }
    }
    false
}

fn score<I: I2c>(i2c: &mut I) -> u8 {
    probe_bq27220(i2c) as u8 + probe_ds3231(i2c) as u8 + probe_qmi8658(i2c) as u8
}

/// Returns true if the I²C fingerprint indicates an X3 (≥2 devices in two
/// consecutive passes), matching the C++ confirmation rule. Inconclusive →
/// false (treat as X4, the conservative default).
pub fn is_x3<I: I2c, D: DelayNs>(i2c: &mut I, delay: &mut D) -> bool {
    let s1 = score(i2c);
    delay.delay_ms(2);
    let s2 = score(i2c);
    s1 >= 2 && s2 >= 2
}
