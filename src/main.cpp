#include <globals.h>

#if defined(HEADLESS)
#include <VectorDisplay.h>
#else
#include <tft.h>
#endif
#if defined(CARDPUTER)
// launcher-adv-mirror ADR 0001: browser display mirror + remote control,
// added on top of upstream Launcher. See ADAPTER.md.
#include "CardputerMirror.h"
#include "LauncherAdapter.h"
#include "serial_console.h"
#include <ESPAsyncWebServer.h>
static bool g_mirrorOk      = false;
static bool g_mirrorStarted = false; // begin() attempted (once) -- see tryStartCardputerMirror() below
#endif
#include "esp_ota_ops.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "nvs_flash.h"
#if ARDUINO_M5STACK_TAB5
#include "nvs.h"
#include "nvs_handle.hpp"
#endif
#include <SD.h>
#include <SPIFFS.h>

#include "powerSave.h"
#include "ram_profile.h"
#include "utils.h"
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

#if defined(SET_LOOP_TASK_STACK_SIZE)
SET_LOOP_TASK_STACK_SIZE(16384)
#endif

// Public Globals
#ifdef USE_M5GFX
uint16_t FGCOLOR = BLACK;
uint16_t ALCOLOR = BLACK;
uint16_t BGCOLOR = WHITE;
#elif E_PAPER_DISPLAY
uint16_t FGCOLOR = BLACK;
uint16_t ALCOLOR = 0x8888;
uint16_t BGCOLOR = WHITE;
#else
uint16_t FGCOLOR = GREEN;
uint16_t ALCOLOR = RED;
uint16_t BGCOLOR = BLACK;
#endif
uint16_t odd_color = 0x30c5;
uint16_t even_color = 0x32e5;

int8_t _miso = SDCARD_MISO;
int8_t _mosi = SDCARD_MOSI;
int8_t _sck = SDCARD_SCK;
int8_t _cs = SDCARD_CS;

// Navigation Variables
long LongPressTmp = 0;
volatile bool LongPress = false;
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
LTouchPoint touchPoint;
keyStroke KeyStroke;

#if defined(HAS_TOUCH)
volatile uint16_t tftHeight = TFT_WIDTH - (FM * LH + 4);
#else
volatile uint16_t tftHeight = TFT_WIDTH;
#endif
volatile uint16_t tftWidth = TFT_HEIGHT;
TaskHandle_t xHandle;
// Defined in mykeyboard.cpp; declared here because this task is compiled before that header
// is included below.
void launcherInputLockInit();
void launcherInputLock();
void launcherInputUnlock();

