#pragma once

#include <Arduino.h>
#include <Mesh.h>

/*------------ Frame Protocol --------------*/
#define FIRMWARE_VER_CODE 14

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "15 Oct 2026"
#endif

#ifndef FIRMWARE_VERSION
  #ifdef GIT_COMMIT
    #define FIRMWARE_VERSION "v1.18.0d5-" GIT_COMMIT
  #else
    #define FIRMWARE_VERSION "v1.18.0d5"
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"

#ifdef WITH_WIFI_SWITCHING
  #if !defined(ENABLE_WIFI_INTERFACE) || !defined(ENABLE_USB_INTERFACE)
    #error "uni (WITH_WIFI_SWITCHING) runs on the upstream transports: add ENABLE_WIFI_INTERFACE and ENABLE_USB_INTERFACE"
  #endif
  #include "WifiPrefs.h"
  #include <WiFi.h>
#endif

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/OptionalFeatureFlags.h>
#include <helpers/BaseSerialInterface.h>
#ifdef WITH_COMPANION_FRAME_FRAGMENTS
#include <helpers/CompanionFrameFragments.h>
#endif
#include <helpers/IdentityStore.h>
#include <helpers/OfflineRecordRing.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/TimeSyncHelper.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif
#ifdef WITH_COMPANION_FRAME_FRAGMENTS
#ifndef OFFLINE_LONG_FRAME_SLOTS
// Eight long frames can sit in the queue at once. A burst of long messages
// evicts its own predecessors from RAM otherwise; each slot is
// companion_frames::MAX_ORIGINAL_LEN bytes, so eight of them cost about 2 KB.
#define OFFLINE_LONG_FRAME_SLOTS 8
#endif
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>

#ifdef WITH_COMPANION_CLI
#include <helpers/CommonCLI.h>
#include "CompanionCLICallbacks.h"
#define TERMINAL_CLI_PSK  "VGVybWluYWxDTEkxMjM0NQ=="  // "TerminalCLI12345" — exactly 16 bytes
#endif

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

// Copied from simple_repeater
#define CTL_TYPE_NODE_DISCOVER_REQ      0x80
#define CTL_TYPE_NODE_DISCOVER_RESP     0x90

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  TimeSyncHelper _ts;

  enum TimeSource : uint8_t {
    TIME_SOURCE_UNSET = 0,
    TIME_SOURCE_CONTACTS,
    TIME_SOURCE_ADVERT,
    TIME_SOURCE_APP,
    TIME_SOURCE_RTC,
    TIME_SOURCE_GPS
  };


  class Listener {
    public:
      virtual void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) = 0;
      virtual void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) = 0;
      virtual void onQueueSizeChanged(int msgcount) = 0;
      virtual void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) { }
      virtual void onControlDataRecv(const mesh::Packet* pkt) { }
      virtual void onChannelDataRecv(mesh::Packet *pkt, const mesh::GroupChannel &channel, uint16_t data_type,
                                     const uint8_t *data, size_t data_len) { }
      virtual void onACKRecv(uint32_t ack_crc) { }
      virtual uint8_t onUnhandledRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                         uint8_t len, uint8_t *reply) { return 0; /* unknown request type */ }
      virtual bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) { return false; /* not handled */ }
      virtual void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                               const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) { }
      virtual void onRawDataRecv(mesh::Packet *pkt) { }
      // A chat line ready for a display: text after compatibility transforms,
      // a decoded or placeholder line for binary channel data, a CLI reply
      // mirrored to a channel. `hops` is the hop count (0xFF = direct).
      virtual void onShowMessage(uint8_t hops, const char* chat_name, const char* text, int msgcount, bool is_pm) { }
      virtual void onMessageSent(const char* to_name, const char* text, bool is_pm, bool is_room) { }
      virtual void onAppConnection(bool connected) { }   // polled from loop()
      virtual void onStatusChanged() { }   // node state shown on screen changed (time source, ...)
      virtual ~Listener() { }
  };

  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store);

  void begin();
  void startInterface(BaseSerialInterface &serial);
  void setListener(Listener* listener) { _listener = listener; }

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  uint32_t getBLEPin();
  void setBLEPin(uint32_t active_pin);

  void loop();
  void handleCmdFrame(size_t len);
  bool advert();
  void enterCLIRescue();
  void exitCLIRescue();
  bool isCLIRescue() const { return _cli_rescue; }

  int  getRecentlyHeard(AdvertPath dest[], int max_num);

