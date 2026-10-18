#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/ui/DisplayPowerHold.h>
#include "MyMesh.h"

// ESP32 companion power saving. See agent_docs/power.md.
//
// The mechanism is automatic light sleep, not a manual sleep call: the BLE
// controller keeps the connection and wakes the CPU for a connection event,
// while the application yields so the FreeRTOS idle task can hand the CPU over.
// ESP32Board::sleep() is deliberately NOT used for a companion — it wakes only
// on the radio DIO pin and a timer, so BLE connection events would be missed and
// the app would see the node drop off.
//
// This needs a framework built with CONFIG_PM_ENABLE and FreeRTOS tickless idle.
// Stock arduino-esp32 2.0.17 (platformio/espressif32@6.11.0) ships both
// disabled, so esp_pm_configure() returns ESP_ERR_NOT_SUPPORTED there: the code
// below then only reports the failure and the node behaves exactly as before.
#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
  #if defined(WITH_ESP32_PM_REQUIRED)
    // A *_ps environment: refuse to build a "power saving" image on a core that
    // cannot sleep. Install the PS core once: python tools/ps_framework.py
    #include "sdkconfig.h"
    #if !CONFIG_PM_ENABLE
      #error "WITH_ESP32_PM_REQUIRED: framework built without CONFIG_PM_ENABLE - run: python tools/ps_framework.py"
    #endif
  #endif
  #include "esp_pm.h"
  #include "esp_bt.h"
  #include "esp_sleep.h"
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT \
      && !(defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE)
    #include "USB.h"   // ESPUSB: TinyUSB "mounted" state for the USB guard
  #endif

  #define ESP32_PM_CPU_MAX_MHZ   80
  #define ESP32_PM_CPU_MIN_MHZ   40
  // How long the idle path yields to the FreeRTOS idle task: this is what actually
  // lets the chip sleep, and how long depends on the profile.
  #define ESP32_PM_IDLE_YIELD_MS            10
  #define ESP32_PM_IDLE_YIELD_AGGRESSIVE_MS 20
  // How long one press of the user button keeps the CPU awake — enough to read the
  // console and type a CLI command.
  #define ESP32_PM_USB_HOLD_MS   30000
  // Awake window right after boot: a native-USB link has to enumerate before the
  // guard can see the host, and a chip that sleeps first never gets enumerated.
  #define ESP32_PM_BOOT_HOLD_MS  30000

  // Boards whose Serial runs over the ESP32-S3's own USB peripheral cannot sleep
  // while a host is attached: that link is serviced by the CPU, so automatic light
  // sleep drops it — not just the log, the port itself. But the transport here is
  // BLE, so the port is only logs and CLI, while the node spends its life on a
  // battery with nothing plugged in. So light sleep stays enabled, and two things
  // keep the console usable:
  //   * the link is held awake automatically while a USB host is detected;
  //   * a press of the user button holds the CPU awake for a window — the fallback
  //     for when detection cannot tell, and the way in when the cable appeared
  //     while the node was asleep (USB cannot wake the CPU, but BLE and LoRa
  //     traffic wakes it constantly, so the press is noticed almost at once).
  // WITH_ESP32_POWER_SAVING_USB_SERIAL_SAFE restores the conservative rule: no
  // light sleep at all on such boards, matching the fork's PS 17.1.5 choice.
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    #define ESP32_PM_NATIVE_USB    1
  #else
    #define ESP32_PM_NATIVE_USB    0
  #endif

  // WITH_WIFI_SWITCHING is not in the list: there the transport is chosen at runtime,
  // so light sleep stays configured and esp32_pm_wantsAwake() holds the lock while
  // the active transport is WiFi or USB.
  #if defined(WIFI_SSID) || (defined(ENABLE_USB_INTERFACE) && !defined(WITH_WIFI_SWITCHING)) || defined(SERIAL_RX) || defined(ETHERNET_ENABLED)
    // The transport itself is WiFi, USB, UART or Ethernet: sleeping breaks the link,
    // not just the log.
    #define ESP32_PM_LIGHT_SLEEP   0
    #define ESP32_PM_USB_GUARD     0
  #elif ESP32_PM_NATIVE_USB && !defined(WITH_ESP32_POWER_SAVING_USB_SERIAL_SAFE)
    #define ESP32_PM_LIGHT_SLEEP   1
    #define ESP32_PM_USB_GUARD     1
  #else
    #define ESP32_PM_LIGHT_SLEEP   1
    #define ESP32_PM_USB_GUARD     0
  #endif
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