void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = launcherMillis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || launcherMillis() - timer > 75) {
            // Held across the whole update so _getKeyPress() can never observe (or copy) a
            // half-written KeyStroke. It replaces the old vTaskSuspend() scheme, which could
            // freeze this task inside the allocator and deadlock loopTask - see mykeyboard.h.
            launcherInputLock();
            resetGlobals();
#ifndef DONT_USE_INPUT_TASK
            InputHandler();
#ifdef USE_CARDKB2
            cardkb2_poll();
#endif
#endif
            launcherInputUnlock();
            timer = launcherMillis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// More 2nd grade global Variables
int dimmerSet = 20;
unsigned long previousMillis;
bool isSleeping;
bool isScreenOff;
bool dev_mode = false;
int bright = 100;
bool dimmer = false;
int prog_handler; // 0 - Flash, 1 - SPIFFS
int currentIndex;
int rotation = ROTATION;
bool sdcardMounted;
bool onlyBins;
bool bootToApp = true;
bool noDotFiles;
bool autoBackup = true;
bool returnToMenu;
bool update;
bool askSpiffs;

// bool command;
size_t file_size;
String ssid;
String pwd;
String wui_usr = "admin";
String wui_pwd = "launcher";
String dwn_path = "/downloads/";
String lastInstalledApp = "";
uint16_t total_firmware = 0;
uint8_t current_page = 1;
uint8_t num_pages = 0;
JsonDocument doc(launcherJsonAllocator());
JsonArray favorite;
JsonDocument settings;
std::vector<Option> options;

#include "app_registry.h"
#include "display.h"
#include "massStorage.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "sd_functions.h"
#include "serial_console.h"
#include "settings.h"
#include "webInterface.h"

/*********************************************************************
**  Function: _setup_gpio()
**  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
*********************************************************************/
void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

/*********************************************************************
**  Function: _post_setup_gpio()
**  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
*********************************************************************/
void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
**  Function: _late_setup_gpio()
**  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
*********************************************************************/
void _late_setup_gpio() __attribute__((weak));
void _late_setup_gpio() {}

#if defined(CARDPUTER)
// launcher-adv-mirror. Deliberately NOT called from setup(): that runs
// before Launcher's own network stack exists at all -- WiFi doesn't auto-
// connect here, it needs a manual `wifi connect`/`wifi auto`, sometimes long
// after boot. Starting CardputerMirror's AsyncWebServer/AsyncWebSocket
// before lwIP's tcpip task exists crashes ("assert failed: tcpip_api_call
// ... Invalid mbox"). Called instead from the home menu loop, once, the
// first time launcherWifiIsConnected() is true -- by then lwIP is
// definitely up, because a real WiFi connection can't exist without it.
static void tryStartCardputerMirror() {
    if (g_mirrorStarted || !launcherWifiIsConnected()) return;
    g_mirrorStarted = true;

    cmirror::Config mc;
    mc.manageWifi = false; // Launcher already owns WiFi connection policy
    g_mirrorOk = CardputerMirror.begin(mc, launcherAdapter);
    launcherConsolePrintf("CardputerMirror.begin() -> %s\n", g_mirrorOk ? "true" : "FALSE");
    // The old standalone mirror firmware's own wifi_manager started mDNS
    // unconditionally at boot with this exact hostname -- CardputerMirror
    // itself has no mDNS code (that was standalone-example-only), and
    // Launcher only starts mDNS when its own WUI feature is launched by
    // hand. Start it here so http://cardputer.local keeps working the same
    // way it did before this integration.
    launcherMdnsStart("cardputer", mc.port);

    // launcher-adv-mirror: /diag -- reachable over HTTP/WiFi (proven reliable
    // all session) specifically so USB CDC health is inspectable even when
    // the USB link itself has gone silent, which is exactly the failure mode
    // this exists for: serial stopped responding to input or heartbeats with
    // no clear cause, surviving reflashes and power cycles. isConnected()/
    // isPlugged() distinguish "USB link genuinely down" from "link fine, but
    // something upstream (taskSerialConsole, or Serial's own TX path) isn't
    // moving bytes"; consoleLoopTicks() (serial_console.h) distinguishes
    // "task not scheduled at all" from "task alive, Serial just isn't
    // delivering/accepting bytes" -- next occurrence, check this instead of
    // guessing blind again.
    if (g_mirrorOk) {
        auto *srv = (AsyncWebServer *)CardputerMirror.serverHandle();
        if (srv) {
            srv->on("/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
                char buf[256];
                snprintf(
                    buf, sizeof(buf),
                    "{\"usbConnected\":%s,\"usbPlugged\":%s,\"consoleLoopTicks\":%lu,"
                    "\"freeHeap\":%u,\"uptimeMs\":%lu}",
                    Serial.isConnected() ? "true" : "false", Serial.isPlugged() ? "true" : "false",
                    (unsigned long)consoleLoopTicks(), (unsigned)ESP.getFreeHeap(),
                    (unsigned long)launcherMillis()
                );
                req->send(200, "application/json", buf);
            });
        }
    }
}
#endif

