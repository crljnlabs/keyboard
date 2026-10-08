// Hardware pin map.
//
// Single source of truth for every GPIO the firmware touches.
//
// IMPORTANT: these values do NOT follow the net names in the schematic. The
// PCB has one of the two ESP32 headers wired upside down, and the values below
// describe what is physically connected, which is what the firmware has to
// drive.
//
// Both headers run top to bottom on the board (y 23.52 -> 43.84 in
// kicad/Keypad.kicad_pcb). The column at x=74.74 carries
//   5V, GND, 3V3, GP13, GP12, GP11, GP10, GP9, GP8
// which matches the ESP32-S3 Super Mini, so everything on that side is fine.
// The column at x=89.78 carries
//   GP7, GP6, GP5, GP4, GP3, GP2, GP1, RX, TX
// while the module reads TX, RX, GP1 ... GP7 top to bottom - exactly reversed.
// The resulting real wiring, slot by slot:
//
//   slot 1  GPIO43 -> display SCL      slot 6  GPIO4  -> display BLK
//   slot 2  GPIO44 -> display SDA      slot 7  GPIO5  -> SW3
//   slot 3  GPIO1  -> display RES      slot 8  GPIO6  -> SW2
//   slot 4  GPIO2  -> display DC       slot 9  GPIO7  -> SW1
//   slot 5  GPIO3  -> display CS       (slot 5 is the centre, so CS is the one
//                                       pin the reversal left in place)
//
// The board should be corrected in the next PCB revision; until then this
// header, and the TFT_* macros in platformio.ini that mirror it, compensate.
// Do not "fix" these values back to the schematic net names.

#pragma once

#include <stdint.h>

namespace pins {

// --- TFT display (ST7789, 8-pin module: GND VCC SCL SDA RES DC CS BLK) ------
// Write-only over SPI; the module has no MISO line.
//
// SCL and SDA land on GPIO43/44, the UART0 pins. That works because logging
// goes through the native USB CDC interface, not UART0. The ROM bootloader
// does print on GPIO43 for a few milliseconds at boot, which clocks a little
// noise into the panel before the firmware resets it - harmless.
constexpr uint8_t kTftSclk = 43;
constexpr uint8_t kTftMosi = 44;
constexpr uint8_t kTftReset = 1;
constexpr uint8_t kTftDc = 2;
constexpr uint8_t kTftCs = 3;
constexpr uint8_t kTftBacklight = 4;

// --- Rotary encoder (KY-040 style: CLK DT SW + GND) ------------------------
// On the correctly wired header, so these match the schematic.
constexpr uint8_t kEncoderA = 13;
constexpr uint8_t kEncoderB = 12;
constexpr uint8_t kEncoderButton = 11;

// --- Key switches ----------------------------------------------------------
// Every switch shorts its GPIO to GND, so all of them are active low and need
// an internal pull-up. SW1-SW3 sit on the reversed header; SW4-SW6 do not.
constexpr uint8_t kSwitchCount = 6;
constexpr uint8_t kSwitches[kSwitchCount] = {
    7,   // SW1
    6,   // SW2
    5,   // SW3
    10,  // SW4
    9,   // SW5
    8,   // SW6
};

}  // namespace pins
