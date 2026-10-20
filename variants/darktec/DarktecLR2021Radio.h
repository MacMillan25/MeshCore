#pragma once

#include <helpers/radiolib/CustomLR2021.h>

/*
 * LR2021 для модуля NiceRF LoRa2021F33-2G4.
 *
 * Отличие от штатного CustomLR2021 одно: доступ к настройке DIO. У этого
 * модуля весь RF-тракт (PA, LNA, 5-вольтовое питание усилителя) управляется
 * ногами DIO5..DIO8 самого чипа, а в RadioLib функции DIO объявлены protected.
 * Поэтому класс-наследник, который просто выставляет их наружу — чтобы можно
 * было и таблицу RF-ключа выдать, и вручную подёргать ногу при отладке.
 */
class DarktecLR2021 : public CustomLR2021 {
public:
  DarktecLR2021(Module* mod) : CustomLR2021(mod) {}

  // Функция DIO по даташиту чипа: NONE, IRQ, RF_SWITCH, GPIO_OUTPUT_LOW/HIGH...
  int16_t dioFunction(uint8_t dio, uint8_t func, uint8_t pull) {
    return setDioFunction(dio, func, pull);
  }

  int16_t dioLevel(uint8_t dio, bool high) {
    const uint8_t pull = (dio == 5) ? RADIOLIB_LR2021_DIO_SLEEP_PULL_UP
                                    : RADIOLIB_LR2021_DIO_SLEEP_PULL_AUTO;
    return setDioFunction(dio, high ? RADIOLIB_LR2021_DIO_FUNCTION_GPIO_OUTPUT_HIGH
                                    : RADIOLIB_LR2021_DIO_FUNCTION_GPIO_OUTPUT_LOW,
                          pull);
  }

  int16_t dioNone(uint8_t dio) {
    return setDioFunction(dio, RADIOLIB_LR2021_DIO_FUNCTION_NONE,
                          RADIOLIB_LR2021_DIO_SLEEP_PULL_AUTO);
  }
};