/*********************************************************************
**  Function: setup
**  Where the devices are started and variables set
*********************************************************************/
void setup() {
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
    RAM_LOG("setup-start");
    nvs_flash_init();
    launcherPartitionInitDefaultSizes();
    ensureM5StackUiFlowNVSDefaults();
    RAM_LOG("after-nvs-partition-defaults");

#if ARDUINO_M5STACK_TAB5
    esp_err_t nve;
    std::unique_ptr<nvs::NVSHandle> nvsHandle = nvs::open_nvs_handle("launcher", NVS_READWRITE, &nve);
    bool init = false;
    nve = nvsHandle->get_item("init", init);
    if (nve != ESP_OK) {
        nvsHandle->set_item("init", false);
        nvsHandle->commit();
        init = false;
    }
    if (init >= 1) { // restart com eeprom em 1
        nvsHandle->set_item("init", false);
        nvsHandle->commit();
        ESP.restart();
    } else {
        nvsHandle->set_item("init", true);
        nvsHandle->commit();
    }
#endif

// Setup GPIOs and stuff
#if defined(HEADLESS)
#if LED > 0
    launcherGpioOutput(LED);        // Set pin to recognize if launcher is starting or connecting
    launcherGpioWrite(LED, LED_ON); // keeps on until exit
#endif
#endif

    _setup_gpio();

    // Get Configuration from NVS partition
    getFromNVS();
    RAM_LOG("after-getFromNVS");

    // declare variables
    size_t currentIndex = 0;
    prog_handler = 0;
    sdcardMounted = false;
    String fileToCopy;

// Init Display
#if !defined(HEADLESS)
    // tft->setAttribute(PSRAM_ENABLE,true);
    tft->begin();
#ifdef TFT_INVERSION_ON
    tft->invertDisplay(true);
#endif

#endif
    tft->setRotation(rotation);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    if (rotation & 0b1) {
#if defined(HAS_TOUCH)
        tftHeight = TFT_WIDTH - (FM * LH + 4);
#else
        tftHeight = TFT_WIDTH;
#endif
        tftWidth = TFT_HEIGHT;
    } else {
#if defined(HAS_TOUCH)
        tftHeight = TFT_HEIGHT - (FM * LH + 4);
#else
        tftHeight = TFT_HEIGHT;
#endif
        tftWidth = TFT_WIDTH;
    }
    tft->fillScreen(BGCOLOR);
    setBrightness(bright, false);
    initDisplay(true);
    RAM_LOG("after-first-display");

    // Performs the verification when Launcher is installed through OTA
    partitionCrawler();
    RAM_LOG("after-partitionCrawler");

    // Init post setup GPIO before SD Card initializes
    _post_setup_gpio();

    // launcher-adv-mirror: CardputerMirror.begin() is NOT called here.
    // Learned the hard way: this point in setup() runs before Launcher's own
    // network stack exists at all (WiFi doesn't auto-connect here -- it
    // needs a manual `wifi connect`/`wifi auto`, sometimes long after boot).
    // Starting CardputerMirror's AsyncWebServer/AsyncWebSocket before lwIP's
    // tcpip task exists crashes with "assert failed: tcpip_api_call ...
    // Invalid mbox". See tryStartCardputerMirror(), called instead once
    // WiFi.status() actually confirms a network exists (home menu loop).

#if defined(HAS_RESISTIVE_TOUCH)
    if (!loadTouchCalibration()) calibrateTouch();
#endif
    // Gets the config.conf from SD Card and fill out the settings JSON
    getConfigs();
    RAM_LOG("after-getConfigs");
#if defined(HAS_TOUCH)
    TouchFooter2();
#endif

    // Some boards need input polling to stay on the main loop thread because
    // display/touch drivers are not safe to service from a helper task.
    // Must exist before either side can take it; setup() is still single threaded here.
    // Created unconditionally: _getKeyPress() is reachable even in DONT_USE_INPUT_TASK builds.
    launcherInputLockInit();
#ifndef DONT_USE_INPUT_TASK
    xTaskCreate(
        taskInputHandler, // Task function
        "InputHandler",   // Task Name
        3500,             // Stack size
        NULL,             // Task parameters
        2,                // Task priority (0 to 3), loopTask has priority 2.
        &xHandle          // Task handle (not used)
    );
#else
    xHandle = nullptr;
#endif

    // Command interface over the Serial Monitor (nav/reboot/partitions/flash), always
    // on so a host script can steer the Launcher before it auto-boots a queued OTA app.
    static TaskHandle_t serialConsoleHandle;
    xTaskCreate(
        taskSerialConsole, // Task function
        "SerialConsole",   // Task Name
        4096,              // Stack size
        NULL,              // Task parameters
        1,                 // Task priority, below InputHandler/loopTask
        &serialConsoleHandle
    );

    // Start Bootscreen timer
    int i = launcherMillis();
    int j = 0;
    LongPress = true;
    RAM_LOG("before-bootscreen");

#if defined(HAS_KEYBOARD) || defined(USE_CARDKB2)
    std::vector<LauncherAppMetadata> bootApps = launcherListInstalledApps();
#endif

#if defined(USE_CARDKB2) && defined(CARDKB2_SDA) && defined(CARDKB2_SCL)
    if (sdcardMounted) launcherDelayMs(300);
    else launcherDelayMs(100);
    cardkb2_setup(CARDKB2_SDA, CARDKB2_SCL);
#endif

    // Init any device specific hardware after TFT+SD+CardKb
    _late_setup_gpio();

    while (launcherMillis() < i + (2000 + bootToApp * 3000)) { // increased from 2500 to 5000
        initDisplay();                                         // Inicia o display

        if (launcherMillis() > (i + j * 500)) { // Serial message each ~500ms
            launcherConsolePrintln("Press the button to enter the Launcher!");
            j++;
        }
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            LTouchPoint *t = &touchPoint;

            // Tap on one of the app shortcut cards: boot that app directly
            bool shortcutHit = false;
            for (MenuOptions &shortcut : launcherBootAppShortcuts()) {
                if (shortcut.contain(t->x, t->y)) {
                    touchPoint.pressed = false;
                    shortcut.action();
                    shortcutHit = true;
                    break;
                }
            }
            if (shortcutHit) continue;

            // Enable touch the center of the screen to get into Launcher
            int third_x = tftWidth / 3;
            int third_y = tftHeight / 3;
            if (t->x > third_x * 1 && t->x < third_x * 2 && ((t->y > third_y && t->y < third_y * 2))) {
                tft->fillScreen(BGCOLOR);
                touchPoint.pressed = false;
                goto Launcher;
            }
        }
#endif
        // Direct input check for startup - bypass check() function to avoid task suspension
#if defined(HAS_1_BUTTON)
        if (check(SelPress) || check(NextPress))
#else
        if (check(SelPress))
#endif
        {
            tft->fillScreen(BGCOLOR);
            goto Launcher;
        }

#if defined(HAS_KEYBOARD) || defined(USE_CARDKB2)
        keyStroke key = _getKeyPress();
        bool anyKeyTriggered = key.pressed && !key.enter;
#elif defined(HAS_1_BUTTON)
        bool anyKeyTriggered = check(EscPress);
#elif defined(STICK_C_PLUS2) || defined(STICK_C_PLUS)
        bool anyKeyTriggered = check(NextPress);
#else
        bool anyKeyTriggered = check(AnyKeyPress);
#endif
        if (anyKeyTriggered) {
#if defined(HAS_KEYBOARD) || defined(USE_CARDKB2)
            // Digit shortcut: 1..9,0 boots straight into that slot's installed app.
            int appIndex = -1;
            if (!key.word.empty()) {
                char digit = key.word[0];
                if (digit >= '1' && digit <= '9') appIndex = digit - '1';
                else if (digit == '0') appIndex = 9;
            }
            if (appIndex >= 0 && appIndex < static_cast<int>(bootApps.size())) {
                launcherBootAppByLabel(bootApps[appIndex].label.c_str());
                goto Launcher;
            }
#endif
            launcherBootInstalledAppOrShowMenu();
            goto Launcher;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // time to inputTask
    }
    // If nothing is done and there's something installed, launch it
    if (launcherBootCurrentApp()) {
        tft->fillScreen(BGCOLOR);
        _setBrightness(0);

        return (void)releaseHeapObjectsAndReboot();
    }

// If M5 or Enter button is pressed, continue from here
Launcher:
    RAM_LOG("launcher-label");
    LongPress = false;
    tft->fillScreen(BGCOLOR);
#if LED > 0 && defined(HEADLESS)
    launcherGpioWrite(LED, LED_ON ? LOW : HIGH); // turn off the LED
#endif
}

