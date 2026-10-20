#pragma once

#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include "DarktecBoard.h"

#ifndef DARKTEC_LR2021_POLL_IRQ
#define DARKTEC_LR2021_POLL_IRQ 1
#endif

#ifndef DARKTEC_LR2021_RAW_LOG
#define DARKTEC_LR2021_RAW_LOG 0
#endif

/*
 * LR2021 на модуле NiceRF LoRa2021F33-2G4.
 *
 * Почему чтение пакета сделано здесь, а не штатным путём RadioLib:
 * в обычной работе MeshCore между приёмом пакета и его чтением успевает
 * вызвать CAD (scanChannel) и CustomLR2021::isReceiving(), а оба чистят
 * IRQ-флаги. Пакет лежит в FIFO целый, но readData() видит «заголовок
 * битый» (нет бита HEADER_VALID) и выбрасывает его. Проверено прямым
 * приёмом (lr2021 listen): сразу после RX_DONE читается 3 из 3.
 *
 * Поэтому пакет забирается из FIFO в тот же момент, когда опрос увидел
 * RX_DONE, и дальше ядро получает уже готовые байты.
 */
class DarktecLR2021Wrapper : public CustomLR2021Wrapper {
  DarktecBoard& _dt;

  float   _freq = 0.0f;
  float   _bw = 0.0f;
  uint8_t _sf = 0;
  uint8_t _cr = 0;

  uint8_t _pending[255];
  int     _pending_len = 0;

  // Счётчики для диагностики.
  uint32_t _st_poll = 0;
  uint32_t _st_read = 0;
  uint32_t _st_len = 0;
  uint32_t _st_irq = 0;
  uint32_t _st_poll_irq = 0;
  uint32_t _st_bad = 0;
  uint32_t _rx_active_at = 0;

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
  uint32_t getStatBad() const { return _st_bad; }

  // CAD выключен: scanChannel() стирает IRQ и перезапускает приём.
  void setCADEnabled(bool enable) override {
    (void)enable;
    RadioLibWrapper::setCADEnabled(false);
  }

  // Своя проверка «идёт приём»: флаги чипа не трогаем.
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

  // Опрос статуса: пакет забираем из FIFO сразу, пока флаги целы.
  bool pollPacketFlag() override {
#if DARKTEC_LR2021_POLL_IRQ
    const uint32_t irq = lr()->getIrqFlags();

    if (irq & RADIOLIB_LR2021_IRQ_RX_DONE) {
      _st_poll++;
      _st_poll_irq = irq;
      _st_irq = irq;
      _st_read++;

      size_t plen = lr()->getPacketLength();
      if (plen > sizeof(_pending)) plen = sizeof(_pending);
      _st_len = (uint32_t)plen;

      if (plen > 0) {
        const int16_t err = lr()->readData(_pending, plen);
        if (err == RADIOLIB_ERR_NONE) {
          _pending_len = (int)plen;
#if DARKTEC_LR2021_RAW_LOG
          Serial.print("[RX] len=");
          Serial.print((int)plen);
          Serial.print(" irq=0x");
          Serial.print(irq, HEX);
          Serial.print(":");
          for (size_t i = 0; i < plen && i < 24; i++) {
            Serial.print(' ');
            if (_pending[i] < 0x10) Serial.print('0');
            Serial.print(_pending[i], HEX);
          }
          Serial.println();
#endif
        } else {
          _st_bad++;
          _pending_len = 0;
#if DARKTEC_LR2021_RAW_LOG
          Serial.print("[RX] read err=");
          Serial.print(err);
          Serial.print(" len=");
          Serial.print((int)plen);
          Serial.print(" irq=0x");
          Serial.println(irq, HEX);
#endif
        }
      } else {
        _pending_len = 0;
      }

      // Флаги этого пакета снимаем; данные уже у нас.
      lr()->clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL);
      return true;
    }

    if (irq & RADIOLIB_LR2021_IRQ_TX_DONE) {
      _st_poll++;
      return true;
    }
#endif
    return false;
  }

  int recvRaw(uint8_t* bytes, int sz) override {
    if (_pending_len > 0) {
      const int out = (_pending_len > sz) ? sz : _pending_len;
      memcpy(bytes, _pending, out);
      _pending_len = 0;
      _dt.onLoRaPacketReceived();
      startRecv();   // снять флаг прерывания в ядре и снова в приём
      return out;
    }
    // Пакета нет — штатный путь: он же возвращает радио в приём после
    // передачи, сброса AGC и смены настроек.
    return RadioLibWrapper::recvRaw(bytes, sz);
  }

  void loop() override {
    RadioLibWrapper::loop();
    _dt.setLoRaReceiving(isReceivingPacket());
  }
};