// interface manager
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface (uni builds have no BLE_PIN_CODE: the PIN comes from prefs)
#if defined(BLE_PIN_CODE) || defined(WITH_WIFI_SWITCHING)
  #ifdef ESP32
    // include esp32 bluetooth interface
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    // include nrf52 bluetooth interface
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(RP2040_PLATFORM)
    // include rp2040 (Pico W / CYW43) bluetooth interface
    #include <helpers/rp2040/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  #ifndef WIFI_PWD
    #define WIFI_PWD ""
  #endif
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #ifndef WIFI_RETRY_INTERVAL
    #if defined(RP2040_PLATFORM)
      #define WIFI_RETRY_INTERVAL 30000   // each attempt blocks loop(), so retry less often
    #else
      #define WIFI_RETRY_INTERVAL 10000   // millis between reconnect attempts
    #endif
  #endif
  #ifndef WIFI_RETRY_TIMEOUT
    #define WIFI_RETRY_TIMEOUT 5000     // RP2040: cap on how long one join may block loop()
  #endif
  #if defined(ESP32) || defined(RP2040_PLATFORM)
    #include <helpers/wifi/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE) && defined(WITH_WIFI_SWITCHING)
  #include "UsbLinkInterface.h"
  UsbLinkInterface usb_serial_interface;
#elif defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface usb_serial_interface;
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

// platform file system
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

/* GLOBAL OBJECTS */
#include "PowerScreens.h"
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

/* WIFI RECONNECT TRACKERS */
#ifdef WITH_WIFI_SWITCHING
static void uniApplyCommsMode(uint8_t mode);
#endif
#ifdef ENABLE_WIFI_INTERFACE
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
  char wifi_ssid[33] = WIFI_SSID;   // replaced by stored prefs at boot, if set
  char wifi_pwd[64] = WIFI_PWD;
  bool wifi_was_connected = false;
  bool wifi_enabled = false;   // set at boot from prefs; false also when the effective SSID is blank
#endif

#ifdef WITH_WIFI_SWITCHING
// uni transport modes: BLE alone (the chip may sleep), BLE and WiFi together,
// or USB alone. Every transport stays registered in interface_manager; the mode
// only enables and disables them, so a change needs no reboot.
static void uniApplyCommsMode(uint8_t mode) {
  const bool wifi = mode == COMMS_MODE_WIFI;
  const bool usb = mode == COMMS_MODE_USB;
  WifiPrefs* wp = the_mesh.getWifiPrefs();

  if (usb) bluetooth_interface.disable();
  else     bluetooth_interface.enable();

  if (wifi && wp->network_count > 0) {
    StrHelper::strncpy(wifi_ssid, wp->networks[0].ssid, sizeof(wifi_ssid));
    StrHelper::strncpy(wifi_pwd, wp->networks[0].password, sizeof(wifi_pwd));
    WiFi.mode(WIFI_STA);
    wifi_interface.begin(wp->tcp_port);   // no-op once listening; a new port needs a reboot
    if (wp->ip_mode == IP_MODE_STATIC) {
      IPAddress ip, gw, mask;
      ip.fromString(wp->static_ip);
      gw.fromString(wp->static_gw);
      mask.fromString(wp->static_mask);
      WiFi.config(ip, gw, mask);
    } else {
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);   // back to DHCP after a static IP
    }
    WiFi.begin(wifi_ssid, wifi_pwd);
    last_wifi_reconnect_attempt = millis();
    wifi_interface.enable();
    wifi_enabled = true;
  } else {
    wifi_enabled = false;
    wifi_interface.disable();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (usb) usb_serial_interface.enable();
  else     usb_serial_interface.disable();

  board.setInhibitSleep(wifi || usb);
}
#endif