/**********************************************************************
**  Function: loop
**  Main loop
**********************************************************************/
#ifndef HEADLESS
void loop() {
    static bool loggedFirstLoop = false;
    if (!loggedFirstLoop) {
        RAM_LOG("first-loop-start");
        loggedFirstLoop = true;
    }
    bool redraw = true;
    bool update_sd;
    int index = 0;
    int opt = 5; // there are 3 options> 1 list SD files, 2 OTA, 3 USB and 4 Config
    int pass_by = 0;
    bool first_loop = true;
    getBrightness();
    launcherConsolePrintln("Type 'help' for Serial commands.");
    if (!sdcardMounted) index = 1; // if SD card is not present, paint SD square grey and auto select OTA
    std::vector<MenuOptions> menuItems = {
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "SD", "Launch from SDCard",
#else
            "SD",
            "Launch from or mng SDCard",
#endif
         [=]() { loopSD(false); },
         sdcardMounted
        },
#ifndef DISABLE_OTA
        {"OTA", "Online Installer", [=]() { ota_function(); }},
#endif
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "WUI", "Start WebUI",
#else
            "WUI",
            "Start Web User Interface",
#endif
         [=]() { loopOptionsWebUi(); }
        },
#if defined(SOC_USB_OTG_SUPPORTED)
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "USB", "SD->USB",
#else
            "USB",
            "SD->USB Interface",
