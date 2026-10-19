#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <stdint.h>

// Start and stop screens of the companion.
namespace power_screens {

// Boot: shown once the display is up, before the radio starts.
void drawBoot(DisplayDriver* d);

// Power-off: a last frame saying the node is off, why (low battery) and how to
// turn it on. mv — измеренное напряжение для строки «Low battery 2.71V» (0 —
// не показывать). On e-ink it is pushed with a full refresh and the panel left
// powered down, so it stays readable; the caller then powers the node off.
void drawPowerOff(DisplayDriver* d, bool low_battery, uint16_t mv = 0);

// Whether a battery at `mv` is too flat to keep running: below
// AUTO_SHUTDOWN_MILLIVOLTS (3.40 V unless the board sets its own), or at or
// below AUTO_SHUTDOWN_PERCENT of charge if that is set. Never on external
// power, nor below ~2.5 V, which means no battery (none fitted, no sense line).
bool batteryFlat(uint16_t mv, bool external_power);

}  // namespace power_screens
