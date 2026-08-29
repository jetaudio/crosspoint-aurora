#pragma once

#include <cstdint>

// Append-only battery telemetry log.
//
// The point of this is to answer "where does the charge actually go?" from real
// use rather than from bench measurements. The fuel gauge sits in series with
// the pack, so it can only ever report the TOTAL current -- there is no
// per-component sensing anywhere on this board. Attribution has to come from
// correlation instead: log the measured charge drop for an interval alongside
// the state the device was in during that interval, collect a day or two of
// ordinary reading, then solve for each component's cost offline.
//
// Hence the split of responsibilities: the device records facts, the PC does
// the inference. No regression runs here. A least-squares fit needs its
// conditioning inspected before its answers mean anything -- two loads that are
// always on together (the CPU floor and the always-scanning GT911, today) are
// perfectly collinear and cannot be separated by any amount of data. That is a
// judgement to make while looking at the design matrix, not something to bury
// in firmware that only ever emits a confident-looking number.
//
// Row cadence is a compromise. RemainingCapacity has 1 mAh granularity, so at
// ~40 mA a five-minute row drops only ~3 mAh: three quantisation levels, +-1 of
// noise. Longer intervals measure better; shorter ones resolve behaviour
// changes better. Five minutes keeps two days of logging to ~70 KB while still
// giving a usable signal once many rows are stacked.
namespace BatteryLog {

// Reads the gauge and starts a session. Safe to call when no SD card is
// present -- logging simply stays off.
void begin();

// Cheap; call every main-loop pass. Samples on its own schedule.
void tick();

// Force a row out now. Used before deep sleep, where the SD card is about to be
// unmounted and RAM is about to be lost.
void flushNow(const char* event);

// Count an e-paper refresh. Each one is a current spike (the panel PMIC pumping
// its rails), so it belongs in the model as energy-per-event rather than as
// milliamps-times-seconds like every other load.
void noteDisplayRefresh();

// Time actually spent halted in light sleep. Since the idle loop started
// sleeping between page turns this is the single largest term in the model,
// and it is invisible to accumulate(): the CPU is stopped, so no main-loop
// pass credits those milliseconds to anything. Without this column the sleep
// time lands in the residual and drags every other coefficient with it.
void noteLightSleep(uint32_t ms);

// Count a page turn. A row's page count says how much of its interval was
// "reading" rather than "idle with the page up", which is what separates the
// per-turn cost (render + refresh + the clock ramp behind them) from the
// standing floor. Refresh count alone can't: menus and popups refresh too.
void notePageTurn();

// Mark the start of a sleep gap. Records the wall clock, which — unlike
// millis() — keeps running across deep sleep, so the next boot can tell how long
// the device was actually away and charge that time to sleep rather than
// silently dropping it. Call from the sleep path, after the last row is written.
void noteSleepEntry();

// --- since-last-charge accounting -------------------------------------------
// The same facts the CSV carries, but anchored to the moment the cable came out
// rather than to a cold boot, and readable on-device. The gauge gives the only
// hard number here (charge actually spent); everything else is the state the
// device was in while spending it, which is what turns one number into an
// answer about where it went.
//
// Counters live in RTC_NOINIT and are re-anchored whenever external power is
// present, so "since last charge" survives every sleep in between.
struct Usage {
  bool valid = false;      // an anchor has been taken
  bool onCharger = false;  // external power right now, so the totals are frozen
  uint16_t startRemCapMah = 0;
  uint16_t remCapMah = 0;
  uint16_t fccMah = 0;
  uint16_t socPct = 0;
  uint32_t elapsedS = 0;  // wall time since the anchor, sleep included
  uint32_t runMs = 0;     // wall time with the CPU booted (light sleep included)
  uint32_t lightSleepMs = 0;
  uint32_t deepSleepMs = 0;
  uint32_t hiClockMs = 0;
  uint32_t wifiMs = 0;
  uint32_t frontlightPctMs = 0;  // integral of brightness% x ms
  uint32_t refreshes = 0;
  uint32_t pageTurns = 0;
  uint32_t sleepStallMs = 0;  // time lost polling a power line that would not release
  uint32_t sleepStalls = 0;   // how many sleeps hit the release-poll ceiling
  bool lastParkOk = true;     // did the last sleep park the panel PMIC
};
Usage usage();

}  // namespace BatteryLog