#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
// Power saving is the feature; the profile says how far to take it. The lock below
// is held whenever the CPU must stay awake instead, and three things ask for that:
// a USB host on the line (that link is serviced by the CPU), the app connected in
// the CONSERVATIVE profile, and a button-granted window for reading the console.
static esp_pm_lock_handle_t esp32_pm_lock = NULL;      // ESP_PM_NO_LIGHT_SLEEP
static esp_pm_lock_handle_t esp32_pm_apb_lock = NULL;  // ESP_PM_APB_FREQ_MAX
static bool esp32_pm_lock_held = false;
static bool esp32_pm_apb_lock_held = false;
static uint32_t esp32_pm_hold_until = 0;
static bool esp32_pm_window_active = false;
static uint16_t esp32_pm_yield_ms = ESP32_PM_IDLE_YIELD_MS;
static uint8_t esp32_pm_applied_mode = 0xFF;
// Depth of panel work in flight that must not be interrupted by sleep.
static uint16_t esp32_display_hold = 0;

#if ESP32_PM_USB_GUARD
static bool esp32_usbHostPresent() {
  #if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
    return Serial.isConnected();   // HWCDC: hardware USB-Serial-JTAG link
  #else
    // USBCDC over TinyUSB: the host has enumerated and configured the device.
    // Not (bool)Serial — that only turns true once a terminal opens the port with
    // DTR and RTS, so the chip slept before that and the link died (seen on E290).
    return (bool)USB;
  #endif
}
#endif

static uint16_t esp32_pm_yieldFor(uint8_t mode) {
  return (mode == POWER_MODE_AGGRESSIVE) ? ESP32_PM_IDLE_YIELD_AGGRESSIVE_MS
                                         : ESP32_PM_IDLE_YIELD_MS;
}

static bool esp32_pm_wantsAwake() {
  if (ESP32_PM_LIGHT_SLEEP == 0) return false;   // this transport forbids sleeping
  #if defined(WITH_WIFI_SWITCHING)
    if (the_mesh.commsForbidsSleep()) return true;
  #endif
  // An e-ink panel drives its waveform for seconds: sleeping through it leaves the
  // screen stuck black. The drivers report panel work in flight (see
  // DisplayPowerHold.h), and that has to outrank every other consideration.
  if (esp32_display_hold > 0) return true;
  #if defined(DISPLAY_CLASS) && defined(WITH_ASYNC_EINK)
    // An async panel is pumped from the loop in stages, but its waveform runs the
    // whole time: sleeping between stages would break the refresh just as it does
    // inside one blocking call.
    if (display.isRefreshBusy()) return true;
  #endif
  #if ESP32_PM_USB_GUARD
    if (esp32_usbHostPresent()) return true;
  #endif
  if (the_mesh.getNodePrefs()->power_mode == POWER_MODE_CONSERVATIVE
      && the_mesh.isAppConnected()) {
    return true;
  }
  #if ENV_INCLUDE_GPS == 1
    // GPS runs over UART (Tracker 115200, v4): light sleep drops NMEA bytes, so stay
    // awake while the GPS is enabled in prefs (see applyGpsPrefs()).
    if (the_mesh.getNodePrefs()->gps_enabled) return true;
  #endif
  return esp32_pm_window_active;
}

