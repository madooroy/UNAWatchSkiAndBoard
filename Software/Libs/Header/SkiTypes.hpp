/**
 ******************************************************************************
 * @file    SkiTypes.hpp
 * @brief   Data shared by the service and the GUI: settings, the season's
 *          totals, the live snapshot, events and the day log.
 *
 * Plain structs with no SDK dependency (the message wrappers are in
 * Commands.hpp), so the conversions and the profile builder are tested on
 * the host. Values are integers in fixed units to keep the live message
 * small: metres, centimetres per second, seconds.
 ******************************************************************************
 */

#ifndef SKI_TYPES_HPP
#define SKI_TYPES_HPP

#include <cstddef>
#include <cstdint>

#include "Ski/RunTracker.hpp"

namespace Ski
{

// -- Settings -------------------------------------------------------------------

enum class Sport : uint8_t { Ski = 0, Snowboard, COUNT };
enum class AlertMode : uint8_t { BeepAndBuzz = 0, BuzzOnly, BeepOnly, COUNT };
enum class DemoSpeed : uint8_t { Off = 0, X1, X10, X60, COUNT };
enum class DayState : uint8_t { Idle = 0, Active, Paused, Done };

/// Storm alert thresholds offered, hPa per 3 h (0 = off).
constexpr uint8_t kStormChoices[] = { 0, 2, 3, 4 };

struct Settings {
    uint8_t sport     = static_cast<uint8_t>(Sport::Ski);
    uint8_t stormHpa  = 3;    ///< 0 = no storm alerts
    uint8_t runAlerts = 1;    ///< buzz at the end of each run
    uint8_t alertMode = static_cast<uint8_t>(AlertMode::BeepAndBuzz);
    uint8_t demo      = static_cast<uint8_t>(DemoSpeed::Off);
    uint8_t reserved[3] = {};

    void sanitize();
};

/// Lifetime totals and the last day, persisted with the settings.
struct Season {
    uint16_t days        = 0;
    uint16_t runs        = 0;
    uint32_t verticalM   = 0;
    uint16_t topSpeedCms = 0;   ///< fastest ever, cm/s
    uint16_t bestDayM    = 0;   ///< biggest day's vertical
    uint16_t lastRuns    = 0;
    uint16_t lastVertM   = 0;
    uint32_t lastDayUtc  = 0;   ///< 0 = never
};

/// "key=value" lines; unknown keys are ignored, missing ones keep defaults.
size_t settingsToText(const Settings& s, const Season& season, char* buf, size_t len);
void   settingsFromText(const char* text, Settings& s, Season& season);

// -- Live data ------------------------------------------------------------------

struct RunInfo {
    uint16_t number      = 0;   ///< 0 = none
    uint16_t verticalM   = 0;
    uint32_t distanceM   = 0;
    uint16_t maxSpeedCms = 0;
    uint16_t avgSpeedCms = 0;
    uint16_t durationS   = 0;
    uint16_t startMin    = 0;   ///< local minute of the day it started
};

struct LiftInfo {
    uint16_t number    = 0;     ///< 0 = none
    uint16_t gainM     = 0;
    uint16_t durationS = 0;
    uint16_t topAltM   = 0;
};

struct TotalsInfo {
    uint16_t runs         = 0;
    uint16_t lifts        = 0;
    uint32_t verticalM    = 0;
    uint32_t liftVertM    = 0;
    uint32_t distanceM    = 0;
    uint16_t maxSpeedCms  = 0;
    uint16_t longestRunM  = 0;
    uint32_t runTimeS     = 0;
    uint32_t liftTimeS    = 0;
    uint32_t totalTimeS   = 0;
};

struct StormInfo {
    static constexpr uint8_t kSpark = 12;
    uint8_t  level      = 0;        ///< StormWatch::Level
    uint8_t  spots      = 0;
    uint16_t readings   = 0;
    int16_t  rateCenti  = 0;        ///< hPa per 3 h x 100 (sea-level equivalent)
    uint16_t pressureDeci = 0;      ///< latest station pressure, hPa x 10
    uint16_t ageMin     = 0xFFFF;   ///< minutes since the trend was computed
    uint8_t  thresholdHpa = 3;
    uint8_t  sparkCount = 0;
    int16_t  spark[kSpark] = {};    ///< busiest spot, hPa x 10 relative to its latest
};

struct LiveData {
    uint8_t  motion    = 0;        ///< Ski::Motion
    uint8_t  gpsFix    = 0;
    uint8_t  battery   = 0;        ///< %
    uint8_t  altValid  = 0;
    uint16_t stillS    = 0;        ///< seconds standing still
    uint16_t todMin    = 0;        ///< local time, minutes of the day
    uint16_t speedCms  = 0;        ///< current speed, cm/s
    int16_t  vsCms     = 0;        ///< vertical speed, cm/s, + up
    int16_t  altM      = 0;        ///< altitude, m
    uint16_t demoPermille = 0;     ///< demo day progress
    RunInfo    run;                ///< the open run (number 0 if none)
    RunInfo    lastRun;            ///< the last finished run
    LiftInfo   lift;               ///< the open lift (number 0 if none)
    TotalsInfo totals;
    StormInfo  storm;
    uint8_t    heartRate = 0;      ///< bpm, 0 = no reading
};

enum class EventKind : uint8_t {
    None = 0,
    RunEnded,
    LiftStarted,
    Storm,
    FirstFix,
    LowBattery,
};

struct EventData {
    uint8_t  kind      = 0;   ///< EventKind
    uint8_t  bestSpeed = 0;   ///< RunEnded: new top speed of the day
    uint8_t  bestVert  = 0;   ///< RunEnded: biggest vertical of the day
    uint8_t  reserved  = 0;
    int16_t  rateCenti = 0;   ///< Storm: hPa per 3 h x 100
    uint16_t value     = 0;   ///< LowBattery: %
    RunInfo  run;             ///< RunEnded
};

// -- Day log (summary screen) ---------------------------------------------------

/// Altitude through the day for the summary's profile: a fixed number of
/// buckets whose length doubles whenever the day outgrows them, so any day
/// fits in the same memory.
class DayProfile
{
public:
    static constexpr uint8_t kBuckets = 120;

