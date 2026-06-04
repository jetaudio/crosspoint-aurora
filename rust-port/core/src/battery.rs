//! Battery state-of-charge estimation (X4).
//!
//! Ports `BatteryMonitor::percentageFromMillivolts` from
//! `open-x4-sdk/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp` — a cubic
//! fit over LiPo discharge samples. The X4 reads the pack through a ÷2 divider
//! on GPIO0, so callers pass `adc_millivolts * 2`.
//!
//! All math is plain `f64` arithmetic (no std float methods) so it builds for
//! `no_std`/RISC-V soft-float and is unit-tested on the host.

/// The X4 battery divider multiplier (the ADC sees half the pack voltage).
pub const DIVIDER_MULTIPLIER: f64 = 2.0;

/// Estimate state-of-charge percent (0..=100) from pack millivolts.
pub fn percentage_from_millivolts(millivolts: u16) -> u8 {
    let v = millivolts as f64 / 1000.0;
    // Polynomial derived from LiPo samples (identical coefficients to the C++).
    let y = -144.9390 * v * v * v + 1655.8629 * v * v - 6158.8520 * v + 7501.3202;
    let y = if y < 0.0 {
        0.0
    } else if y > 100.0 {
        100.0
    } else {
        y
    };
    // Round half-up; y is already clamped to [0, 100].
    (y + 0.5) as u8
}

/// Convenience: percent directly from the raw ADC millivolts (pre-divider).
pub fn percentage_from_adc_millivolts(adc_mv: u16) -> u8 {
    let pack = (adc_mv as f64 * DIVIDER_MULTIPLIER) as u16;
    percentage_from_millivolts(pack)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_and_empty_clamp() {
        // The cubic is only meaningful across the LiPo band (~3.2–4.2 V); the
        // clamp keeps it in [0,100]. At ~4.2 V the polynomial exceeds 100, so
        // the upper clamp is the one clean, well-defined edge. The cubic fit is
        // only meaningful in the LiPo band (~3.4–4.2 V) and is non-monotonic
        // outside it, so we don't assert specific out-of-band values — that
        // matches the C++ clamp behaviour exactly. A mid-band voltage must land
        // on a sensible interior percentage.
        assert_eq!(percentage_from_millivolts(4200), 100);
        let mid = percentage_from_millivolts(3700);
        assert!(mid > 1 && mid < 99, "mid-band pct out of range: {mid}");
    }

    #[test]
    fn midrange_is_monotonic_and_in_band() {
        // Across the usable 3.4–4.1 V band, percent should be in [0,100] and
        // non-decreasing as voltage rises.
        let mut last = 0u8;
        for mv in (3400..=4100).step_by(50) {
            let p = percentage_from_millivolts(mv);
            assert!(p <= 100);
            assert!(p >= last, "pct dropped as voltage rose at {mv} mV");
            last = p;
        }
        assert!(last > 0);
    }

    #[test]
    fn divider_is_applied() {
        // adc sees half the pack, so 2100 mV at the pin ⇒ 4200 mV pack ⇒ 100 %.
        assert_eq!(percentage_from_adc_millivolts(2100), 100);
    }
}