// The NO_LIGHT_SLEEP lock above only stops light sleep; it does not stop DFS from
// dropping the CPU to the XTAL frequency. On ESP32-S3 that also powers the BBPLL
// down, and BBPLL clocks the chip's own USB-Serial-JTAG (HWCDC) and USB-OTG
// (TinyUSB) — so the console dies even though the guard "holds". Keep APB at max
// while a host is on the line or a button window is open, and for the whole session
// when the *transport* is native USB (there DFS is never safe).
static bool esp32_pm_wantsApbMax() {
  #if ESP32_PM_NATIVE_USB
    #if ESP32_PM_USB_GUARD
      if (esp32_usbHostPresent()) return true;
    #endif
    if (esp32_pm_window_active) return true;
  #endif

  #if defined(ENABLE_USB_INTERFACE) && ESP32_PM_LIGHT_SLEEP == 0 && ESP32_PM_NATIVE_USB
    return true;
  #endif

  return false;
}

static void esp32_servicePmHold() {
  #if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
    // Active low with a pull-up. One press buys a window with the CPU awake, so the
    // console can be read even when host detection says nothing is attached.
    if (digitalRead(PIN_USER_BTN) == LOW) {
      esp32_pm_window_active = true;
      esp32_pm_hold_until = millis() + ESP32_PM_USB_HOLD_MS;
    }
  #endif

  // The window is a flag, not a signed difference: millis() wraps every ~49.7 days,
  // and a naive (int32_t)(hold_until - millis()) > 0 turns positive again ~24.8 days
  // after the wrap, holding the lock with no sleep. Clear the flag once the deadline
  // has passed; the unsigned subtraction is correct across the millis() wrap.
  if (esp32_pm_window_active && ((int32_t)(millis() - esp32_pm_hold_until) >= 0)) {
    esp32_pm_window_active = false;
  }

  if (esp32_pm_lock != NULL) {
    const bool hold = esp32_pm_wantsAwake();
    if (hold != esp32_pm_lock_held) {
      if (hold) {
        esp_pm_lock_acquire(esp32_pm_lock);
      } else {
        esp_pm_lock_release(esp32_pm_lock);
      }
      esp32_pm_lock_held = hold;
    }
  }

  if (esp32_pm_apb_lock != NULL) {
    const bool apb = esp32_pm_wantsApbMax();
    if (apb != esp32_pm_apb_lock_held) {
      if (apb) {
        esp_pm_lock_acquire(esp32_pm_apb_lock);
      } else {
        esp_pm_lock_release(esp32_pm_apb_lock);
      }
      esp32_pm_apb_lock_held = apb;
    }
  }
}

// Panel work must not be interrupted by sleep — the drivers bracket their refresh
// with these (see DisplayPowerHold.h). Kept as a counter: a driver may refresh from
// within another panel call (clear() from begin(), for instance).
void displayPowerHoldBegin() {
  esp32_display_hold++;
  esp32_servicePmHold();
}

void displayPowerHoldEnd() {
  if (esp32_display_hold > 0) esp32_display_hold--;
  esp32_servicePmHold();
}

// Apply the profile: at startup, and again whenever the setting changes, because
// the CLI can flip it while the node is running.
static void esp32_applyPowerMode(uint8_t mode) {
  esp32_pm_yield_ms = esp32_pm_yieldFor(mode);

  // IDF 4.4 wants a per-target config struct; IDF 5 targets (C6) take the generic one.
  #if CONFIG_IDF_TARGET_ESP32C3
    esp_pm_config_esp32c3_t pm_config;
  #elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config;
  #elif CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config;
  #elif CONFIG_IDF_TARGET_ESP32C6
    esp_pm_config_t pm_config;
  #else
    #error "No esp_pm_config_t for this target"
  #endif

  if (mode == POWER_MODE_OFF) {
    // "off" means "as before the feature": a fixed CPU frequency with no DFS and no
    // light sleep. min == max disables DFS, so an on-the-fly off -> auto switch
    // re-applies scaling on the next esp_pm_configure() call.
    pm_config.max_freq_mhz = ESP32_PM_CPU_MAX_MHZ;
    pm_config.min_freq_mhz = ESP32_PM_CPU_MAX_MHZ;
    pm_config.light_sleep_enable = false;
  } else {
    pm_config.max_freq_mhz = ESP32_PM_CPU_MAX_MHZ;
    pm_config.min_freq_mhz = ESP32_PM_CPU_MIN_MHZ;
    pm_config.light_sleep_enable = (ESP32_PM_LIGHT_SLEEP != 0);
  }

  esp_err_t err_pm = esp_pm_configure(&pm_config);
  esp32_pm_applied_mode = mode;
  if (err_pm == ESP_OK) {
    Serial.printf("ESP32 PM: %d-%d MHz, light sleep %s, mode %u\n",
                  (int)pm_config.max_freq_mhz, (int)pm_config.min_freq_mhz,
                  pm_config.light_sleep_enable ? "on" : "off", (unsigned)mode);
  } else {
    Serial.printf("ESP32 PM: unavailable (%d) - framework built without CONFIG_PM_ENABLE\n",
                  (int)err_pm);
  }
}

