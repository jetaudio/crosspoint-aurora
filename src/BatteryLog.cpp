#include "BatteryLog.h"

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#if FREEINK_DEVICE_LILYGO
#include <BoardT5S3.h>
#endif
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>

#include <cstddef>
#include <cstring>

#include "CrossPointSettings.h"

namespace {

constexpr const char* LOG_PATH = "/.crosspoint/battery.csv";
constexpr const char* LOG_OLD_PATH = "/.crosspoint/battery-old.csv";
constexpr const char* CSV_HEADER =
    "uptime_s,rtc,event,remcap_mah,fcc_mah,mv,current_ma,soc,chrg_stat,pg,"
    "cpu_mhz,fl_on,fl_pct,fl_on_ms,wifi_on_ms,hi_clock_ms,lsleep_ms,refreshes,page_turns,rows,"
    "free_heap,dsleep_ms,stall_ms,stalls,park,rst,wake\n";
constexpr unsigned long SAMPLE_INTERVAL_MS = 60UL * 1000UL;
constexpr unsigned long ROW_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Accumulators have to survive deep sleep, which is a full power-down of RAM.
// RTC_NOINIT keeps them across that and across a reset, so a night spent asleep
// does not reset the refresh counter or lose the session's starting charge.
#define BATTERY_LOG_MAGIC 0x42544C47u  // 'BTLG'
RTC_NOINIT_ATTR uint32_t logMagic;
RTC_NOINIT_ATTR uint32_t logRefreshCount;
RTC_NOINIT_ATTR uint32_t logRowCount;
RTC_NOINIT_ATTR uint32_t logFrontlightOnMs;
RTC_NOINIT_ATTR uint32_t logWifiOnMs;
RTC_NOINIT_ATTR uint32_t logHighClockMs;
RTC_NOINIT_ATTR uint32_t logLightSleepMs;
RTC_NOINIT_ATTR uint32_t logPageTurns;
// Wall-clock microseconds captured at sleep entry. gettimeofday() is the only
// clock on this chip that keeps running through deep sleep (IDF carries the
// offset in RTC memory and adds it back on wake); millis() restarts at zero, so
// without this the hours spent asleep land nowhere and every per-hour figure
// derived from the log is wrong by however long the device was away.
RTC_NOINIT_ATTR uint64_t logSleepEntryUs;
RTC_NOINIT_ATTR uint32_t logDeepSleepMs;

// Since-last-charge accumulators. Same facts as above, different anchor: these
// reset every time external power shows up, which is the span a reader actually
// asks about ("what has this cost me since I unplugged it?").
//
// Elapsed time is a counter rather than an anchor on the wall clock: after a
// power cut gettimeofday() restarts near 1970, and an NTP sync later steps it
// forward by decades, so "now minus anchor" is only meaningful within one
// uninterrupted boot-and-sleep chain.
#define BATTERY_USE_MAGIC 0x42555332u  // 'BUS2'
RTC_NOINIT_ATTR uint32_t useMagic;
RTC_NOINIT_ATTR uint64_t useElapsedMs;
RTC_NOINIT_ATTR uint16_t useStartRemCap;
RTC_NOINIT_ATTR uint32_t useRunMs;
RTC_NOINIT_ATTR uint32_t useLightSleepMs;
RTC_NOINIT_ATTR uint32_t useDeepSleepMs;
RTC_NOINIT_ATTR uint32_t useHighClockMs;
RTC_NOINIT_ATTR uint32_t useWifiMs;
RTC_NOINIT_ATTR uint32_t useFrontlightPctMs;
RTC_NOINIT_ATTR uint32_t useRefreshes;
RTC_NOINIT_ATTR uint32_t usePageTurns;

// RTC_NOINIT survives deep sleep but not a power cut (Power Off's ship mode, a
// flat or unplugged pack), so the since-charge counters are also mirrored to the
// card and restored from there when a cold boot finds RTC memory empty.
constexpr const char* USAGE_PATH = "/.crosspoint/battery-usage.bin";
constexpr uint32_t USAGE_FILE_MAGIC = 0x46535542u;  // 'BUSF'
constexpr uint32_t USAGE_FILE_VERSION = 1;
// The gauge reads ~1 mAh noisy and recalibrates after a pack is reconnected. A
// reading this far ABOVE what was saved means the pack was charged (or swapped)
// while the device was off, so the old span no longer describes this charge.
constexpr uint16_t USAGE_RESTORE_CHARGE_TOLERANCE_MAH = 20;
// A gap longer than this between save and restore is treated as an RTC glitch.
constexpr uint32_t USAGE_MAX_OFF_GAP_S = 60UL * 24UL * 60UL * 60UL;

struct UsageSnapshot {
  uint32_t magic;
  uint32_t version;
  uint16_t startRemCap;
  uint16_t savedRemCap;  // gauge reading at save time; 0 = unread
  uint32_t rtcEpoch;     // RTC seconds at save time; 0 = no trustworthy RTC
  uint64_t elapsedMs;
  uint32_t runMs;
  uint32_t lightSleepMs;
  uint32_t deepSleepMs;
  uint32_t highClockMs;
  uint32_t wifiMs;
  uint32_t frontlightPctMs;
  uint32_t refreshes;
  uint32_t pageTurns;
  uint32_t checksum;  // over every byte before this field
};

unsigned long lastSampleMs = 0;
unsigned long lastRowMs = 0;
unsigned long lastAccumMs = 0;
bool headerChecked = false;

// The gauge lives on whichever I2C controller BoardConfig names.
TwoWire& gaugeWire() {
#if SOC_I2C_NUM > 1
  if (BoardConfig::ACTIVE.batteryGauge.i2cBus == 1) return Wire1;
#endif
  return Wire;
}

bool readGauge16(uint8_t reg, uint16_t& out) {
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  if (addr == 0) return false;
  TwoWire& bus = gaugeWire();
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return false;
  if (bus.requestFrom(static_cast<int>(addr), 2) != 2) return false;
  const uint8_t lo = bus.read();
  const uint8_t hi = bus.read();
  out = static_cast<uint16_t>(lo | (hi << 8));
  return true;
}

bool readCharger8(uint8_t reg, uint8_t& out) {
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.chargerAddr;
  if (addr == 0) return false;
  TwoWire& bus = gaugeWire();
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return false;
  if (bus.requestFrom(static_cast<int>(addr), 1) != 1) return false;
  out = bus.read();
  return true;
}

uint64_t wallUs() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + static_cast<uint64_t>(tv.tv_usec);
}

