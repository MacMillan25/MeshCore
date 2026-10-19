#pragma once

#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include "DarktecBoard.h"

/*
 * LR2021 на плате DarkTec (тестовая сборка).
 *
 * Всё радио — штатная обёртка MeshCore (side detectors, RSSI/SNR, rx boosted
 * gain). Своё здесь только одно: сообщать плате о приёме и о том, что идёт
 * пакет, иначе модель расхода батареи (ETA и ток на странице батареи) считает,
 * что эфир всегда молчит.
 */
class DarktecLR2021Wrapper : public CustomLR2021Wrapper {
  DarktecBoard& _dt;
public:
  DarktecLR2021Wrapper(CustomLR2021& radio, DarktecBoard& board)
    : CustomLR2021Wrapper(radio, board), _dt(board) {}

  int recvRaw(uint8_t* bytes, int sz) override {
    int len = RadioLibWrapper::recvRaw(bytes, sz);
    if (len > 0) {
      _dt.onLoRaPacketReceived();
    }
    return len;
  }

  void loop() override {
    RadioLibWrapper::loop();
    _dt.setLoRaReceiving(isReceivingPacket());
  }
};