static void esp32_servicePowerPrefs() {
  const uint8_t mode = the_mesh.getNodePrefs()->power_mode;
  if (mode != esp32_pm_applied_mode) {
    esp32_applyPowerMode(mode);
  }
}

static void esp32_setupPower() {
  #if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
  #endif
  #if ESP32_PM_NATIVE_USB
    // Boot window: let a native-USB link enumerate before the guard decides.
    esp32_pm_window_active = true;
    esp32_pm_hold_until = millis() + ESP32_PM_BOOT_HOLD_MS;
  #endif
  // Take the locks BEFORE light sleep and DFS are switched on, so a USB port is
  // never dropped in the first moments.
  if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "power", &esp32_pm_lock) != ESP_OK) {
    esp32_pm_lock = NULL;
    Serial.println("ESP32 PM: no lock available");
  } else if (esp32_pm_wantsAwake()) {
    esp_pm_lock_acquire(esp32_pm_lock);
    esp32_pm_lock_held = true;
  }

  if (esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "power_apb", &esp32_pm_apb_lock) != ESP_OK) {
    esp32_pm_apb_lock = NULL;
    Serial.println("ESP32 PM: no APB lock available");
  } else if (esp32_pm_wantsApbMax()) {
    esp_pm_lock_acquire(esp32_pm_apb_lock);
    esp32_pm_apb_lock_held = true;
  }

  // LoRa wake-up is the yield timer plus DIO1 polling: no ext1 on DIO1. IDF 4.4
  // moves ext1 pins into the RTC domain before every sleep and does not return
  // them to digital after light sleep, which breaks the RadioLib interrupt and the
  // DIO1 level read. A front that fired while asleep is recovered by
  // radio_driver.recoverMissedDioInterrupt() in loop().

  esp32_applyPowerMode(the_mesh.getNodePrefs()->power_mode);
}
#endif

void setup() {
  board.feedWatchdog();   // a soft reset leaves a started watchdog running
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    power_screens::drawBoot(disp);
  }
#endif

  // A battery too flat to run is caught here, before the radio and the rest of
  // the start-up draw their peak current: otherwise a nearly empty cell browns
  // the board out during boot and it restarts in a loop, silently. Two
  // readings apart, so one noisy sample does not turn the node off.
  if (power_screens::batteryFlat(board.getBattMilliVolts(), board.isExternalPowered())) {
    delay(200);
    if (power_screens::batteryFlat(board.getBattMilliVolts(), board.isExternalPowered())) {
#ifdef DISPLAY_CLASS
      if (disp) power_screens::drawPowerOff(disp, true);
#endif
#if defined(NRF52_POWER_MANAGEMENT)
      static_cast<NRF52Board&>(board).initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
#endif
      board.powerOff();
    }
  }

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin();
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin();
#else
  #error "need to define filesystem"
#endif