#if defined(DISPLAY_CLASS) && !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
#endif
  TimeSource getTimeSource() const { return _time_source; }
  uint32_t getTimeSyncCount() const { return _ts._sync_count; }
  uint32_t getTimeLastSync() const { return _ts._last_sync; }
  int32_t getTimeLastAdjustment() const { return _ts._last_adj; }
  bool hasRecentAppTimeSet() const;
  const char *getTimeSourceLabel() const;

  // true while a packet is being transmitted, or the radio has not yet
  // returned to receive mode after a TX. The superloop defers blocking e-ink
  // service() on this (see main.cpp) so the radio is not kept out of RX.
  bool isRadioTxBusy() const { return Dispatcher::isRadioTxBusy(); }

protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  bool getCADEnabled() const override;
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  uint8_t getAutoAddMaxHops() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text, char* reply) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                    const uint8_t* app_data, size_t app_data_len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override { return addContact(contact); }
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override {
    // A stored record without a name is just a free slot. Loading it would call
    // setChannel(), which pulls num_channels up to channel_idx + 1: /channels2
    // holds up to MAX_GROUP_CHANNELS records, so num_channels ended up pinned at
    // MAX_GROUP_CHANNELS and addChannel("TerminalCLI") returned NULL forever.
    // Nodes that saved their channel list before the CLI existed stayed without
    // the channel. Empty records keep the slot empty without touching the count.
    if (!ch.name[0]) return true;
    return setChannel(channel_idx, ch);
  }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = 0;
  }

public:
  void savePrefs() {
    _prefs.node_lat = sensors.node_lat;
    _prefs.node_lon = sensors.node_lon;
    _store->savePrefs(_prefs);
    _prefs.clearDirty();
  }
  void deferSavePrefs();   // schedule flash write to happen outside of BLE connection

#ifdef WITH_COMPANION_CLI
  const char* getCliPin() const { return _prefs.cli_pin; }
  void regenerateCliPin() { ensureCliPin(true); }

  int getChatMode() const {
    bool remote = _prefs.rcli_mode != REMOTE_CLI_OFF;
    if ( remote &&  _terminal_cli_enabled) return 0;  // C+P
    if (!remote &&  _terminal_cli_enabled) return 1;  // cht
    if ( remote && !_terminal_cli_enabled) return 2;  // PM
    return 3;                                         // OFF
  }
  void setChatMode(int m) {
    bool remote = (m == 0 || m == 2);
    if (!remote) _prefs.rcli_mode = REMOTE_CLI_OFF;
    else if (_prefs.rcli_mode == REMOTE_CLI_OFF) _prefs.rcli_mode = REMOTE_CLI_PIN;
    _terminal_cli_enabled = (m == 0 || m == 1);
    deferSavePrefs();
  }

  int getTimesyncMode() const {
    if ( _ts_from_adverts &&  _ts_from_messages) return 0;  // a+g
    if (!_ts_from_adverts && !_ts_from_messages) return 1;  // gps
    return 2;                                                // adv
  }
  void setTimesyncMode(int m) {
    _ts_from_adverts  = (m == 0 || m == 2);
    _ts_from_messages = (m == 0);
  }

