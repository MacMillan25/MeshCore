#include <gtest/gtest.h>

#include <DarktecPrefsFormat.h>

// Тесты чистого формата /darktec.prefs.
// В native-сборке макросов BATTERY_* нет — проверяем константные дефолты.

using namespace DarktecPrefsFormat;

TEST(DarktecPrefsFormatTest, BuildFlagDefaultsSanitized) {
  const Settings d = buildFlagDefaults();
  EXPECT_EQ(d.capacity_mah, 3000);
  EXPECT_EQ(d.module, MOD_E22_900M33S);
  EXPECT_EQ(d.auto_off_idx, 0);
  EXPECT_EQ(d.noise_idx, 2);
  EXPECT_EQ(d.capacity_mode, CAP_USER);
  EXPECT_EQ(d.brightness, BRI_BRIGHT);
}

TEST(DarktecPrefsFormatTest, BrightnessContrastValues) {
  EXPECT_EQ(brightnessContrast(BRI_DIM), 0);
  EXPECT_EQ(brightnessContrast(BRI_NORMAL), 128);
  EXPECT_EQ(brightnessContrast(BRI_BRIGHT), 255);
  EXPECT_EQ(brightnessContrast(99), 255);   // мусор — максимум
}

TEST(DarktecPrefsFormatTest, SerializeParseRoundTrip) {
  Settings in{};
  in.capacity_mah = 4500;
  in.module = MOD_E22P_868M30S;
  in.auto_off_idx = 4;
  in.noise_idx = 0;
  in.capacity_mode = CAP_4000;
  in.brightness = BRI_DIM;

  uint8_t buf[32];
  const size_t n = serialize(in, buf, sizeof(buf));
  EXPECT_EQ(n, kWireSize);
  EXPECT_EQ(buf[0], 'D');
  EXPECT_EQ(buf[1], 'R');
  EXPECT_EQ(buf[2], 'T');
  EXPECT_EQ(buf[3], 'C');
  EXPECT_EQ(buf[4], kWireVersion);

  Settings out{};
  ASSERT_TRUE(parse(buf, n, out));
  EXPECT_EQ(out.capacity_mah, 4500);
  EXPECT_EQ(out.module, MOD_E22P_868M30S);
  EXPECT_EQ(out.auto_off_idx, 4);
  EXPECT_EQ(out.noise_idx, 0);
  EXPECT_EQ(out.capacity_mode, CAP_4000);
  EXPECT_EQ(out.brightness, BRI_DIM);
}

TEST(DarktecPrefsFormatTest, RejectsCorruption) {
  Settings in{};
  in.capacity_mah = 3500;
  uint8_t buf[32];
  const size_t n = serialize(in, buf, sizeof(buf));
  ASSERT_EQ(n, kWireSize);

  Settings out{};
  buf[7] ^= 0x01;  // module
  EXPECT_FALSE(parse(buf, n, out));
  buf[7] ^= 0x01;

  buf[0] = 'X';
  EXPECT_FALSE(parse(buf, n, out));
  buf[0] = 'D';

  buf[4] = kWireVersion + 1;  // неизвестная версия
  EXPECT_FALSE(parse(buf, n, out));
  buf[4] = kWireVersion;

  EXPECT_TRUE(parse(buf, n, out));
  EXPECT_EQ(out.capacity_mah, 3500);
}

TEST(DarktecPrefsFormatTest, SanitizeClampsRanges) {
  Settings s{};
  s.capacity_mah = 99999;   // вне 1000..10000 → дефолт
  s.module = 77;            // неизвестный модуль → E22-33S
  s.auto_off_idx = 9;       // вне 0..4 → 0
  s.noise_idx = 9;          // вне 0..3 → 500 мс
  s.capacity_mode = 9;      // вне 0..2 → UserPref
  s.brightness = 9;         // вне 0..2 → Bright
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 3000);
  EXPECT_EQ(s.module, MOD_E22_900M33S);
  EXPECT_EQ(s.auto_off_idx, 0);
  EXPECT_EQ(s.noise_idx, 2);
  EXPECT_EQ(s.capacity_mode, CAP_USER);
  EXPECT_EQ(s.brightness, BRI_BRIGHT);
}