#ifdef BLE_PIN_CODE // 123456 by default
  if (the_mesh.getNodePrefs()->ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (disp != NULL && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      the_mesh.setBLEPin(rng.nextInt(100000, 999999)); // random pin each session
    } else {
      the_mesh.setBLEPin(BLE_PIN_CODE); // otherwise static pin
    }
#else
    the_mesh.setBLEPin(BLE_PIN_CODE); // otherwise static pin
#endif
  } else {
    the_mesh.setBLEPin(the_mesh.getNodePrefs()->ble_pin);
  }
#else
  the_mesh.setBLEPin(0);
#endif

#ifdef WITH_WIFI_SWITCHING
  the_mesh.initCommsFromPrefs();   // uni: transport settings and the BLE PIN
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE) || defined(WITH_WIFI_SWITCHING)
  bluetooth_interface.begin(blePrefixEnabled(the_mesh.getNodePrefs()) ? BLE_NAME_PREFIX : "",
                            the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
#endif

// add wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  // use wifi ssid and password from prefs if ssid is not empty, otherwise use the build flag defaults
  if (the_mesh.getNodePrefs()->wifi_ssid[0]) {
    strcpy(wifi_ssid, the_mesh.getNodePrefs()->wifi_ssid);
    strcpy(wifi_pwd, the_mesh.getNodePrefs()->wifi_pwd);
  }
  // only start wifi if enabled and ssid is not empty
  wifi_enabled = the_mesh.getNodePrefs()->wifi_enabled;
#ifdef WITH_WIFI_SWITCHING
  // uni: the interface is always registered; the mode turns WiFi on and off
  // at runtime (uniApplyCommsMode below), not only at boot.
  // The TCP server starts in uniApplyCommsMode(), once WiFi is up: before the
  // first WiFi.mode() the TCP/IP stack does not exist and opening a socket aborts.
  interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          wifi_needs_reconnect = false;
      }
  });
  if (false) {
#else
  if (wifi_enabled && wifi_ssid[0]) {
#endif
#if defined(ESP32)
    board.setInhibitSleep(true);   // prevent sleep when WiFi is active
    WiFi.setAutoReconnect(true);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            WIFI_DEBUG_PRINTLN("WiFi disconnected (reason=%s). Flagging for reconnect...",
              WiFi.disconnectReasonName((wifi_err_reason_t)info.wifi_sta_disconnected.reason));
            wifi_needs_reconnect = true;
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
            wifi_needs_reconnect = false;
        }
    });
#endif

    WIFI_DEBUG_PRINTLN("connecting to %s", wifi_ssid);

#if defined(RP2040_PLATFORM)
    // the join itself blocks inside the core (CYW43::begin busy-waits for the
    // association), so every attempt stalls the mesh loop. beginNoBlock() only skips the
    // extra DHCP wait. Give the first connect a full window, then bound the retries below.
    WiFi.beginNoBlock(wifi_ssid, wifi_pwd);
    last_wifi_reconnect_attempt = millis();   // let DHCP finish before the poll can retry
#else
    WiFi.begin(wifi_ssid, wifi_pwd);
#endif
    wifi_interface.begin(TCP_PORT);
    interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
  } else {
    WIFI_DEBUG_PRINTLN("wifi disabled");
  }
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  ethernet_interface.begin();
  interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

  the_mesh.startInterface(interface_manager);
#ifdef WITH_WIFI_SWITCHING
  the_mesh.setCommsHook(uniApplyCommsMode);
  uniApplyCommsMode(the_mesh.getWifiPrefs()->comms_mode);
#endif
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
  the_mesh.setListener(&ui_task);
#endif

  board.onBootComplete();

#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
  #if defined(BLE_PIN_CODE)
    // Controller-side modem sleep: the radio sleeps between connection events.
    // On ESP32-S3/C3 this only exists if the framework was built with
    // CONFIG_BT_CTRL_MODEM_SLEEP, hence the report rather than an abort.
    esp_err_t err_bt = esp_bt_sleep_enable();
    if (err_bt != ESP_OK) {
      Serial.printf("ESP32 PM: BLE sleep not available (%d)\n", (int)err_bt);
    }
  #endif

  esp32_setupPower();