// External power present, charging or already topped off. Both count: a full
// pack on the cable is still not discharging, so the since-charge span has not
// started yet.
bool onExternalPower() { return gpio.isCharging() || gpio.isChargeComplete(); }

// Start (or restart) the since-last-charge span at the gauge's current reading.
void anchorUsage() {
  uint16_t remCap = 0;
  readGauge16(0x10, remCap);
  useMagic = BATTERY_USE_MAGIC;
  useElapsedMs = 0;
  useStartRemCap = remCap;
  useRunMs = 0;
  useLightSleepMs = 0;
  useDeepSleepMs = 0;
  useHighClockMs = 0;
  useWifiMs = 0;
  useFrontlightPctMs = 0;
  useRefreshes = 0;
  usePageTurns = 0;
}

// Time-weighted accumulators. MUST be called every main-loop pass, not on the
// sampling interval: this credits the whole elapsed slice to whatever state is
// observed right now, so sampling once a minute would attribute a full minute
// to an instant. That is fine for the frontlight, which changes slowly, and
// completely wrong for the CPU clock, which drops to 80 MHz three seconds after
// every page turn and jumps back for each render. First run of the log showed
// hi_clock_ms stuck at 0 for exactly that reason.
void accumulate() {
  const unsigned long now = millis();
  if (lastAccumMs == 0) {
    lastAccumMs = now;
    return;
  }
  const unsigned long dt = now - lastAccumMs;
  lastAccumMs = now;

  const bool frontlightOn = Frontlight.isOn();
  const bool wifiUp = WiFi.getMode() != WIFI_MODE_NULL;
  const bool highClock = getCpuFrequencyMhz() > 100;
  if (frontlightOn) logFrontlightOnMs += dt;
  if (wifiUp) logWifiOnMs += dt;
  if (highClock) logHighClockMs += dt;

  useRunMs += dt;
  useElapsedMs += dt;
  if (wifiUp) useWifiMs += dt;
  if (highClock) useHighClockMs += dt;
  // Brightness-weighted, because the frontlight is the one load whose cost
  // scales with a setting rather than with being on: 1% and 100% differ by two
  // orders of magnitude, and "hours the light was on" cannot tell them apart.
  if (frontlightOn) useFrontlightPctMs += dt * Frontlight.brightness();
}