#endif
         [=]() {
                if (setupSdCard()) {
                    MassStorage();
                    tft->drawPixel(0, 0, 0);
                    tft->fillScreen(BGCOLOR);
                } else {
                    displayError("Insert SD Card");
                }
            }, sdcardMounted
        },
#endif
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "PM"
#else
            "PMan"
#endif
            ,
         "Partition Manager.", [=]() { partList(); }
        },
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "CFG", "Change Settings.",
#else
            "CFG",
            "Change Launcher Settings.",
#endif
         [=]() { settings_menu(); }
        }
    };
    if (first_loop) RAM_LOG("first-mainMenu-built");

    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        String appLabel = app.label;
        String appName = app.name.isEmpty() ? app.label : app.name;
        String appIcon = app.name.substring(0, 5);
        appIcon.toUpperCase();
        menuItems.push_back(
            {appIcon,
             appName,
             [appLabel]() { launcherShowAppActions(appLabel.c_str()); },
             true,
             false,
             0,
             0,
             0,
             0,
             ALCOLOR}
        );
    }

#if !defined(CARDPUTER)
    menuItems.push_back(
        // Add power off option for devices that are not easy to turn off
        // on e-paper, it keeps the Launcher bootscreen printed
        {"OFF", "Turn off Device", [=]() { powerOff(); }}
    );