#endif
#if defined(ESP32)
  Serial.printf("Setup complete! Free heap: %u, Max block: %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
#endif
}

void loop() {
  board.feedWatchdog();
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#if defined(DISPLAY_CLASS) && defined(WITH_ASYNC_EINK)
  if (!the_mesh.isRadioTxBusy()) display.service();
#endif
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
  board.loop();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

  #if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
    // Cheap: a couple of register reads and bool compares. Runs while the node is
    // busy too, so both the lock state and the profile always match the settings.
    esp32_servicePowerPrefs();
    esp32_servicePmHold();
    radio_driver.recoverMissedDioInterrupt();
  #endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    // nrf ignores the seconds param and sleeps whenever possible; the profile only
    // says whether to stay awake while the app is connected, or not to sleep at all.
    // AUTO is what this platform always did.
    const uint8_t power_mode = the_mesh.getNodePrefs()->power_mode;
    bool stay_awake = false;
#ifdef DISPLAY_CLASS
    // Экран шума — режим измерения: шаг дорожки задаётся временем, поэтому между
    // пробуждениями спать нельзя, иначе интервал растягивается в разы.
    stay_awake = ui_task.isNoiseMeasurementActive();
#endif
    if (!stay_awake && power_mode != POWER_MODE_OFF
        && (power_mode != POWER_MODE_CONSERVATIVE || !the_mesh.isAppConnected())) {
      board.sleep(0);
    }
#elif defined(ESP32) && defined(WITH_ESP32_POWER_SAVING) && ESP32_PM_LIGHT_SLEEP
    // Walking the loop as fast as it can keeps the CPU out of light sleep; the
    // SX1262 TCXO stays up through ESP32 light sleep, so there is no TCXO start
    // cost per wake-up. Yielding to the idle task is what actually lets the chip
    // sleep; while a received frame is still queued the loop keeps running instead
    // so the frame is handed over promptly.
    // With WiFi or USB active the lock keeps the chip awake anyway, so the yield
    // would only add latency to a link interface_manager does not even see.
    if (!interface_manager.isReadBusy() && !interface_manager.isWriteBusy()
  #if defined(WITH_WIFI_SWITCHING)
        && !the_mesh.commsForbidsSleep()
  #endif
       ) {
      vTaskDelay(pdMS_TO_TICKS(esp32_pm_yield_ms));
    }
#endif
  }

#ifdef ENABLE_WIFI_INTERFACE
  if (wifi_enabled) {
    // RP2040 has no WiFi event callbacks, so poll the link state instead
  #if defined(RP2040_PLATFORM)
      wifi_needs_reconnect = (WiFi.status() != WL_CONNECTED);
      if (wifi_was_connected == wifi_needs_reconnect) {   // link state changed
        wifi_was_connected = !wifi_needs_reconnect;
        if (wifi_was_connected) {
          WIFI_DEBUG_PRINTLN("connected, listening on %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
        } else {
          WIFI_DEBUG_PRINTLN("link lost");
        }
      }
  #endif

    // Safely attempt to reconnect if flagged. On RP2040 each attempt blocks the mesh loop
    // for up to WIFI_RETRY_TIMEOUT, so retry less often and cap how long a join may stall.
    if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > WIFI_RETRY_INTERVAL)) {
      WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect to %s (status %d)...", wifi_ssid, WiFi.status());
    #if defined(RP2040_PLATFORM)
        WiFi.setTimeout(WIFI_RETRY_TIMEOUT);
        WiFi.beginNoBlock(wifi_ssid, wifi_pwd);   // no reconnect() on this platform
    #else
        WiFi.disconnect();
        WiFi.reconnect();
    #endif
      last_wifi_reconnect_attempt = millis();
    }
  }
#endif
}
