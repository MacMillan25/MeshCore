#pragma once

#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "DarktecBoard.h"

/*
 * Управление RF-трактом Darktec.
 *
 * На плате к MCU подведена одна нога модуля — P0.17 (пин 6). Вторая нога
 * (пин 7) висит на DIO2 модуля, то есть ей управляет сам чип SX1262:
 * высокий при передаче, низкий в остальное время (SX126X_DIO2_AS_RF_SWITCH).
 * Это проверено на железе и работает правильно — не трогаем.
 *
 * Пин 6 у модулей означает разное, и что с ним делать, решает настройка
 * «Radio Mod» (или CLI `set radio.module`): у E22/30S/RA62 это RXEN и им
 * управляет RadioLib, у E22P это EN и он держится высоким вручную. См.
 * darktec_radio_set_rf() в target.cpp. От самой обёртки модуль не зависит —
 * она только считает активность радио для модели расхода батареи.
 */
class DarktecSX1262Wrapper : public CustomSX1262Wrapper {
  DarktecBoard& _dt;
public:
  DarktecSX1262Wrapper(CustomSX1262& radio, DarktecBoard& board)
    : CustomSX1262Wrapper(radio, board), _dt(board) {}

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
