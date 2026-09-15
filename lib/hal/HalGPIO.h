#pragma once

#include <Arduino.h>
#include <InputManager.h>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

// Xteink X3 Hardware
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

// TI BQ27220 Fuel gauge I2C
#define I2C_ADDR_BQ27220 0x55  // Fuel gauge I2C address
#define BQ27220_SOC_REG 0x2C   // StateOfCharge() command code (%)
#define BQ27220_CUR_REG 0x0C   // Current() command code (signed mA)
#define BQ27220_VOLT_REG 0x08  // Voltage() command code (mV)

// Analog DS3231 RTC I2C
#define I2C_ADDR_DS3231 0x68  // RTC I2C address
#define DS3231_SEC_REG 0x00   // Seconds command code (BCD)

// QST QMI8658 IMU I2C
#define I2C_ADDR_QMI8658 0x6B        // IMU I2C address
#define I2C_ADDR_QMI8658_ALT 0x6A    // IMU I2C fallback address
#define QMI8658_WHO_AM_I_REG 0x00    // WHO_AM_I command code
#define QMI8658_WHO_AM_I_VALUE 0x05  // WHO_AM_I expected value

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

  // Serial-injected button state (see injectButton)
  uint8_t pendingInjectButton = 0xFF;
  unsigned long pendingInjectHoldMs = 0;
  uint8_t injectedButton = 0xFF;
  unsigned long injectedHoldMs = 0;
  unsigned long injectedPressStart = 0;
  bool injectActive = false;
  bool injectPressEdge = false;
  bool injectReleaseEdge = false;

  // Serial-injected touch state (see injectTouchTap and friends)
  enum class InjectTouch : uint8_t { None, Tap, LongPress, Swipe };
  InjectTouch pendingTouch = InjectTouch::None;
  InjectTouch activeTouch = InjectTouch::None;
  float injTouchX1 = 0, injTouchY1 = 0, injTouchX2 = 0, injTouchY2 = 0;

  // Configurable action buttons (see setMaskedButtons above).
  uint8_t maskedButtons = 0;
  bool masked(uint8_t buttonIndex) const { return (maskedButtons >> buttonIndex) & 1; }

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }
  bool isXteinkDevice() const;

  // True when the board's page buttons sit on the left/right screen edges
  // (X3, X4 Pro) rather than an off-screen vertical rocker. Drives side-hint
  // placement and the flipped large-step direction in selection activities.
  // Keyed off the active BoardConfig profile, not the X3/X4 runtime detection.
  bool hasEdgeSideButtons() const;

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;

  // Configurable action buttons: a masked button is hidden from the normal
  // queries above (its raw identity must not leak to activities) and is read
  // through the raw* accessors by main.cpp's dispatcher, which translates
  // short/long presses into user-configured actions.
  //
  // The mask applies to injected buttons too, which is what makes CMD:KEY on a
  // masked button emulate that key rather than the meaning it used to have:
  // inject UP on a board whose UP pin is a configurable key and you get the
  // action bound to that key, not a scroll. Anything that needs the meaning
  // asks for it directly -- a dispatched Back is MappedInputManager::
  // requestBackAction(), not a synthesized BTN_BACK.
  void setMaskedButtons(uint8_t mask) { maskedButtons = mask; }
  bool rawIsPressed(uint8_t buttonIndex) const;
  bool rawWasPressed(uint8_t buttonIndex) const;
  bool rawWasReleased(uint8_t buttonIndex) const;

  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;
  // True when any button contact is closed right now, read straight from the
  // hardware (ADC ladder off its idle rail, or the power GPIO asserted), without
  // going through the debounced state. Cheap enough to call every few ms.
  bool rawInputActive();
  bool hasTouch() const;
  // Capacitive Home key reported by the touch controller (X4 Pro). The tap
  // event fires on release and excludes a long hold.
  bool hasHomeKey() const;
  bool wasHomeKeyPressed() const;
  bool wasHomeKeyTapped() const;
  bool wasHomeKeyLongPressed() const;
  bool wasTouchTap(float& nx, float& ny) const;
  bool wasTouchDown(float& nx, float& ny) const;
  // Raw release edge, reported even when the contact was not a tap (swipe end,
  // drag-off). Snapshot builders forward it so interaction routing can clear
  // pressed state.
  bool wasTouchReleased() const;
  bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const;
  bool isTouchHeldAt(float& nx, float& ny) const;
  // One-shot long-press, fired by the SDK classifier while the finger is still
  // down (stationary contact held past its threshold). Position = touch-down
  // point. Callers that act on it should suppressTouchContact() so the lift
  // cannot also tap.
  bool wasTouchLongPress(float& nx, float& ny) const;
  // Ignore the remainder of the current contact (its continued hold and its
  // release edge). Self-clears once the contact ends.
  void suppressTouchContact();
  unsigned long lastTouchHeldMs() const;
  bool wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;
  bool wasTouchActivity() const;
  void setSharedConfirmPowerShortPressEmitsPower(bool enabled);

  bool isPowerButtonPhysicallyPressed() const;

  // Debug: fake a button press from the serial console (CMD:KEY:...). The
  // press edge fires on the next update(), isPressed() stays true for holdMs,
  // then the release edge fires — one edge per update() frame, mirroring how
  // InputManager reports real buttons. Boards whose buttons don't physically
  // exist (T5S3 has only the BOOT button) are driven entirely through this.
  void injectButton(uint8_t buttonIndex, unsigned long holdMs = 0);

  // Debug: fake touch gestures from the serial console (CMD:TAP/LONG/SWIPE).
  // Coordinates are normalized to the native panel frame (same space the
  // touch controller reports); each event fires for exactly one update()
  // frame through the same accessors real touches use, so every consumer —
  // FreeInkUI hit rects, rowTouch grids, reader tap zones — sees them.
  void injectTouchTap(float nx, float ny);
  void injectTouchLongPress(float nx, float ny);
  void injectSwipe(float nx1, float ny1, float nx2, float ny2);

  // Verify that the physical power button remains held through input debounce.
  // Returns true if verification succeeded, false if device should return to sleep.
  // Should only be called when wakeup reason is PowerButton.
  bool verifyPowerButtonWakeup();

  // Check if USB is connected
  bool isUsbConnected() const;

  // Charging state for the battery indicator. Deliberately separate from
  // isUsbConnected(): that one feeds wake-reason classification, where changing
  // the answer changes boot behaviour (see getWakeupReason). This one is free to
  // ask the charger IC / fuel gauge over I2C on boards that have no usbDetect
  // pin at all -- the LilyGo T5 S3 is one, which is why its battery icon never
  // showed a charging bolt. Cached: the read is a multi-register I2C
  // transaction on a bus shared with the panel PMIC, far too slow per frame.
  bool isCharging() const;

  // True when the charger IC says it has finished topping the pack off while
  // external power is present (BQ25896 CHRG_STAT = 0b11 with PG_STAT set).
  // The fuel gauge cannot answer this on its own -- see HalPowerManager::
  // getBatteryPercentage() for why the reported percentage needs it.
  bool isChargeComplete() const;

  // Ship mode: ask the charger IC (BQ25896) to open its BATFET, disconnecting
  // the pack from the system rail. On battery the board loses power inside a
  // few hundred microseconds of the write returning; on USB it keeps running
  // from VBUS, so callers must fall through to ordinary deep sleep. Exit is
  // hardware-only: /QON (the power button) held low for ~1 s, or a cable.
  // Returns false when the board has no charger that can do this.
  bool enterChargerShipMode() const;

  static constexpr unsigned long CHARGE_POLL_MS = 1500;
  // Whether a cold boot with no USB detected can be trusted to mean a held
  // power button (Xteink-style button-energized rail with reliable USB
  // detection). When false, cold boots always proceed to a normal boot.
  bool coldBootImpliesPowerButton() const;

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
