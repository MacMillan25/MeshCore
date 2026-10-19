#pragma once

#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include "DarktecBoard.h"

/*
 * LR2021 на плате DarkTec (тестовая сборка).
 *
 * Своё здесь две вещи:
 *   1. Сообщать плате о приёме и о том, что идёт пакет, — иначе модель
 *      расхода батареи (ETA и ток на странице батареи) видит тишину в эфире.
 *   2. Не зависеть от линии IRQ. У модуля на проводках она может быть
 *      подключена не к тому DIO, который настроен в драйвере, и тогда прерываний
 *      нет вообще: RSSI читается (шумовой пол на экране живой), а пакеты не
 *      принимаются и передача не завершается. Поэтому статус чипа опрашивается
 *      по SPI — RX_DONE/TX_DONE видны там независимо от ноги IRQ.
 */
class DarktecLR2021Wrapper : public CustomLR2021Wrapper {
  DarktecBoard& _dt;

  static constexpr uint32_t RX_IRQ_BITS =
      RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR |
      RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID | RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR |
      RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR2021_IRQ_TIMEOUT;

  CustomLR2021* lr() { return (CustomLR2021*)_radio; }

public:
  DarktecLR2021Wrapper(CustomLR2021& radio, DarktecBoard& board)
    : CustomLR2021Wrapper(radio, board), _dt(board) {}

  // Опрос статуса вместо прерывания: поднимаем флаг приёма/передачи сами.
  bool pollPacketFlag() override {
    const uint32_t irq = lr()->getIrqFlags();
    return (irq & (RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_TX_DONE |
                   RADIOLIB_LR2021_IRQ_TIMEOUT)) != 0;
  }

  int recvRaw(uint8_t* bytes, int sz) override {
    const uint32_t before = lr()->getIrqFlags();
    int len = RadioLibWrapper::recvRaw(bytes, sz);
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