#endif
    opt = menuItems.size(); // number of options in the menu
    update_sd = sdcardMounted;
    while (1) {
        if (redraw) {
            if (update_sd != sdcardMounted) {
                for (auto o : menuItems) {
                    if (o.name == "SD") o.active = sdcardMounted;
                    if (o.name == "USB") o.active = sdcardMounted;
                }
                update_sd = sdcardMounted;
            }
            if (!dev_mode && pass_by == 5) {
                displayMsg("Dev mode Activated");
                dev_mode = true;
                saveConfigs();
            }
            drawMainMenu(menuItems, index);
#if defined(HAS_TOUCH)
            TouchFooter();
#endif
            redraw = false;
            LongPress = false;
            returnToMenu = false;
            tft->display(false);
            if (first_loop) {
                first_loop = false;
                launcherDelayMs(350);
                resetGlobals(); // avoid leaking command after menu is shown
            }
        }
        if (touchPoint.pressed) {
            int i = 0;
            for (auto item : menuItems) {
                if (item.contain(touchPoint.x, touchPoint.y)) {
                    resetGlobals();
#ifndef E_PAPER_DISPLAY
                    if (i == index) {
                        item.action();
                        tft->drawPixel(0, 0, 0);
                        tft->fillScreen(BGCOLOR);
                    } else {
                        index = i;
                        drawMainMenu(menuItems, index); // Redraw the menu to show the selected item
                        break;
                    }
#else
                    item.action(); // Call the action associated with the selected menu item
#endif
                    returnToMenu = false;
                    redraw = true;
                    goto END;
                }
                i++;
            }
            touchPoint.Clear();
        }
        if (check(PrevPress)) {
            if (index == 0) index = opt - 1;
            else if (index > 0) index--;
            pass_by = 0;
            redraw = true;
        }
        // DW Btn to next item
        if (check(NextPress)) {
            index++;
            if ((index + 1) > opt) {
                index = 0;
                if (!dev_mode) pass_by++;
            }
            redraw = true;
        }
#if defined(HAS_KEYBOARD) || defined(HAS_5_BUTTONS) || defined(USE_CARDKB2)
        auto moveMainMenuRow = [&](int direction) {
            if (tftHeight <= 90) return;

            int cols = (tftHeight > 90) ? 3 : 5;
            int rows = (opt + cols - 1) / cols;
            int targetRow = index / cols + direction;
            if (targetRow < 0) {
                targetRow = rows - 1;
            } else if (targetRow >= rows) {
                targetRow = 0;
            }

            int targetStart = targetRow * cols;
            int targetEnd = targetStart + cols;
            if (targetEnd > opt) targetEnd = opt;

            int currentCenter = menuItems[index].x + menuItems[index].w / 2;
            int nextIndex = targetStart;
            int bestDistance = 0x7fffffff;
            for (int i = targetStart; i < targetEnd; ++i) {
                int candidateCenter = menuItems[i].x + menuItems[i].w / 2;
                int distance = candidateCenter > currentCenter ? candidateCenter - currentCenter
                                                               : currentCenter - candidateCenter;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    nextIndex = i;
                }
            }

            index = nextIndex;
            pass_by = 0;
            redraw = true;
        };

        if (check(UpPress)) { moveMainMenuRow(-1); }
        if (check(DownPress)) { moveMainMenuRow(1); }
#endif

        // Select and run function
        if (check(SelPress)) {
            menuItems.at(index).action(); // Call the action associated with the selected menu item
            tft->drawPixel(0, 0, 0);
            tft->fillScreen(BGCOLOR);
            pass_by = 0;
            returnToMenu = false;
            redraw = true;
            goto END;
        }

