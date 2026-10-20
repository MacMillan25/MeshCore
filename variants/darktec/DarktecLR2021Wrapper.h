#pragma once

#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include "DarktecBoard.h"

#ifndef DARKTEC_LR2021_POLL_IRQ
#define DARKTEC_LR2021_POLL_IRQ 0
#endif

#ifndef DARKTEC_LR2021_RAW_LOG
#define DARKTEC_LR2021_RAW_LOG 0
#endif

/*
 * LR2021 на плате DarkTec (тестовая сборка).
 *
 * Своё здесь три вещи:
 *   1. Сообщать плате о приёме и о том, что идёт пакет, — иначе модель
 *      расхода батареи (ETA и ток на странице батареи) видит тишину в эфире.
 *   2. Не зависеть от линии IRQ. У модуля на проводках она может быть
 *      подключена не к тому DIO, который настроен в драйвере, и тогда прерываний
 *      нет вообще: RSSI читается (шумовой пол на экране живой), а пакеты не
 *      принимаются и передача не завершается. Поэтому статус чипа опрашивается
 *      по SPI — RX_DONE/TX_DONE видны там независимо от ноги IRQ.
 *   3. Запоминать параметры, которые выдало ядро (частота, полоса, SF, CR):
 *      по ним видно, совпадают ли они с сетью.
 */
class DarktecLR2021Wrapper : public CustomLR2021Wrapper {
  DarktecBoard& _dt;

  float   _freq = 0.0f;
  float   _bw = 0.0f;
  uint8_t _sf = 0;
  uint8_t _cr = 0;

  // Счётчики для диагностики: сколько раз опрос увидел RX_DONE, сколько раз
  // пришлось читать, какая была длина в регистре и статус в этот момент.
  uint32_t _st_poll = 0;
  uint32_t _st_read = 0;
  uint32_t _st_len = 0;
  uint32_t _st_irq = 0;
  uint32_t _st_poll_irq = 0;
  uint32_t _rx_active_at = 0;

  static constexpr uint32_t RX_IRQ_BITS =
      RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR |
      RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID | RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR |
      RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR2021_IRQ_TIMEOUT;

  CustomLR2021* lr() { return (CustomLR2021*)_radio; }

public:
  DarktecLR2021Wrapper(CustomLR2021& radio, DarktecBoard& board)
    : CustomLR2021Wrapper(radio, board), _dt(board) {}

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    _freq = freq;
    _bw = bw;
    _sf = sf;
    _cr = cr;
    CustomLR2021Wrapper::setParams(freq, bw, sf, cr);
  }

  float getParamFreq() const { return _freq; }
  float getParamBw() const { return _bw; }
  uint8_t getParamSf() const { return _sf; }
  uint8_t getParamCr() const { return _cr; }

  uint32_t getStatPoll() const { return _st_poll; }
  uint32_t getStatRead() const { return _st_read; }
  uint32_t getStatLen() const { return _st_len; }
  uint32_t getStatIrq() const { return _st_irq; }
  uint32_t getStatPollIrq() const { return _st_poll_irq; }

  // Прерывание CAD выключено насовсем: scanChannel() в драйвере сбрасывает
  // IRQ-флаги и перезапускает приём, из-за чего уже принятый пакет теряется
  // (именно так у нас пропадал бит HEADER_VALID перед чтением).
  void setCADEnabled(bool enable) override {
    (void)enable;
    RadioLibWrapper::setCADEnabled(false);
  }

  // Свой вариант «идёт приём»: не трогаем флаги чипа. Штатный
  // CustomLR2021::isReceiving() при бите ошибки CRC заголовка гасит
  // HEADER_VALID/PREAMBLE — а пакет к этому моменту уже лежит в FIFO,
  // и readData() бракует его как «заголовок битый».
  bool isReceivingPacket() override {
    const uint32_t irq = lr()->getIrqFlags();
    if (irq & RADIOLIB_LR2021_IRQ_RX_DONE) return true;

    const bool hdr = (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID) != 0;
    const bool pre = (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED) != 0;
    if (!hdr && !pre) {
      _rx_active_at = 0;
      return false;
    }

    const uint32_t now = millis();
    if (_rx_active_at == 0) _rx_active_at = now;
    if (now - _rx_active_at > 5000) {   // залипший флаг — чистим сами
      lr()->clearIrqFlags(RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED |
                          RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID |
                          RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR);
      _rx_active_at = 0;
      return false;
    }
    return true;
  }

  // Опрос статуса вместо прерывания: поднимаем флаг приёма/передачи сами.
  // По умолчанию ВЫКЛЮЧЕН: на этой плате прерывание от модуля доходит до MCU
  // (на прошивке до опроса поймали пакет), а второй потребитель флагов только
  // мешает — ISR и опрос дерутся за одни и те же биты, и чтение идёт по мусору.
  // Включается флагом сборки -D DARKTEC_LR2021_POLL_IRQ=1.
  bool pollPacketFlag() override {
#if DARKTEC_LR2021_POLL_IRQ
    const uint32_t irq = lr()->getIrqFlags();
    if (irq & (RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_TX_DONE)) {
      _st_poll++;
      if (irq & RADIOLIB_LR2021_IRQ_RX_DONE) _st_poll_irq = irq;
      return true;
    }
#endif
    return false;
  }

  int recvRaw(uint8_t* bytes, int sz) override {
    const uint32_t before = lr()->getIrqFlags();
    if (before & RADIOLIB_LR2021_IRQ_RX_DONE) {
      _st_read++;
      _st_irq = before;
      _st_len = (uint32_t)lr()->getPacketLength(false);
    }
    int len = RadioLibWrapper::recvRaw(bytes, sz);
#if DARKTEC_LR2021_RAW_LOG
    if (len > 0) {
      Serial.print("[RX] len=");
      Serial.print(len);
      Serial.print(" irq=0x");
      Serial.print(before, HEX);
      Serial.print(":");
      for (int i = 0; i < len && i < 24; i++) {
        Serial.print(' ');
        if (bytes[i] < 0x10) Serial.print('0');
        Serial.print(bytes[i], HEX);
      }
      Serial.println();
    } else if (before & RADIOLIB_LR2021_IRQ_RX_DONE) {
      Serial.print("[RX] fail irq=0x");
      Serial.print(before, HEX);
      Serial.print(" reglen=");
      Serial.println(_st_len);
    }
#endif
    if (len > 0) {
      _dt.onLoRaPacketReceived();
    }
    // Флаги приёма в чипе не сбрасываются сами: без этого опрос поднимал бы
    // INT_READY по одному и тому же пакету. TX_DONE здесь не трогаем — его
    // снимает finishTransmit().
    if (before & RX_IRQ_BITS) {
      lr()->clearIrqFlags(RX_IRQ_BITS);
    }
    return len;
  }

  void loop() override {
    RadioLibWrapper::loop();
    _dt.setLoRaReceiving(isReceivingPacket());
  }
};
