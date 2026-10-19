#pragma once

#include <helpers/ui/SSD1306Display.h>

// OLED Darktec.
//
// Раньше здесь жила «точка-пульс вместо LED»: драйвер рисовал 3×3 пикселя в
// левой части экрана между кадрами UI, когда шёл эфир. Теперь активность
// показывает сам интерфейс — надпись RX/TX в строке статуса, — а точка убрана:
// она загоралась раньше интерфейса и рисовалась поверх чужого кадра.
class DarktecDisplay : public SSD1306Display {
  bool _was_charging = false;

public:
  bool isOn() override;
};