#if defined(HAS_KEYBOARD)
        // Boot a file bound to a keyboard shortcut (Settings -> Manage shortcuts / SD "Bind to key")
        {
            keyStroke key = _getKeyPress();
            if (key.pressed && !key.enter && !key.exit_key && !key.word.empty()) {
                char pressedChar = key.word[0];
                if (pressedChar >= 'A' && pressedChar <= 'Z') pressedChar += ('a' - 'A');
                String pressedKey = String(pressedChar);
                String boundPath;
                if (sdcardMounted && getKeyBinding(pressedKey, boundPath)) {
                    if (SDM.exists(boundPath)) {
                        updateFromSD(boundPath); // reboots on success; only returns on failure
                    } else {
                        displayError("File not found");
                        removeKeyBinding(pressedKey);
                        displayMsg(String("'") + pressedKey + "' removed");
                    }
                    tft->drawPixel(0, 0, 0);
                    tft->fillScreen(BGCOLOR);
                    pass_by = 0;
                    returnToMenu = false;
                    redraw = true;
                    goto END;
                }
            }
        }
#endif
        checkReboot();
#if defined(CARDPUTER)
        // launcher-adv-mirror ADR 0001/0004. This while(1) is the home menu's
        // own per-iteration tick -- it owns loopTask almost entirely while
        // idling here, so the outer loop()'s END: label (below) is only
        // reached on specific goto transitions, not every idle iteration.
        // Discovered debugging: the mirror's server never became reachable
        // and a diagnostic heartbeat never printed while sitting at the home
        // menu, because CardputerMirror.update() (at END:) was never being
        // called at all in that state. Calling it here too -- same task as
        // drawMainMenu()/tft->display() above, which is exactly what ADR
        // 0002's safety rule requires (readback must run on the same task as
        // the application's own draws, never a separate one).
        tryStartCardputerMirror(); // no-ops after the first successful attempt
        CardputerMirror.update();
        {
            static uint32_t lastBeat = 0;
            const uint32_t now2 = launcherMillis();
            if (now2 - lastBeat > 3000) {
                lastBeat = now2;
                launcherConsolePrintf(
                    "[cardputermirror] ok=%s step=%s ip=%s clients=%d frames=%lu\n",
                    g_mirrorOk ? "true" : "FALSE",
                    CardputerMirror.debugBeginStep(),
                    CardputerMirror.ipAddress().c_str(),
                    CardputerMirror.clientCount(),
                    (unsigned long)CardputerMirror.framesSent()
                );
            }
        }
#endif
    }

END:
#if defined(CARDPUTER)
    // launcher-adv-mirror ADR 0001. Unconditional -- every loop() path
    // (normal fall-through and every `goto END`) converges here, same
    // guarantee cardputer-adv-mirror's own standalone example relies on.
    tryStartCardputerMirror(); // no-ops after the first successful attempt
    CardputerMirror.update();
    // Periodic, not one-shot: the ESP32-S3's native USB CDC re-enumerates on
    // reset, so a monitor that attaches even slightly late misses one-shot
    // setup() prints entirely -- learned the hard way debugging this exact
    // integration. cardputer-adv-mirror's own standalone example uses the
    // same pattern for the same reason.
    {
        static uint32_t lastBeat = 0;
        const uint32_t now = launcherMillis();
        if (now - lastBeat > 3000) {
            lastBeat = now;
            launcherConsolePrintf(
                "[cardputermirror] ok=%s step=%s ip=%s clients=%d frames=%lu\n",
                g_mirrorOk ? "true" : "FALSE",
                CardputerMirror.debugBeginStep(),
                CardputerMirror.ipAddress().c_str(),
                CardputerMirror.clientCount(),
                (unsigned long)CardputerMirror.framesSent()
            );
        }
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
}