#endif

  bool isCyr2LatChannelsEnabled() const { return _cyr2lat_channels_enabled; }
  bool isCyr2LatContactsEnabled() const { return _cyr2lat_contacts_enabled; }
  void setCyr2LatChannelsEnabled(bool enabled);
  void setCyr2LatContactsEnabled(bool enabled);

  bool isMCOtxtEnabled() const {
#ifdef WITH_MCOTXT
    return _prefs.ui_mcotxt_disabled == 0;
#else
    return false;
#endif
  }
  void setMCOtxtEnabled(bool enabled);
  bool isMCMPDetectEnabled() const {
#ifdef WITH_MCMP_DETECT
    return _prefs.ui_mcmp_detect_off == 0;
#else
    return false;
#endif
  }
  void setMCMPDetectEnabled(bool enabled);
  bool isAEICDetectEnabled() const {
#ifdef WITH_AEIC_DETECT
    return _prefs.ui_aeic_detect_off == 0;
#else
    return false;
#endif
  }
  void setAEICDetectEnabled(bool enabled);
  bool isMCOimgDetectEnabled() const {
#ifdef WITH_MCOIMG_DETECT
    return _prefs.ui_mcoimg_detect_off == 0;
#else
    return false;
#endif
  }
  void setMCOimgDetectEnabled(bool enabled);

#ifdef WITH_COMPANION_FRAME_FRAGMENTS
  bool isFramedPacketsEnabled() const { return _prefs.framed_packets_enabled != 0; }
  void setFramedPacketsEnabled(bool enabled);
#endif

#ifdef WITH_WIFI_SWITCHING
  // The transports themselves live in main.cpp, registered in interface_manager;
  // the mode (BLE, BLE+WiFi, USB) says which of them are enabled, and main.cpp
  // applies it through this hook.
  void setCommsHook(void (*hook)(uint8_t mode)) { _comms_hook = hook; }
  void switchCommsMode(uint8_t mode, int wifi_net_idx = 0);
  // A join in progress: WiFi chosen and not up yet, for the first 15 s. Later
  // attempts are upstream's background reconnect, the menu does not wait for them.
  bool isWifiConnecting() const {
    return _wifi_prefs.comms_mode == COMMS_MODE_WIFI && WiFi.status() != WL_CONNECTED
        && millis() - _wifi_attempt_start < 15000;
  }
  // WiFi and USB carry the app link themselves and drop when the chip light-sleeps;
  // BLE survives it.
  bool commsForbidsSleep() const { return _wifi_prefs.comms_mode != COMMS_MODE_BLE; }
  bool isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  String getWifiIP() const { return WiFi.localIP().toString(); }
  WifiPrefs* getWifiPrefs() { return &_wifi_prefs; }
  void addWifiNetwork(const char* ssid, const char* pass);
  void removeWifiNetwork(const char* ssid);
  void saveWifiPrefs();
  void loadWifiPrefs();
  void initCommsFromPrefs();
#endif

  // Whether an app is connected over any transport.
  bool isAppConnected() const { return _serial != nullptr && _serial->isConnected(); }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled ? "1" : "0");
    if (_prefs.gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _prefs.gps_interval);
      sensors.setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

private:
  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  void writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  // Returns the entry's position, or -1 when it was dropped. `seq` is given only
  // when the frame comes back from the offline store.
  int addToOfflineQueue(const uint8_t frame[], int len, uint8_t compat_flags = 0,
                        uint32_t seq = offline_ring::NO_SEQ);
  void removeOfflineQueueHead();
  void resetAppSyncState();
  // Canonical queue frames are stored in the v3 layout (SNR + two reserved
  // bytes). Collapse a v3 text-message frame to the legacy layout for apps
  // older than v3. Returns the new length, or 0 when not a v3 text frame.
  int convertV3FrameToLegacy(const uint8_t* src, int len, uint8_t* dst) const;
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  bool handleCommand(const char* text, uint32_t sender_timestamp, char* reply);
  void checkSerialInterface();
  // True while the connected app still has frames to receive: queued in the
  // transport, or waiting in the offline queue for its next SYNC_NEXT_MESSAGE.
  bool hasUndeliveredAppFrames() const;
  // Postpone a due reboot/power-off while those frames drain, re-arming
  // action_at. Bounded by APP_DRAIN_GRACE_MILLIS so an app that never syncs
  // cannot defer the action indefinitely.
  bool deferForAppDrain(unsigned long& action_at);
  bool isValidClientRepeatFreq(uint32_t f) const;

  // helpers, short-cuts
  void saveChannels() { _store->saveChannels(this); }
  void saveContacts();

  DataStore* _store;
  NodePrefs _prefs;
  uint32_t pending_login;
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  Listener* _listener;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;
  bool _cli_rescue;
  bool send_unscoped;   // force un-scoped flood (instead of using send_scope)
  char cli_command[80];
  char reply_buf[166];
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;
  unsigned long dirty_prefs_expiry;

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;
  TimeSource _time_source = TIME_SOURCE_UNSET;
  uint32_t _app_time_lock_until = 0;
  void noteTimeSource(TimeSource source);