uint32_t snapshotChecksum(const UsageSnapshot& snap) {
  // FNV-1a: enough to reject a torn or foreign file, and needs no table.
  const auto* bytes = reinterpret_cast<const uint8_t*>(&snap);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(UsageSnapshot, checksum); i++) {
    h ^= bytes[i];
    h *= 16777619u;
  }
  return h;
}

// Mirror the since-charge counters to the card. Called on the row cadence and
// on the way into sleep / power off, so a sudden power cut loses at most one
// row interval of accounting.
void saveUsage() {
  if (useMagic != BATTERY_USE_MAGIC || !Storage.ready()) return;
  UsageSnapshot snap{};
  snap.magic = USAGE_FILE_MAGIC;
  snap.version = USAGE_FILE_VERSION;
  snap.startRemCap = useStartRemCap;
  readGauge16(0x10, snap.savedRemCap);
  if (!halClock.epochSeconds(snap.rtcEpoch)) snap.rtcEpoch = 0;
  snap.elapsedMs = useElapsedMs;
  snap.runMs = useRunMs;
  snap.lightSleepMs = useLightSleepMs;
  snap.deepSleepMs = useDeepSleepMs;
  snap.highClockMs = useHighClockMs;
  snap.wifiMs = useWifiMs;
  snap.frontlightPctMs = useFrontlightPctMs;
  snap.refreshes = useRefreshes;
  snap.pageTurns = usePageTurns;
  snap.checksum = snapshotChecksum(snap);

  HalFile file;
  if (!Storage.openFileForWrite("BATTLOG", USAGE_PATH, file)) return;
  file.write(reinterpret_cast<const uint8_t*>(&snap), sizeof(snap));
  file.flush();
  file.close();
}

// Cold boot with RTC memory gone: pick the span back up from the card, unless
// the pack has visibly been charged since it was written.
bool restoreUsage() {
  if (!Storage.ready() || !Storage.exists(USAGE_PATH)) return false;
  UsageSnapshot snap{};
  HalFile file = Storage.open(USAGE_PATH, O_RDONLY);
  if (!file) return false;
  const int got = file.read(reinterpret_cast<uint8_t*>(&snap), sizeof(snap));
  file.close();
  if (got != static_cast<int>(sizeof(snap)) || snap.magic != USAGE_FILE_MAGIC ||
      snap.version != USAGE_FILE_VERSION || snap.checksum != snapshotChecksum(snap)) {
    return false;
  }

  uint16_t remCap = 0;
  if (!readGauge16(0x10, remCap) || remCap == 0) return false;
  if (snap.savedRemCap != 0 && remCap > snap.savedRemCap + USAGE_RESTORE_CHARGE_TOLERANCE_MAH) {
    LOG_DBG("BATTLOG", "Pack gained charge while off (%u -> %u mAh); new span", snap.savedRemCap, remCap);
    return false;
  }

  // The time spent switched off still belongs to the span. Only the RTC can
  // say how long that was; without it the gap is simply not counted.
  uint64_t offMs = 0;
  uint32_t nowEpoch = 0;
  if (snap.rtcEpoch != 0 && halClock.epochSeconds(nowEpoch) && nowEpoch >= snap.rtcEpoch &&
      nowEpoch - snap.rtcEpoch < USAGE_MAX_OFF_GAP_S) {
    offMs = static_cast<uint64_t>(nowEpoch - snap.rtcEpoch) * 1000ULL;
  }

  useMagic = BATTERY_USE_MAGIC;
  useStartRemCap = snap.startRemCap;
  useElapsedMs = snap.elapsedMs + offMs;
  useRunMs = snap.runMs;
  useLightSleepMs = snap.lightSleepMs;
  useDeepSleepMs = snap.deepSleepMs;
  useHighClockMs = snap.highClockMs;
  useWifiMs = snap.wifiMs;
  useFrontlightPctMs = snap.frontlightPctMs;
  useRefreshes = snap.refreshes;
  usePageTurns = snap.pageTurns;
  LOG_INF("BATTLOG", "Since-charge usage restored from card (off %lus)", static_cast<unsigned long>(offMs / 1000ULL));
  return true;
}

