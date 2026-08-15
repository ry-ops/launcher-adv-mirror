#ifndef LAUNCHER_SERIAL_CONSOLE_H
#define LAUNCHER_SERIAL_CONSOLE_H

#include <cstdint>

// FreeRTOS task that parses commands typed into the Serial Monitor and lets a
// host script drive the Launcher (navigate menus, inspect/edit the partition
// table, flash firmware) without physical button access. See src/serial_console.cpp
// for the command grammar.
void taskSerialConsole(void *parameter);

// launcher-adv-mirror: increments once per taskSerialConsole() poll iteration
// (~every 5-10ms). A live counter reachable over HTTP even when the USB CDC
// link itself has gone silent -- proof the console task is actually still
// scheduled and polling Serial.available(), as opposed to the USB link being
// the thing that's stuck. See ADAPTER.md / adapters/launcher/'s /diag route.
uint32_t consoleLoopTicks();

#endif