    void reset(uint32_t t0, uint16_t bucketS = 30);
    void add(uint32_t t, float alt, Motion m);

    uint8_t  count() const       { return mCount; }
    uint16_t bucketSeconds() const { return mBucketS; }
    int16_t  minAlt(uint8_t i) const  { return mMin[i]; }
    int16_t  maxAlt(uint8_t i) const  { return mMax[i]; }
    Motion   motion(uint8_t i) const  { return static_cast<Motion>(mKind[i]); }
    /// Over all buckets.
    void     range(int16_t& lo, int16_t& hi) const;

private:
    uint32_t mT0      = 0;
    uint16_t mBucketS = 30;
    uint8_t  mCount   = 0;
    int16_t  mMin[kBuckets] = {};
    int16_t  mMax[kBuckets] = {};
    uint8_t  mKind[kBuckets] = {};

    void compress();
};

struct DayLog {
    static constexpr uint8_t kMaxRuns = 60;

    uint8_t    count     = 0;   ///< runs held (the first kMaxRuns of the day)
    uint8_t    sport     = 0;
    uint8_t    demo      = 0;
    uint8_t    saved     = 0;
    uint8_t    hrAvg     = 0;   ///< bpm over the day, 0 = no readings
    uint8_t    hrMax     = 0;
    uint16_t   startMin  = 0;   ///< local minute the day started
    uint16_t   endMin    = 0;
    RunInfo    runs[kMaxRuns];
    TotalsInfo totals;
    DayProfile profile;

    void reset();
    void addRun(const RunInfo& r);
};

// -- Conversions ----------------------------------------------------------------

uint16_t   toCms(float mps);
RunInfo    toInfo(const RunStats& r, uint32_t localDayStartUtc);
LiftInfo   toInfo(const LiftStats& l);
TotalsInfo toInfo(const DayTotals& d);

} // namespace Ski

#endif // SKI_TYPES_HPP