#ifdef WITH_COMPANION_FRAME_FRAGMENTS
  // A queued frame keeps up to MAX_FRAME_SIZE bytes in place; a longer one (only
  // with fragments on) stores a slot index into the fixed long-frame RAM pool.
  // compat_flags stays inside the Frame, so it shifts with the entry.
  struct Frame {
    uint16_t len;
    uint8_t compat_flags;
    union { uint8_t inline_buf[MAX_FRAME_SIZE]; uint8_t long_slot; } storage;
  };
  static_assert(OFFLINE_LONG_FRAME_SLOTS > 0 && OFFLINE_LONG_FRAME_SLOTS <= 8,
                "long-frame slot mask is uint8_t");
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];
  uint8_t long_frame_pool[OFFLINE_LONG_FRAME_SLOTS][companion_frames::MAX_ORIGINAL_LEN];
  uint8_t long_frame_slot_mask;
  int allocLongFrameSlot();
  void freeLongFrameSlot(uint8_t slot);
  uint8_t* frameData(Frame& frame);
  const uint8_t* frameData(const Frame& frame) const;
  void releaseFrameStorage(Frame& frame);
  int buildLegacyQueuedFrame(const uint8_t* src, uint16_t len, uint8_t* dest) const;
#else
  struct Frame {
    uint8_t len;
    uint8_t compat_flags;
    uint8_t buf[MAX_FRAME_SIZE];
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];
  const uint8_t* frameData(const Frame& frame) const { return frame.buf; }
  uint8_t* frameData(Frame& frame) { return frame.buf; }
