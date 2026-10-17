#include <gtest/gtest.h>

#include <DarktecSoc.h>

// Контрольные точки — из замера MacMillan25 (spec 0001, AC-9):
// Li-ion 4000 mAh, разряд ~0.1 А за 36.9 ч, отдача 3690 mAh (заряд принял
// 4133 mAh), full 4158 мВ, empty 3006 мВ. SOC на сетке таблицы совпадает
// с индексом; интерполяция — линейная.

TEST(DarktecSocTablesTest, LiIonControlPointsFromMeasurement) {
  // Полный и пустой по факту замера (не по номиналу 4200/3000).
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 4158), 100);
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 3006), 0);
  // Точка из CSV: 3.5810 В при отданных 2000 mAh → SOC = 100−2000/3690·100 ≈ 46 %.
  const uint8_t soc = DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 3581);
  EXPECT_GE(soc, 44);
  EXPECT_LE(soc, 48);
}

TEST(DarktecSocTablesTest, LiIonGridRoundTrip) {
  // Каждая точка сетки таблицы воспроизводит свой SOC ровно.
  for (int i = 0; i < DARKTEC_SOC_POINTS; i++) {
    const uint16_t mv = DarktecSocTables::kLiion[i];
    EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, mv), i * DARKTEC_SOC_STEP_PCT)
        << "i=" << i << " mv=" << mv;
  }
}

TEST(DarktecSocTablesTest, LiIonInterpolatesBetweenCsvPoints) {
  // Между сеточными точками SOC растёт с напряжением и не прыгает больше
  // чем на шаг сетки плюс погрешность интерполяции.
  const int a = DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 3581);
  const int b = DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 3600);
  EXPECT_GT(b, a);   // напряжение растёт — SOC строго растёт
  EXPECT_LE(b - a, 6);
}

TEST(DarktecSocTablesTest, LiIonClampsOutOfRange) {
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 5000), 100);
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiIon, 1000), 0);
}

TEST(DarktecSocTablesTest, LiFePO4AnchorsAndPlateau) {
  // Опорные точки §8: 3.400 В → 100 %, 2.500 В → 0 %.
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiFePO4, 3400), 100);
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiFePO4, 2500), 0);
  // Плато: 3.29 В — середина; на плоском участке напряжение не различает
  // SOC (FR-22: точность обеспечивает кулонометр), допускаем широкий коридор.
  const uint8_t soc = DarktecSoc::socFromCellMv(DarktecSoc::Chem::LiFePO4, 3290);
  EXPECT_GE(soc, 45);
  EXPECT_LE(soc, 55);
}

TEST(DarktecSocTablesTest, LtoAnchorsAndMonotonic) {
  // LTO: окно 1.8–2.5 В (наш консервативный full 2.5 В).
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LTO, 2500), 100);
  EXPECT_EQ(DarktecSoc::socFromCellMv(DarktecSoc::Chem::LTO, 1800), 0);
  const uint8_t soc = DarktecSoc::socFromCellMv(DarktecSoc::Chem::LTO, 2360);
  EXPECT_GE(soc, 45);
  EXPECT_LE(soc, 55);
}

TEST(DarktecSocTablesTest, AllTablesMonotonic) {
  const uint16_t* tables[] = {DarktecSocTables::kLiion, DarktecSocTables::kLifepo4,
                              DarktecSocTables::kLto};
  for (const uint16_t* t : tables) {
    for (int i = 1; i < DARKTEC_SOC_POINTS; i++) {
      ASSERT_LE(t[i - 1], t[i]) << "i=" << i;
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