TEST(DarktecPrefsFormatTest, CapacityBoundaryValuesKept) {
  Settings s{};
  s.capacity_mah = 1000;
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 1000);
  s.capacity_mah = 10000;
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 10000);
}

TEST(DarktecPrefsFormatTest, EffectiveCapacityFollowsMode) {
  Settings s{};
  s.capacity_mah = 4500;

  s.capacity_mode = CAP_USER;
  EXPECT_EQ(effectiveCapacityMah(s), 4500);

  s.capacity_mode = CAP_3000;
  EXPECT_EQ(effectiveCapacityMah(s), 3000);

  s.capacity_mode = CAP_4000;
  EXPECT_EQ(effectiveCapacityMah(s), 4000);
}

// Старый файл beeline (версия 1) читается и переводит порядок модулей
// 0 RA62, 1 30S, 2 E22P, 3 33S в наш 0 33S, 1 30S, 2 RA62, 3 E22P.
TEST(DarktecPrefsFormatTest, ReadsVersion1AndRemapsModules) {
  auto makeV1 = [](uint8_t old_module, uint8_t* buf) {
    buf[0] = 'D'; buf[1] = 'R'; buf[2] = 'T'; buf[3] = 'C';
    buf[4] = 1;
    buf[5] = 0x40; buf[6] = 0x1F;  // 8000 mAh → sanitize к дефолту
    buf[7] = 0;                    // CHEM_LIION
    buf[8] = 1;
    buf[9] = old_module;
    buf[10] = 0xD0; buf[11] = 0x07;  // 2000 mA
    for (int i = 0; i < 6; i++) buf[12 + i] = 0;
    const uint16_t crc = crc16(buf + 4, 1 + kPayloadSize);
    buf[18] = (uint8_t)(crc & 0xFF);
    buf[19] = (uint8_t)(crc >> 8);
  };

  uint8_t buf[32];
  Settings out{};

  makeV1(3, buf);  // v1: 3 = E22-900M33S
  ASSERT_TRUE(parse(buf, kWireSize, out));
  EXPECT_EQ(out.module, MOD_E22_900M33S);

  makeV1(2, buf);  // v1: 2 = E22P
  ASSERT_TRUE(parse(buf, kWireSize, out));
  EXPECT_EQ(out.module, MOD_E22P_868M30S);

  makeV1(0, buf);  // v1: 0 = HT-RA62
  ASSERT_TRUE(parse(buf, kWireSize, out));
  EXPECT_EQ(out.module, MOD_HT_RA62);
}

// Версия 2 (наш первый перенос) не знала режима ёмкости: резерв был нулевой,
// поэтому после чтения режим — UserPref, а ёмкость — как записана.
TEST(DarktecPrefsFormatTest, ReadsVersion2AsUserCapacity) {
  Settings in{};
  in.capacity_mah = 4000;
  uint8_t buf[32];
  ASSERT_EQ(serialize(in, buf, sizeof(buf)), kWireSize);

  buf[4] = 2;   // подменяем версию на старую
  buf[10] = 0;  // в v2 это был резерв
  const uint16_t crc = crc16(buf + 4, 1 + kPayloadSize);
  buf[18] = (uint8_t)(crc & 0xFF);
  buf[19] = (uint8_t)(crc >> 8);

  Settings out{};
  ASSERT_TRUE(parse(buf, kWireSize, out));
  EXPECT_EQ(out.capacity_mah, 4000);
  EXPECT_EQ(out.capacity_mode, CAP_USER);
}

// Версия 3 знала режим ёмкости, но не яркость: после чтения яркость максимальная.
TEST(DarktecPrefsFormatTest, ReadsVersion3WithDefaultBrightness) {
  Settings in{};
  in.brightness = BRI_DIM;
  uint8_t buf[32];
  ASSERT_EQ(serialize(in, buf, sizeof(buf)), kWireSize);
  buf[4] = 3;    // подменяем версию на старую
  buf[11] = 0;   // в v3 это был резерв
  const uint16_t crc = crc16(buf + 4, 1 + kPayloadSize);
  buf[18] = (uint8_t)(crc & 0xFF);
  buf[19] = (uint8_t)(crc >> 8);

  Settings out{};
  ASSERT_TRUE(parse(buf, kWireSize, out));
  EXPECT_EQ(out.capacity_mode, CAP_USER);
  EXPECT_EQ(out.brightness, BRI_BRIGHT);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
