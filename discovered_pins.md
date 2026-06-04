# CrossPoint — Discovered GPIO Pin Map (HARDWARE-SAFETY REFERENCE)

> Verified **directly from source** (not guessed). Every row cites the defining
> `file:line`. These are the pin numbers the Rust port MUST use unchanged.
> Wrong pins on this board can short the e-ink panel or the battery latch.

Target board: **ESP32-C3** (RISC-V, `riscv32imc-unknown-none-elf`), Xteink **X4**
(primary) with **X3** variant auto-detected at boot.

## 1. E-ink display — SPI (custom pins, NOT the C3 hardware-SPI defaults)
Source: `lib/hal/HalGPIO.h:7-14`, driver `open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp`

| Signal | GPIO | Define | Source |
|--------|-----:|--------|--------|
| SCLK (clock)      | 8  | `EPD_SCLK` | `lib/hal/HalGPIO.h:7` |
| MOSI (data out)   | 10 | `EPD_MOSI` | `lib/hal/HalGPIO.h:8` |
| MISO (data in)    | 7  | `SPI_MISO` (shared with SD) | `lib/hal/HalGPIO.h:14` |
| CS (chip select)  | 21 | `EPD_CS`   | `lib/hal/HalGPIO.h:9` |
| DC (data/command) | 4  | `EPD_DC`   | `lib/hal/HalGPIO.h:10` |
| RST (reset)       | 5  | `EPD_RST`  | `lib/hal/HalGPIO.h:11` |
| BUSY              | 6  | `EPD_BUSY` (`pinMode INPUT`) | `lib/hal/HalGPIO.h:12` |

**SPI settings** (`EInkDisplay.cpp:495-496`):
`SPISettings(spiHz, MSBFIRST, SPI_MODE0)` where `spiHz = X3 ? 16_000_000 : 40_000_000`.
→ **Mode 0 (CPOL=0, CPHA=0), MSB-first. X4 = 40 MHz, X3 = 16 MHz.**

**BUSY polarity differs by variant** (`EInkDisplay.cpp:550-575`) — CRITICAL:
- **X4 (SSD1677):** BUSY **active HIGH** — held HIGH while busy, drops LOW when done.
  Poll: `while (digitalRead(BUSY) == HIGH) {}`.
- **X3 (UC81xx):** BUSY **active LOW** — idle HIGH, working LOW. Wait for HIGH→LOW→HIGH
  with race protection (controller may not assert BUSY immediately).

