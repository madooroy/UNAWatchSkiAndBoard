// Heart rate per lap, with laps dated back after the fact.

#include "Ski/HeartRateLog.hpp"

#include <gtest/gtest.h>

using Ski::HeartRateLog;

TEST(HeartRateLog, LapsDatedBackGetTheirOwnSeconds)
{
    HeartRateLog h;
    h.reset(1000);
    for (uint32_t t = 1000; t < 1600; ++t) h.add(t, 90);    // lift: 90
    for (uint32_t t = 1600; t < 1900; ++t) h.add(t, 150);   // run: 150
    for (uint32_t t = 1900; t < 2000; ++t) h.add(t, 100);   // next lift, confirmed late
    // The lift lap is written when the run is confirmed, after the fact.
    auto lift = h.closeLap(1000, 1600);
    EXPECT_EQ(lift.avg, 90);
    EXPECT_EQ(lift.max, 90);
    auto run = h.closeLap(1600, 1900);
    EXPECT_EQ(run.avg, 150);
    EXPECT_EQ(run.max, 150);
    auto day = h.day();
    EXPECT_EQ(day.max, 150);
    EXPECT_NEAR(day.avg, (600 * 90 + 300 * 150 + 100 * 100) / 1000.0, 1.0);
}

TEST(HeartRateLog, LongLapsFoldOldSeconds)
{
    HeartRateLog h;
    h.reset(0);
    for (uint32_t t = 0; t < 3 * 3600; ++t) h.add(t, t < 3600 ? 80 : 120);   // lunch, then skiing
    auto lap = h.closeLap(0, 3 * 3600);
    EXPECT_NEAR(lap.avg, (80 + 2 * 120) / 3.0, 1.0);
    EXPECT_EQ(lap.max, 120);
}

TEST(HeartRateLog, NoReadingsIsZero)
{
    HeartRateLog h;
    h.reset(0);
    for (uint32_t t = 0; t < 100; ++t) h.add(t, 0);
    auto lap = h.closeLap(0, 100);
    EXPECT_EQ(lap.avg, 0);
    EXPECT_EQ(lap.max, 0);
    EXPECT_EQ(h.day().avg, 0);
}

TEST(HeartRateLog, GapsCountAsNoReading)
{
    HeartRateLog h;
    h.reset(0);
    h.add(0, 100);
    h.add(500, 140);   // paused in between
    auto lap = h.closeLap(0, 501);
    EXPECT_EQ(lap.avg, 120);
    EXPECT_EQ(lap.max, 140);
}