#endif
  // Per-entry bookkeeping, kept outside the Frame so it travels with the entry
  // as the queue shifts: 1 while the entry has not reached the filesystem, its
  // sequence number in the offline store, and where its route lives.
  uint8_t offline_queue_store_pending[OFFLINE_QUEUE_SIZE];
  uint32_t offline_queue_seq[OFFLINE_QUEUE_SIZE];
  uint8_t offline_queue_route[OFFLINE_QUEUE_SIZE];
  #define OFFLINE_ROUTE_NONE   0
  #define OFFLINE_ROUTE_POOL   1   // in route_pool[], not written yet
  #define OFFLINE_ROUTE_STORED 2   // in the entry's record in the offline store
  void moveQueueAux(int dst, int src);
  void clearQueueAux(int pos);
  void onQueueEntryRemoved(int pos);   // before the entry at pos leaves the queue
  void initQueueEntry(int pos, uint8_t compat_flags, uint32_t seq);
  void removeOfflineQueueAt(int pos);
  bool isQueuedChannelMsg(const Frame& frame) const;

  // Set OFFLINE_STORE_DEBUG=1 to trace what the offline store keeps and hands
  // back. Keep it off for builds whose Serial carries the companion protocol.
  #ifndef OFFLINE_STORE_DEBUG
    #define OFFLINE_STORE_DEBUG 0
  #endif
  #if OFFLINE_STORE_DEBUG
    // Only when the CDC has room for the whole line: Serial.write() spins while
    // a host holds the port open without reading it, which would stall the node.
    #define OQ_LOG(...) do { if (Serial.availableForWrite() > 96) Serial.printf("offline: " __VA_ARGS__); } while (0)
  #else
    #define OQ_LOG(...) {}
  #endif

  // The offline queue survives a reboot: one record per queued frame in a ring
  // of files (offline_ring::Ring), identified by sequence number. What the app
  // has consumed is a watermark taken from the queue itself — the lowest seq
  // still in it — so an entry dropped out of order cannot shift what is restored.
  //
  // A record also carries the route of the message, when there is one to keep:
  // the raw packet of the copy this node decoded, as the app would have seen it
  // live in a LogRxData push. It is handed over right before its message.
  //
  // Writes happen only from loop(), never while a BLE link is up (a flash write
  // stalls the SoftDevice), and only once the queue has been quiet for a moment:
  // an entry the app takes right away never touches the flash.
  // With fragments on a queued frame can hold up to FR01's 259-byte original;
  // without them addToOfflineQueue() cannot store more than MAX_FRAME_SIZE.
  #ifdef WITH_COMPANION_FRAME_FRAGMENTS
    #define OFFLINE_STORE_MAX_FRAME companion_frames::MAX_ORIGINAL_LEN
  #else
    #define OFFLINE_STORE_MAX_FRAME MAX_FRAME_SIZE
  #endif
  #define OFFLINE_ROUTE_MAX (MAX_TRANS_UNIT + 2)   // [snr x4][rssi][raw packet]
  // The ring shares the filesystem with the contacts, so its size is a product
  // decision, set per variant.
  #ifndef OFFLINE_STORE_SLOTS
    #define OFFLINE_STORE_SLOTS 24
  #endif
  #define OFFLINE_STORE_TTL_SECONDS (24UL * 60UL * 60UL)
  #define OFFLINE_STORE_FLUSH_BATCH 2       // records written per loop pass
  #define OFFLINE_STORE_QUIET_MS 3000       // the app takes a live message well within this
  #define OFFLINE_STORE_MAX_WAIT_MS 15000   // a steady stream does not hold writes off for ever
  // -D OFFLINE_STORE_OFF=1 keeps the queue in RAM only: a diagnosis switch.
  #ifndef OFFLINE_STORE_OFF
    #define OFFLINE_STORE_OFF 0
  #endif
  class OfflineFsStorage : public offline_ring::Storage {
  public:
    DataStore* store = nullptr;
    bool writeSlot(uint16_t slot, const uint8_t* data, size_t len) override;
    int readSlot(uint16_t slot, uint8_t* buf, size_t cap) override;
    void removeSlot(uint16_t slot) override;
    bool writeIndex(const uint8_t* data, size_t len) override;
    int readIndex(uint8_t* buf, size_t cap) override;
  };
  OfflineFsStorage offline_fs;
  uint32_t offline_slot_seq[OFFLINE_STORE_SLOTS];
  uint8_t offline_scratch[offline_ring::RECORD_HEADER + OFFLINE_STORE_MAX_FRAME + OFFLINE_ROUTE_MAX];
  offline_ring::Ring offline_store;
  unsigned long offline_last_enqueue;   // millis() of the newest queued entry
  unsigned long offline_pending_since;  // millis() of the oldest unwritten entry, 0 = none
  // What the last restore did, kept for the heartbeat: the boot line that reports
  // it is easily lost across a reset (the USB CDC re-enumerates).
  uint16_t offline_last_restored;
  uint16_t offline_last_lost;
  unsigned long offline_beat;
  void beginOfflineStore();
  void serviceOfflineStore();   // loop(), only while no BLE link is up

  // Routes of queued messages: the raw copy each one was decoded from, handed to
  // the app right before the message. Kept in RAM until their record reaches the
  // store, a handful at a time: loop() drains them quickly.
  //   WITH_OFFLINE_ROUTES  channel messages, for every app: the official app,
  //                        meshcore-open and MCO Advanced all take a channel
  //                        route from the raw copy. Switch off with
  //                        -D WITH_OFFLINE_ROUTES=0.
  #ifndef WITH_OFFLINE_ROUTES
    #define WITH_OFFLINE_ROUTES 1
  #endif