**Resolution / controller** (`open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h:27-33`):
- X4: **SSD1677**, **800×480**, 100 bytes/row, BUFFER_SIZE = 800/8*480 = 48000 B/plane.
- X3: **UC81xx**, **792×528**, 99 bytes/row.
- 1-bit per pixel; dual RAM planes (BW + RED/LSB-MSB). Panel = GDEQ0426T82 (4.26").

## 2. Buttons / input
Source: `open-x4-sdk/libs/hardware/InputManager/include/InputManager.h:82-115`,
`.../src/InputManager.cpp:17-67`

Front buttons are **analog**, multiplexed on two ADC pins via a resistor ladder
(`pinMode INPUT`, no pull). The side power button is **digital, active-low**.

| Input | GPIO | Mode | Source |
|-------|-----:|------|--------|
| Front ADC group 1 (Back, Confirm, Left, Right) | 1 | `INPUT` / `analogRead(1)` | `InputManager.h:82` |
| Front ADC group 2 (Up, Down)                   | 2 | `INPUT` / `analogRead(2)` | `InputManager.h:83` |
| Power button                                   | 3 | `INPUT_PULLUP`, pressed = `LOW` | `InputManager.h:84` |

**ADC decode ranges** (`InputManager.cpp:17-18`), value falls in a band → button:
- `ADC_RANGES_1 = {3900(none), 3100, 2090, 750, INT32_MIN}` → group order Back/Confirm/Left/Right.
- `ADC_RANGES_2 = {3900(none), 1120, INT32_MIN}` → Up/Down.
- `ADC_NO_BUTTON = 3900` (`InputManager.h:112`): reading above ⇒ nothing pressed.
- Debounce `DEBOUNCE_DELAY` gate (`InputManager.cpp:88`); power button debounce 5 ms.

**Logical button indices** (`lib/hal/HalGPIO.h:94-100`):
`BACK=0, CONFIRM=1, LEFT=2, RIGHT=3, UP=4, DOWN=5, POWER=6`.

## 3. Power management
Source: `lib/hal/HalPowerManager.cpp:79-94`, `lib/hal/HalGPIO.h:16-18`,
`open-x4-sdk/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp`

| Function | GPIO | Notes | Source |
|----------|-----:|-------|--------|
| **Battery latch MOSFET** | **13** | Drive **LOW** + `gpio_hold_en` before deep sleep → fully powers off MCU. DO NOT drive high spuriously. | `HalPowerManager.cpp:81-86` |
| Battery voltage ADC (X4) | 0  | divider ×2.0 | `HalGPIO.h:16` (`BAT_GPIO0`), `BatteryMonitor.cpp:8` |
| USB-detect (X4)          | 20 | UART0_RXD reads HIGH when USB present | `HalGPIO.h:18` (`UART0_RXD`) |
| Power-button wakeup       | 3 | `esp_deep_sleep_enable_gpio_wakeup(1<<3, ESP_GPIO_WAKEUP_GPIO_LOW)` | `HalPowerManager.cpp:92` |

Deep-sleep sequence (`HalPowerManager.cpp:82-94`): set GPIO13 output → level 0 →
`esp_sleep_config_gpio_isolate()` → `gpio_deep_sleep_hold_en()` → `gpio_hold_en(13)`
→ arm wakeup on GPIO3 low → `esp_deep_sleep_start()`.

## 4. SD card (shares the display SPI bus)
Source: `open-x4-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:4-13`

| Signal | GPIO | Source |
|--------|-----:|--------|
| SD CS  | 12 | `SDCardManager.cpp:4` (`SD_CS = 12`) |
| SCLK/MOSI/MISO | 8 / 10 / 7 | shared with display |
| SPI freq | 40 MHz | `SDCardManager.cpp:5` (`SPI_FQ`) |

Bus brought up once in `lib/hal/HalGPIO.cpp` via `SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS)`.
**Two CS lines on one bus** (display=21, SD=12) — only assert one at a time.

## 5. X3-only I²C (fuel gauge / RTC / IMU) — not on X4
Source: `lib/hal/HalGPIO.h:20-39`

| Signal | GPIO | Source |
|--------|-----:|--------|
| I²C SDA | 20 | `X3_I2C_SDA` (`HalGPIO.h:21`) |
| I²C SCL | 0  | `X3_I2C_SCL` (`HalGPIO.h:22`) — **also BAT ADC pin; mode-switched** |
| I²C freq | 400 kHz | `X3_I2C_FREQ` |

Devices: BQ27220 fuel gauge `0x55` (SOC `0x2C`, Volt `0x08`, Cur `0x0C`),
DS3231 RTC `0x68`, QMI8658 IMU `0x6B`/`0x6A`. X3 detected by I²C fingerprint
at boot (`HalGPIO.cpp` `detectDeviceTypeWithFingerprint`), cached in NVS.

## 6. Conflict / safety summary for the Rust port
- **GPIO 13** is the kill-switch latch — keep it untouched at runtime, only drive
  low+hold in the sleep path. A stray high keeps the board powered / blocks shutdown.
- **GPIO 0** is dual-use on X3 (I²C SCL **and** battery ADC) — never assume one mode.
- **GPIO 1 & 2** are ADC inputs, not digital — read with the ADC, decode by range.
- **One SPI bus, two CS** (display 21 / SD 12) — mutually exclusive selects.
- **BUSY polarity is inverted between X4 and X3** — branch on device type.
- SPI is **Mode 0, MSB-first**; clock 40 MHz (X4) / 16 MHz (X3).