// True when the log on the card already carries today's columns (or there is
// no log yet). Compares the stored first line against CSV_HEADER.
bool schemaChecked() {
  if (!Storage.exists(LOG_PATH)) return true;
  HalFile file = Storage.open(LOG_PATH, O_RDONLY);
  if (!file) return true;  // unreadable: leave it alone rather than destroy it
  // Must be able to hold the WHOLE header plus a terminator: strncmp() below is
  // given strlen(CSV_HEADER) as its length, so a buffer shorter than the header
  // compares the header against this buffer's NUL and reports a mismatch every
  // single time -- which silently rotated the log away on every boot and left
  // exactly one row behind. Sized off the constant so adding a column cannot
  // reintroduce it.
  char stored[sizeof("uptime_s,rtc,event,remcap_mah,fcc_mah,mv,current_ma,soc,chrg_stat,pg,"
                     "cpu_mhz,fl_on,fl_pct,fl_on_ms,wifi_on_ms,hi_clock_ms,lsleep_ms,refreshes,page_turns,rows,"
                     "free_heap,dsleep_ms,stall_ms,stalls,park,rst,wake\n") +
              32] = {};
  const int got = file.read(stored, sizeof(stored) - 1);
  file.close();
  if (got <= 0) return true;  // empty file: the header is about to be written
  return strncmp(stored, CSV_HEADER, strlen(CSV_HEADER)) == 0;
}

