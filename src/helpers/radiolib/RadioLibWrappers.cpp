
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
#include "driver/gpio.h"
#endif

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

static volatile uint8_t state = STATE_IDLE;

// this function is called when a complete packet
// is transmitted by the module
static
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _last_raw_rssi = 0;
  _cad_enabled = false;

  _rssi_batch_n = 0;
  _last_batch_n = 0;
  _next_rssi_sample = 0;
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
#if defined(USE_LR2021)
  // On LR2021, setOutputPower() writes the PA config and TxParams, which are
  // standby-only commands. recvRaw() keeps state == STATE_RX after readData on
  // this platform ("LR2021 stays in Rx"), so - unlike the SX126x path, which
  // falls back to STATE_IDLE and re-arms on the next recvRaw() - nothing calls
  // startReceive() again by itself. Writing the PA config while listening could
  // therefore leave the receiver down until the next reboot. Drop to standby
  // first and let checkRecv() re-arm Rx, as resetAGC() and
  // applySideDetectorConfig() already do.
  idle();
#endif
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;

  const uint8_t n = _rssi_batch_n;
  _last_batch_n = n;
  _rssi_batch_n = 0;
  if (n == 0) return;   // приёмник ни разу не был в RX: держим прежнюю оценку

  // Не среднее, а 25-й процентиль: чужая передача и помеха поднимают RSSI, шум
  // же — это тихий край выборки. Среднее тянулось к помехам, а отсечка «только
  // замеры ниже (пол + 14)» вообще не давала полу расти и прибивала его к первому
  // неудачному значению — именно так пол и залипал на -78.
  int16_t tmp[NOISE_BATCH_MAX];
  memcpy(tmp, _rssi_batch, n * sizeof(tmp[0]));
  for (uint8_t i = 1; i < n; i++) {
    const int16_t v = tmp[i];
    int8_t j = (int8_t)(i - 1);
    while (j >= 0 && tmp[j] > v) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = v;
  }
  int16_t f = tmp[n / 4];
  if (f < -120) f = -120;   // нижняя граница шкалы SX126x
  _noise_floor = f;

  // Самолечения здесь нет намеренно. Высокий и неподвижный пол — не признак
  // «ослепшего» приёмника: у модулей с LNA (E22P) снятая антенна оставляет вход
  // открытым, и тракт честно шумит на -76 dBm. Сброс аналоговой части такой пол
  // не меняет, а приём рвёт зря.

  #ifdef MESH_DEBUG_NOISE_FLOOR
  MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  #endif
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()

  // Reset noise floor sampling so it reconverges from scratch.
  _noise_floor = 0;
  _last_raw_rssi = 0;
  _rssi_batch_n = 0;
  _last_batch_n = 0;
  _next_rssi_sample = 0;
}

#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)
void RadioLibWrapper::recoverMissedDioInterrupt() {
  const int pin = irqDioPin();
  if (pin < 0) return;
  const int level = gpio_get_level((gpio_num_t)pin);
  // `state == STATE_RX` means the radio is receiving and the ISR flag is clear;
  // a HIGH DIO1 then means the packet-received edge fired while the CPU was
  // asleep and was never serviced. `STATE_TX_WAIT` is the same situation for
  // TxDone: the send-finished edge fired asleep, and the ISR flag must be raised
  // so isSendComplete() finishes the TX. recvRaw() and isSendComplete() only look
  // at STATE_INT_READY, so the extra flag is consumed exactly like a real ISR.
  if (level == HIGH && (state == STATE_RX || state == STATE_TX_WAIT)) {
    state |= STATE_INT_READY;
  }
}
#endif

void RadioLibWrapper::loop() {
  // Фолбэк для плат без рабочей линии IRQ: если прерывание не пришло, но в
  // статусе чипа уже есть RX_DONE/TX_DONE, поднимаем тот же флаг, что и ISR.
  if ((state & STATE_INT_READY) == 0 && pollPacketFlag()) {
    state |= STATE_INT_READY;
  }

  // Замер берётся только когда приёмник действительно слушает и пакета нет: во
  // время приёма и передачи RSSI отражает сигнал, а не шум.
  if (state != STATE_RX || isReceivingPacket()) return;
  // 50 Гц — выше шумовой пол всё равно не нужен, а SPI лишний раз не дёргаем.
  const uint32_t now = millis();
  if ((int32_t)(now - _next_rssi_sample) < 0) return;
  _next_rssi_sample = now + 20;

  const int16_t rssi = (int16_t)getCurrentRSSI();
  _last_raw_rssi = rssi;
  if (_rssi_batch_n < NOISE_BATCH_MAX) _rssi_batch[_rssi_batch_n++] = rssi;
}

void RadioLibWrapper::startRecv() {
  #if defined(USE_LR2021)
  _radio->standby(); // without this LR2021 can throw -706 when calling startReceive after hardware CAD when side detectors are enabled
  #endif
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
        n_recv_errors++;
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
      }
    }
    #if defined(USE_LR2021)
    state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    #else
    state = STATE_IDLE;   // need another startReceive()
    #endif
  }

  if (state != STATE_RX) {
    int err = _radio->startReceive();
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
    }
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;

  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;

  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us; // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}
