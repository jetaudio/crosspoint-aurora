#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <stdlib.h>
#include <time.h>

HalClock halClock;  // Singleton instance

namespace {
constexpr const char* NTP_SERVER_HOSTS[] = {"pool.ntp.org", "time.nist.gov"};
constexpr size_t NTP_SERVER_HOST_COUNT = sizeof(NTP_SERVER_HOSTS) / sizeof(NTP_SERVER_HOSTS[0]);

// Resolve the NTP hosts here and hand SNTP literal addresses instead of names.
//
// lwIP's SNTP resolves a server *name* asynchronously (dns_gethostbyname), and with
// SNTP_STARTUP_DELAY the first request fires up to 5s after esp_sntp_init(), so the
// lookup is easily still pending when the next connection opens -- and this runs on
// the first WiFi connect, right before whatever the user came online to do. Arduino's
// NetworkManager::hostByName() calls dns_clear_cache() straight from the calling task
// -- no lwIP core lock -- the first time the interface's IP state changes, which is
// every session's first lookup. Clearing the table runs dns_call_found() on the
// pending SNTP entry (sntp_try_next_server -> sntp_retry -> sys_untimeout) outside
// TCPIP context and panics on LWIP_ASSERT_CORE_LOCKED ("Required to lock TCPIP core
// functionality!").
//
// Resolving up front keeps SNTP free of DNS requests entirely, and the one unlocked
// dns_clear_cache then happens while the DNS table is still empty.
bool startSntpWithResolvedServers() {
  // Can't reconfigure while running.
  if (esp_sntp_enabled()) esp_sntp_stop();

  ip_addr_t servers[NTP_SERVER_HOST_COUNT];
  uint8_t resolved = 0;
  for (size_t i = 0; i < NTP_SERVER_HOST_COUNT && resolved < SNTP_MAX_SERVERS; i++) {
    IPAddress address;
    if (WiFi.hostByName(NTP_SERVER_HOSTS[i], address) != 1) {
      LOG_DBG("CLK", "Could not resolve %s", NTP_SERVER_HOSTS[i]);
      continue;
    }
    address.to_ip_addr_t(&servers[resolved]);
    LOG_DBG("CLK", "NTP server %s resolved to %s", NTP_SERVER_HOSTS[i], address.toString().c_str());
    resolved++;
  }
  if (resolved == 0) return false;

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  // Clear every slot first: a hostname a previous configTime()/configTzTime() left
  // behind would otherwise be resolved by sntp_try_next_server() behind our back.
  for (u8_t i = 0; i < SNTP_MAX_SERVERS; i++) esp_sntp_setserver(i, nullptr);
  for (uint8_t i = 0; i < resolved; i++) esp_sntp_setserver(i, &servers[i]);
  esp_sntp_init();
  return true;
}

// Nothing outside this sync needs SNTP; drop its timer and socket on every exit
// path rather than leaving it polling for the rest of the session.
struct SntpStopGuard {
  ~SntpStopGuard() {
    if (esp_sntp_enabled()) esp_sntp_stop();
  }
};
}  // namespace

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_hasCachedTime) return false;
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _lastPollMs = now;
  _hasCachedTime = true;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::epochSeconds(uint32_t& out) const {
  if (!_available) return false;
  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) return false;
  // Days from civil (Howard Hinnant): no mktime(), so no dependency on TZ.
  const int y = static_cast<int>(dt.year) - (dt.month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned mp = dt.month > 2 ? dt.month - 3u : dt.month + 9u;
  const unsigned doy = (153 * mp + 2) / 5 + dt.day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  if (days < 0) return false;
  out = static_cast<uint32_t>(days * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  // configTzTime() would set this and hand SNTP the hostnames; only the TZ half
  // is still wanted (the RTC is stored in UTC, and gmtime_r reads it back).
  setenv("TZ", "UTC0", 1);
  tzset();
  if (!startSntpWithResolvedServers()) {
    LOG_ERR("CLK", "No NTP server resolved, skipping sync");
    return false;
  }
  const SntpStopGuard sntpStopGuard;

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);

      Rtc::DateTime dt;
      dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
      dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
      dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
      dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
      dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
      dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
      dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
      if (_sdkRtc.set(dt)) {
        _lastPollMs = 0;
        _cachedHour = dt.hour;
        _cachedMinute = dt.minute;
        _hasCachedTime = true;
        LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                dt.second);
        return true;
      }
      return false;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