void writeRow(const char* event) {
  if (!Storage.ready()) return;

  uint16_t remCap = 0, fcc = 0, mv = 0, rawCurrent = 0, soc = 0;
  readGauge16(0x10, remCap);
  readGauge16(0x12, fcc);
  readGauge16(0x08, mv);
  readGauge16(0x0C, rawCurrent);
  readGauge16(0x2C, soc);
  uint8_t chargerStatus = 0;
  const bool chargerOk = readCharger8(0x0B, chargerStatus);

  // A log written by an older firmware has fewer columns; appending today's
  // rows to it would produce a file no parser can read. Retire it instead --
  // once, before the first row of this session -- so the analysis always sees
  // one consistent schema.
  if (!headerChecked && !schemaChecked()) {
    Storage.remove(LOG_OLD_PATH);
    Storage.rename(LOG_PATH, LOG_OLD_PATH);
    LOG_DBG("BATTLOG", "Column set changed; previous log kept as %s", LOG_OLD_PATH);
  }

  // O_AT_END rather than the usual openFileForWrite(): that one passes O_TRUNC,
  // which would wipe the log on every row. Storage::open() takes raw SdFat
  // flags, so an append-only log needs no new HAL surface.
  HalFile file = Storage.open(LOG_PATH, O_RDWR | O_CREAT | O_AT_END);
  if (!file) {
    LOG_DBG("BATTLOG", "Could not open %s", LOG_PATH);
    return;
  }

  // Header only when starting a fresh file, so the log survives reboots as one
  // continuous series rather than restarting each time.
  if (!headerChecked) {
    headerChecked = true;
    if (file.fileSize() == 0) file.print(CSV_HEADER);
  }

  char rtcBuf[24] = "";
  if (!halClock.formatTime(rtcBuf, sizeof(rtcBuf))) rtcBuf[0] = '\0';

  // rst/wake are esp_reset_reason() and esp_sleep_get_wakeup_cause() as plain
  // integers, on every row because the interesting case is always a BOOT row
  // nobody was watching. They separate the three things that look identical from
  // the outside. Values verified on this board: a clean button wake from deep
  // sleep is rst=8 (DEEPSLEEP) wake=3 (EXT1); the debug timer wake is rst=8
  // wake=4 (TIMER); a crash on the way down is rst=4 (PANIC) or rst=6
  // (TASK_WDT) with wake=0; a chip that never slept reads rst=1 (POWERON), and
  // rst=11 (USB) is an esptool reflash. "It showed the sleep screen and then
  // restarted itself" could have been any of them.
  //
  // What the last sleep actually did, which is the one thing no row can observe
  // while it happens: the card is unmounted and the console is down by then, so
  // the sleep path leaves its findings in RTC_NOINIT and the next BOOT row is
  // where they surface. stall_ms is time the device spent awake inside the sleep
  // path (a power line that would not read released); park is 0 when the panel
  // PMIC was verified off before the chip stopped, and the offending expander
  // byte when it was not.
  const auto sleepReport = freeink::PowerManager::sleepReport();
  int parkState = -1;
#if FREEINK_DEVICE_LILYGO
  parkState = BoardT5S3::lastEpdParkOk() ? 0 : BoardT5S3::lastEpdParkState();
#endif

  char line[288];
  const int n = snprintf(
      line, sizeof(line),
      "%lu,%s,%s,%u,%u,%u,%d,%u,%d,%d,%u,%d,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%lu,%lu,%lu,%d,%d,%d\n",
      millis() / 1000UL, rtcBuf, event, remCap, fcc, mv, static_cast<int>(static_cast<int16_t>(rawCurrent)), soc,
      chargerOk ? ((chargerStatus >> 3) & 0x03) : -1, chargerOk ? ((chargerStatus & 0x04) ? 1 : 0) : -1,
      getCpuFrequencyMhz(), Frontlight.isOn() ? 1 : 0, Frontlight.brightness(),
      static_cast<unsigned long>(logFrontlightOnMs), static_cast<unsigned long>(logWifiOnMs),
      static_cast<unsigned long>(logHighClockMs), static_cast<unsigned long>(logLightSleepMs),
      static_cast<unsigned long>(logRefreshCount), static_cast<unsigned long>(logPageTurns),
      static_cast<unsigned long>(logRowCount), ESP.getFreeHeap(), static_cast<unsigned long>(logDeepSleepMs),
      static_cast<unsigned long>(sleepReport.releaseWaitTotalMs), static_cast<unsigned long>(sleepReport.timeouts),
      parkState, static_cast<int>(esp_reset_reason()), static_cast<int>(esp_sleep_get_wakeup_cause()));
  if (n > 0) {
    file.print(line);
    file.flush();
    ++logRowCount;
  }
  file.close();
}

}  // namespace