#if WITH_OFFLINE_ROUTES
  #ifndef OFFLINE_ROUTE_POOL_SLOTS
    #define OFFLINE_ROUTE_POOL_SLOTS 6
  #endif
  // The app session that saw an entry's raw copy live; OFFLINE_LIVE_NONE if none.
  #define OFFLINE_LIVE_NONE 0xFF
  uint8_t offline_queue_live[OFFLINE_QUEUE_SIZE];
  uint8_t app_session;   // counts CMD_APP_START, 0 .. OFFLINE_LIVE_NONE - 1
  struct RoutePoolEntry {
    uint32_t seq;   // offline_ring::NO_SEQ = free
    uint16_t len;
    uint8_t data[OFFLINE_ROUTE_MAX];
  };
  RoutePoolEntry route_pool[OFFLINE_ROUTE_POOL_SLOTS];
  int findPoolRoute(uint32_t seq) const;
  void keepRoute(int pos, const mesh::Packet* pkt);
  void emitQueuedRoute(int pos);
#endif

  bool _app_sync_active = false;
  bool _app_sync_original_sent = false;
  bool _app_sync_route_sent = false;
  uint8_t _app_sync_next_part = 0;
  uint32_t _app_sync_timestamp = 0;
  bool _serial_was_connected = false;

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    // The hash the app computed over the text it handed over, when that
    // text went out transformed (transliteration) and [ack] names the text
    // that went out; 0 when the two are the same. RESP_CODE_SENT and
    // PUSH_CODE_SEND_CONFIRMED carry this one to the app, which knows no
    // other (helpers/TextMessageAck.h).
    uint32_t app_ack;
    ContactInfo* contact;
  };
  #define EXPECTED_ACK_TABLE_SIZE 8
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;

  #define ADVERT_PATH_TABLE_SIZE   16
  AdvertPath advert_paths[ADVERT_PATH_TABLE_SIZE]; // circular table


  struct Cyr2LatChannelMap {
    uint8_t transformed_hash[MAX_HASH_SIZE];
    uint16_t original_payload_len;
    uint8_t original_payload[MAX_PACKET_PAYLOAD];
  };
  #define CYR2LAT_CHANNEL_MAP_SIZE 4
  Cyr2LatChannelMap      _cyr2lat_channel_maps[CYR2LAT_CHANNEL_MAP_SIZE] = {};
  int                    _next_cyr2lat_channel_map = 0;
  bool                   _cyr2lat_channels_enabled = false;
  bool                   _cyr2lat_contacts_enabled = false;
  // Applied only while an original offline frame is being handed to the
  // currently connected app; the queue itself is capability-neutral.
  bool                   _app_supports_mctxt = false;
  bool                   _app_supports_mcmp = false;
  bool                   _app_supports_aeic = false;
  bool                   _app_supports_mcoimg = false;
  // Bytes the current app sent in CMD_APP_START after its 8-byte header (app
  // name and capability list), for offline_route::deliver(); empty between
  // sessions. Truncated if longer than the buffer.
  uint8_t                _app_info[48];
  uint8_t                _app_info_len = 0;
#ifdef WITH_COMPANION_FRAME_FRAGMENTS
  // The current CMD_APP_START declared `frmfrg1`; false until declared.
  bool                   _app_supports_frmfrg1 = false;
  uint16_t               next_fragment_id;
  uint16_t               queued_fragment_id;
  uint8_t                queued_fragment_index;
  bool                   queued_fragment_ack_pending;
#endif
  unsigned long          _pending_reboot_at = 0;
  unsigned long          _pending_reboot_deadline = 0;
  unsigned long          _pending_poweroff_at = 0;
  unsigned long          _pending_poweroff_deadline = 0;
  unsigned long          _app_drain_until = 0;      // 0 = no drain window open

  bool sendGroupMessageWithCyr2LatMap(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name,
                                      const char* text, int text_len, const char* original_text,
                                      int original_len, bool record_map);
  int mapCyr2LatChannelRawLog(const uint8_t* raw, int len, uint8_t* mapped, int mapped_size);

  bool transformCompatText(const char* text, bool has_name, char* output,
                           size_t output_size);
#ifdef WITH_COMPANION_FRAME_FRAGMENTS
  int getNextAppFrame(uint8_t frame[], bool has_ack=false, uint16_t ack_id=0, uint8_t ack_index=0);
  int emitQueuedOriginalForApp(uint8_t frame[], bool retain_source,
                               bool has_ack, uint16_t ack_id, uint8_t ack_index,
                               bool& stage_complete);
  bool canSendFrameFragments() const;
  void resetFragmentTransferState();
  void resetFragmentSession();
  uint16_t allocFragmentId();
  void writeFrameMaybeFragmented(const uint8_t frame[], uint16_t len, uint8_t flags=0);
  void writePacketPayloadFrameMaybeFragmented(mesh::Packet* packet, uint8_t push_code, uint8_t path_byte, const char* name);
