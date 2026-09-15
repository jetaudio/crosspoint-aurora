#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;            // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;     // Timestamp of last battery read in milliseconds
  mutable uint16_t _batteryCachedMillivolts = 0;    // Last read pack voltage, 0 = never read
  mutable unsigned long _millivoltsLastPollMs = 0;  // Its own timestamp: the two reads are independent

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode.
  // powerOff: after the same teardown, put the charger into ship mode so the
  // pack is disconnected outright (see HalGPIO::enterChargerShipMode). If the
  // board is still alive afterwards -- no such charger, or running from USB --
  // it continues into ordinary deep sleep.
  void startDeepSleep(HalGPIO& gpio, bool powerOff = false) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // Pack voltage in millivolts, 0 when the board cannot report one. Needed
  // alongside the percentage because the two disagree near empty: the gauge's
  // 0% is calibrated to its own terminate voltage, while the board browns out
  // when the 3.3 V regulator runs out of headroom -- a higher voltage, and so a
  // NON-zero percentage. Voltage is the quantity that actually predicts the
  // cutoff, so the low-battery shutdown watches both.
  uint16_t getBatteryMillivolts() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
