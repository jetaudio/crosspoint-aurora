// x4-emu networking shim: bring up Ethernet over the QEMU OpenCores MAC
// (esp32c3 QEMU machine) so the firmware reaches the host's internet via SLIRP.
// Compiled/active only in the `emu` build (-DX4EMU_ETH). No-op otherwise.
#pragma once

void emuNetStart(void);