#else
  int getNextAppFrame(uint8_t frame[]);
#endif
  int renderCompatFramePart(const Frame& source, size_t part_index,
                            size_t& part_count, bool& additive,
                            uint8_t output[]);
#ifdef WITH_AEIC_DETECT
  struct AEICNotice {
    bool used;
    uint8_t channel_idx;
    uint16_t sender_prefix;
    uint8_t image_id;
    uint8_t total;
    uint32_t seen_at;
  };
  static const int AEIC_NOTICE_TABLE_SIZE = 8;
  static const uint32_t AEIC_NOTICE_TTL_MILLIS = 60000;
  AEICNotice _aeic_notices[AEIC_NOTICE_TABLE_SIZE] = {};
  int _next_aeic_notice = 0;
  bool markAEICNotice(uint8_t channel_idx, uint16_t sender_prefix,
                      uint8_t image_id, uint8_t total, uint32_t now_millis);
  void aeicSenderName(uint16_t sender_prefix, char* out, size_t out_size);
#endif

#ifdef WITH_COMPANION_CLI
  CommonCLI*             _cli = nullptr;
  CompanionCLICallbacks* _cli_cb = nullptr;
  // Remote CLI: per-sender replay and wrong-PIN state (RAM only).
  struct RemoteCliPeer {
    uint8_t  key[4];        // sender public key prefix; all zero = free slot
    uint32_t last_ts;       // newest accepted command timestamp (sender clock)
    uint32_t lock_until;    // millis(); 0 = not locked
    uint32_t seen;          // millis() of the last use, for eviction
    uint32_t session_until; // millis(); PIN session of the CLI screen, 0 = none
    uint8_t  bad;           // wrong PINs in a row
  };
  RemoteCliPeer          _rcli_peers[8] = {};
  void ensureCliPin(bool regenerate = false);
  bool setCliPin(const char* pin);
  RemoteCliPeer* rcliPeer(const ContactInfo& from);
  int  rcliCheckPin(const ContactInfo& from, const char* text, uint32_t sender_ts, const char** cmd);
  bool rcliFresh(RemoteCliPeer* p, uint32_t sender_ts);
  void rcliAccepted(const ContactInfo& from);
  bool                   _terminal_cli_enabled = true;
  bool                   _ts_from_adverts = true;
  bool                   _ts_from_messages = true;

  void handleRemoteCLI(const ContactInfo& from, uint32_t sender_ts, const char* cmd);
  void handleTerminalCLI(uint8_t ch_idx, uint32_t sender_ts, const char* cmd,
                         bool mirror_ui_reply);
  void sendCliReplyPM(const ContactInfo& to, const char* buf);
  void sendCliReplyChannel(uint8_t ch_idx, const char* buf, bool mirror_ui = false);
  void injectChannelMsg(uint8_t ch_idx, const char* sender_name, uint8_t path_len, int8_t snr_x4, uint32_t ts, const char* text);
  int  findTerminalCLIChannelIdx();
  bool handleCliCmd(uint32_t sender_ts, const char* cmd, char* buf, bool is_remote);
  void runCliChain(uint32_t sender_ts, const char* cmd, char* buf, bool is_remote);
#endif

#ifdef WITH_WIFI_SWITCHING
  WifiPrefs           _wifi_prefs;            // in-memory model; stored in NodePrefs
  uint32_t            _wifi_attempt_start = 0;
  void                (*_comms_hook)(uint8_t mode) = nullptr;
  void                wifiPrefsFromNodePrefs();
  bool                handleUniWifiSet(const char* command, char* reply);
public:
  void                wifiScanDone();   // back to WiFi off after a scan, unless WiFi is the mode
private:
#endif
};

extern MyMesh the_mesh;