#else
void loop() { // Start SD card, If there's no SD Card installed, see if there's ssid saved on memory,
    RAM_LOG("headless-loop-start");
    launcherConsolePrint(
        "     _                            _               \n"
        "    | |                          | |              \n"
        "    | |     __ _ _   _ _ __   ___| |__   ___ _ __ \n"
        "    | |    / _` | | | | '_ \\ / __| '_ \\ / _ \\ '__|\n"
        "    | |___| (_| | |_| | | | | (__| | | |  __/ |   \n"
        "    |______\\__,_|\\__,_|_| |_|\\___|_| |_|\\___|_|   \n"
        "    ----------------------------------------------\n"

        "Welcome to Launcher, an ESP32 firmware where you can have\n"
        "a better control on what you are running on it.\n\n"
        "Now it will Start a web interface, where you can flash a new\n"
        "firmware on a dedicated partition, and swap it whenever you\n"
        "want using this Launcher.\n\n\n"
    );

    getConfigs();
    launcherConsolePrintln("Scanning networks...");
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    bool mode_ap = true;

    if (sdcardMounted) {
        JsonObject setting = settings[0];
        JsonArray WifiList = setting["wifi"].as<JsonArray>();
        for (int i = 0; i < nets; i++) {
            String networkSsid = networks[i].ssid.c_str();
            for (auto wifientry : WifiList) {
                launcherConsolePrintf("Target: %s Network: %s\n", ssid.c_str(), networkSsid.c_str());
                if (networkSsid == wifientry["ssid"].as<String>()) {
                    ssid = wifientry["ssid"].as<String>();
                    pwd = wifientry["pwd"].as<String>();
                    int count = 0;
                    launcherConsolePrintf("Connecting to %s\n", ssid.c_str());
                    LauncherWifiConnectState connectState = LauncherWifiConnectState::Pending;
                    while (connectState != LauncherWifiConnectState::Connected) {
                        connectState = launcherWifiConnectStatus(ssid.c_str(), pwd.c_str(), 500);
                        if (connectState == LauncherWifiConnectState::Connected) break;
                        if (connectState == LauncherWifiConnectState::WrongPassword) {
                            launcherConsolePrintln("Wrong Password");
                            break;
                        }
                        vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                        launcherGpioWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                        launcherConsolePrint(".");
                        count++;
                        if (connectState == LauncherWifiConnectState::Failed || count > 20) {
                            break; // stops trying this network, will try the others, if there are some other
                                   // with same SSID
                        }
                    }
                    if (!launcherWifiIsConnected()) { saveIntoNVS(); }
                }
            }
        }
    } else if (ssid != "") { // will try to connect to a saved network
        for (int i = 0; i < nets; i++) {
            String networkSsid = networks[i].ssid.c_str();
            launcherConsolePrintf("Target: %s Network: %s\n", ssid.c_str(), networkSsid.c_str());
            if (ssid == networkSsid) {
                launcherConsolePrintln("Network matches the SSID, starting connection\n");
                int count = 0;
                launcherConsolePrintf("Connecting to %s\n", ssid.c_str());
                LauncherWifiConnectState connectState = LauncherWifiConnectState::Pending;
                while (connectState != LauncherWifiConnectState::Connected) {
                    connectState = launcherWifiConnectStatus(ssid.c_str(), pwd.c_str(), 500);
                    if (connectState == LauncherWifiConnectState::Connected) break;
                    if (connectState == LauncherWifiConnectState::WrongPassword) {
                        launcherConsolePrintln("Wrong Password");
                        break;
                    }
                    vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                    launcherGpioWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                    launcherConsolePrint(".");
                    count++;
                    if (connectState == LauncherWifiConnectState::Failed || count > 20) {
                        break; // stops trying this network, will try the others, if there are some other with
                               // same SSID, it can take quite sometime :/
                    }
                }
            }
        }
    } else {
        launcherConsolePrintln(
            "Couldn't find SD Card and SSID Saved,\n"
            "you can configure it on the WEB Ui,\n\n"
            "Starting the Launcher in Access point mode\n"
            "Connect into the following network\n"
            "with no other network (mobile data off and unplug wired connections)"
        );
    }

    // if there's no network information, open in Access Point Mode
    if (launcherWifiIsConnected()) mode_ap = false;

    startWebUi("", 0, mode_ap);

    launcherConsolePrintln("Type 'help' for Serial commands.");

    // sorfware will keep trapped in startWebUi loop..
}
#endif