namespace BatteryLog {

void begin() {
  if (logMagic != BATTERY_LOG_MAGIC) {
    // Cold boot (or first run after flashing): start the counters clean. A wake
    // from deep sleep keeps them, which is the whole point of RTC_NOINIT.
    logMagic = BATTERY_LOG_MAGIC;
    logRefreshCount = 0;
    logRowCount = 0;
    logFrontlightOnMs = 0;
    logWifiOnMs = 0;
    logHighClockMs = 0;
    logLightSleepMs = 0;
    logPageTurns = 0;
    logSleepEntryUs = 0;
    logDeepSleepMs = 0;
  }
  display.setRefreshObserver(&noteDisplayRefresh);

  // Close out the gap the device just came back from. noteSleepEntry() stamped
  // the wall clock on the way down; the difference is time nothing else can
  // account for, and it is usually the largest single block in the day. The
  // release-poll stall is subtracted because those milliseconds were spent awake
  // inside the sleep path, not asleep -- charging them to deep sleep would hide
  // exactly the fault that made this worth measuring.
  if (logSleepEntryUs != 0) {
    const uint64_t now = wallUs();
    if (now > logSleepEntryUs) {
      const uint64_t gapMs = (now - logSleepEntryUs) / 1000ULL;
      // The wall clock can be stepped (an NTP sync moves it), so a nonsense gap
      // is dropped rather than allowed to poison the totals.
      if (gapMs < 30ULL * 24ULL * 60ULL * 60ULL * 1000ULL) {
        const uint32_t stall = freeink::PowerManager::sleepReport().releaseWaitMs;
        const uint32_t slept = gapMs > stall ? static_cast<uint32_t>(gapMs) - stall : 0;
        logDeepSleepMs += slept;
        if (useMagic == BATTERY_USE_MAGIC) {
          useDeepSleepMs += slept;
          useElapsedMs += gapMs;
        }
      }
    }
    logSleepEntryUs = 0;
  }

  // "Since last charge" starts when the cable comes out. Anchoring here as well
  // as in tick() matters for the wake-on-USB path, which can reach sleep again
  // without the main loop ever running.
  if (useMagic != BATTERY_USE_MAGIC && !restoreUsage()) anchorUsage();
  if (onExternalPower()) anchorUsage();
  saveUsage();

  lastSampleMs = millis();
  lastRowMs = millis();
  lastAccumMs = millis();
  writeRow("BOOT");
}

void tick() {
  // Every pass: see accumulate()'s note on why this cannot ride the sample
  // interval. It is a handful of comparisons plus one addition.
  accumulate();

  const unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  // Hold the since-charge span at zero for as long as external power is present,
  // so unplugging is what starts the clock. isCharging() is cached, so this is a
  // cheap question once a minute.
  if (onExternalPower()) anchorUsage();

  if (now - lastRowMs >= ROW_INTERVAL_MS) {
    lastRowMs = now;
    writeRow("SAMPLE");
    saveUsage();
  }
}

void flushNow(const char* event) {
  accumulate();
  writeRow(event);
  saveUsage();
  lastRowMs = millis();
}

void noteDisplayRefresh() {
  ++logRefreshCount;
  if (useMagic == BATTERY_USE_MAGIC) ++useRefreshes;
}

// millis() keeps counting across light sleep (the RTC timer runs), so the
// caller's before/after delta is the real halted time; accumulate() would
// otherwise charge it to whatever state the loop resumed in.
void noteLightSleep(const uint32_t ms) {
  logLightSleepMs += ms;
  if (useMagic == BATTERY_USE_MAGIC) useLightSleepMs += ms;
}

void notePageTurn() {
  ++logPageTurns;
  if (useMagic == BATTERY_USE_MAGIC) ++usePageTurns;
}

void noteSleepEntry() { logSleepEntryUs = wallUs(); }

Usage usage() {
  Usage out;
  out.onCharger = onExternalPower();
  if (useMagic != BATTERY_USE_MAGIC) return out;

  uint16_t remCap = 0, fcc = 0, soc = 0;
  readGauge16(0x10, remCap);
  readGauge16(0x12, fcc);
  readGauge16(0x2C, soc);

  out.valid = true;
  out.startRemCapMah = useStartRemCap;
  out.remCapMah = remCap;
  out.fccMah = fcc;
  out.socPct = soc;
  out.elapsedS = static_cast<uint32_t>(useElapsedMs / 1000ULL);
  out.runMs = useRunMs;
  out.lightSleepMs = useLightSleepMs;
  out.deepSleepMs = useDeepSleepMs;
  out.hiClockMs = useHighClockMs;
  out.wifiMs = useWifiMs;
  out.frontlightPctMs = useFrontlightPctMs;
  out.refreshes = useRefreshes;
  out.pageTurns = usePageTurns;

  const auto report = freeink::PowerManager::sleepReport();
  out.sleepStallMs = report.releaseWaitTotalMs;
  out.sleepStalls = report.timeouts;
#if FREEINK_DEVICE_LILYGO
  out.lastParkOk = BoardT5S3::lastEpdParkOk();
#endif
  return out;
}

}  // namespace BatteryLog
