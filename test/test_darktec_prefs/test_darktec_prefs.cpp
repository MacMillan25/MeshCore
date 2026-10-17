#include <gtest/gtest.h>

#include <DarktecPrefsFormat.h>

// Тесты чистого формата /darktec.prefs (spec 0001 FR-14..19, US-6).
// В native-сборке макров BATTERY_* нет — проверяем константные дефолты.

using namespace DarktecPrefsFormat;

TEST(DarktecPrefsFormatTest, BuildFlagDefaultsSanitized) {
  const Settings d = buildFlagDefaults();
  EXPECT_EQ(d.capacity_mah, 4000);
  EXPECT_EQ(d.chem, CHEM_LIION);
  EXPECT_EQ(d.cells, 1);
  EXPECT_EQ(d.module, MOD_E22_900M30S);
  EXPECT_EQ(d.charge_ma, 2000);
}

TEST(DarktecPrefsFormatTest, SerializeParseRoundTrip) {
  Settings in{};
  in.capacity_mah = 3000;
  in.chem = CHEM_LIFEPO4;
  in.cells = 1;
  in.module = MOD_E22P_868M30S;
  in.charge_ma = 1000;

  uint8_t buf[32];
  const size_t n = serialize(in, buf, sizeof(buf));
  EXPECT_EQ(n, kWireSize);
  // magic
  EXPECT_EQ(buf[0], 'D');
  EXPECT_EQ(buf[1], 'R');
  EXPECT_EQ(buf[2], 'T');
  EXPECT_EQ(buf[3], 'C');

  Settings out{};
  ASSERT_TRUE(parse(buf, n, out));
  EXPECT_EQ(out.capacity_mah, 3000);
  EXPECT_EQ(out.chem, CHEM_LIFEPO4);
  EXPECT_EQ(out.cells, 1);
  EXPECT_EQ(out.module, MOD_E22P_868M30S);
  EXPECT_EQ(out.charge_ma, 1000);
}

TEST(DarktecPrefsFormatTest, RejectsCorruption) {
  Settings in{};
  in.capacity_mah = 2500;
  uint8_t buf[32];
  const size_t n = serialize(in, buf, sizeof(buf));
  ASSERT_EQ(n, kWireSize);

  // битый байт payload → CRC не сходится
  Settings out{};
  buf[7] ^= 0x01;  // chem
  EXPECT_FALSE(parse(buf, n, out));
  buf[7] ^= 0x01;

  // битая магия
  buf[0] = 'X';
  EXPECT_FALSE(parse(buf, n, out));
  buf[0] = 'D';

  // чужая версия
  buf[4] = kWireVersion + 1;
  EXPECT_FALSE(parse(buf, n, out));
  buf[4] = kWireVersion;

  // после снятия порчи читается
  EXPECT_TRUE(parse(buf, n, out));
  EXPECT_EQ(out.capacity_mah, 2500);
}

TEST(DarktecPrefsFormatTest, SanitizeClampsRanges) {
  Settings s{};
  s.capacity_mah = 99999;   // вне 500..30000 → дефолт
  s.charge_ma = 50;         // вне 100..3000 → дефолт
  s.chem = 200;             // неизвестная химия → Li-ion
  s.module = 77;            // неизвестный модуль → E22_900M30S
  s.cells = 2;
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 4000);
  EXPECT_EQ(s.charge_ma, 2000);
  EXPECT_EQ(s.chem, CHEM_LIION);
  EXPECT_EQ(s.module, MOD_E22_900M30S);
  // 2S сброшен в 1S: не-LTO не может быть 2 ячейки (US-6)
  EXPECT_EQ(s.cells, 1);
}

TEST(DarktecPrefsFormatTest, LtoAllowsTwoCells) {
  Settings s{};
  s.chem = CHEM_LTO;
  s.cells = 2;
  sanitize(s);
  EXPECT_EQ(s.cells, 2);
}

TEST(DarktecPrefsFormatTest, CapacityBoundaryValuesKept) {
  Settings s{};
  s.capacity_mah = 500;
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 500);
  s.capacity_mah = 30000;
  sanitize(s);
  EXPECT_EQ(s.capacity_mah, 30000);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
