/**
 ******************************************************************************
 * @file    ActivityWriter.hpp
 * @brief   Writes a ski day to a FIT file (the SDK's native FIT encoder).
 *
 * Adapted from the Ride app's writer (itself from UNA's Cycling example),
 * keeping its crash recovery and durability flushes. For skiing:
 *
 *   session  sport alpine_skiing (13) or snowboarding (14); distance, speeds
 *            and descent are the skiing (runs only); ascent is the lifts.
 *   lap      one per run (intensity active) and one per lift ride (intensity
 *            rest). Laps cover the day end to end: each starts where its run
 *            or ride starts and runs on to the start of the next one, so the
 *            queue after a run is in that run's lap (timer and elapsed time
 *            say how much was moving).
 *   record   every second: position, altitude, speed, heart rate (when the
 *            optical sensor has a reading), plus a developer field
 *            "motion" (0 idle, 1 lift, 2 run) and the battery every 5 min.
 ******************************************************************************
 */

#ifndef ACTIVITY_WRITER_HPP
#define ACTIVITY_WRITER_HPP

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>

#include "SDK/Fit/FitProfile.hpp"
#include "SDK/Fit/FitWriter.hpp"
#include "SDK/Fit/RecordingMarker.hpp"
#include "SDK/Kernel/Kernel.hpp"

class ActivityWriter
{
public:
    /// FIT sport numbers the SDK's profile does not list.
    static constexpr uint8_t kSportAlpineSkiing = 13;
    static constexpr uint8_t kSportSnowboarding = 14;

    struct AppInfo {
        std::time_t timestamp  = 0;   // UTC
        uint32_t    appVersion = 0;
        std::string devID;
        std::string appID;
        uint8_t     sport      = kSportAlpineSkiing;
    };

    struct RecordData {
        std::time_t timestamp  = 0;      // UTC
        bool        hasPos     = false;
        float       latitude   = 0.0f;
        float       longitude  = 0.0f;
        bool        hasAlt     = false;
        float       altitude   = 0.0f;   // m
        bool        hasSpeed   = false;
        float       speed      = 0.0f;   // m/s
        uint8_t     motion     = 0;      // Ski::Motion
        uint8_t     heartRate  = 0;      // bpm, 0 = no reading
        bool        hasBattery = false;
        uint8_t     batteryLevel   = 0;  // %
        uint16_t    batteryVoltage = 0;  // mV
    };

    struct LapData {
        std::time_t timestamp = 0;       // end, UTC
        std::time_t timeStart = 0;       // UTC
        uint32_t    elapsedS  = 0;
        uint32_t    timerS    = 0;
        float       distance  = 0.0f;    // m
        float       speedAvg  = 0.0f;    // m/s
        float       speedMax  = 0.0f;    // m/s
        float       ascent    = 0.0f;    // m
        float       descent   = 0.0f;    // m
        uint8_t     hrAvg     = 0;       // bpm, 0 = none
        uint8_t     hrMax     = 0;       // bpm, 0 = none
        SDK::Fit::Intensity  intensity = SDK::Fit::Intensity::Active;
        SDK::Fit::LapTrigger trigger   = SDK::Fit::LapTrigger::Manual;
    };

    struct DayData {
        std::time_t timestamp = 0;       // end, UTC
        std::time_t timeStart = 0;       // UTC
        uint32_t    elapsedS  = 0;
        uint32_t    timerS    = 0;
        float       distance  = 0.0f;    // skiing distance, m
        float       speedAvg  = 0.0f;    // skiing average, m/s
        float       speedMax  = 0.0f;    // m/s
        float       ascent    = 0.0f;    // lift vertical, m
        float       descent   = 0.0f;    // skied vertical, m
        uint8_t     hrAvg     = 0;       // bpm, 0 = none
        uint8_t     hrMax     = 0;       // bpm, 0 = none
        uint16_t    runs      = 0;
    };

    ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir);

    void start(const AppInfo& info);
    bool active() const { return mFit != nullptr; }
    void pause(std::time_t timestamp);
    void resume(std::time_t timestamp);
    void addRecord(const RecordData& record);
    void addLap(const LapData& lap);
    /// Finalize. True iff the FIT file is durably on disk (it is then
    /// registered by the kernel); the .json sidecar is best effort.
    bool stop(const DayData& day);
    void discard();
    /// Finalize an activity a previous boot left unfinished (power loss).
    bool recoverInterrupted();

private:
    enum Local : uint8_t {
        L_FILE_ID = 0,
        L_DEV_ID,
        L_FIELD_DESC,
        L_EVENT,
        L_RECORD,      // no position, no battery
        L_RECORD_G,    // + position
        L_RECORD_B,    // + battery
        L_RECORD_GB,   // + position + battery
        L_LAP,
        L_SESSION,
        L_ACTIVITY,
    };

    enum DevField : uint8_t {
        DF_BATTERY_LEVEL   = 2,   // same numbers as UNA's own apps
        DF_BATTERY_VOLTAGE = 3,
        DF_MOTION          = 10,
    };

    static constexpr std::time_t skFlushIntervalSec = 30;

    const SDK::Kernel& mKernel;
    const char*        mPath = nullptr;
    uint8_t            mSport = kSportAlpineSkiing;

    std::unique_ptr<SDK::Interface::IFile> mFile;
    std::unique_ptr<SDK::Fit::FitWriter>   mFit;
    SDK::Fit::RecordingMarker              mMarker;
    uint16_t    mLapCounter   = 0;
    std::time_t mLastFlushUtc = 0;

    void defineRecordMessages();
    void writeFieldDescription(uint8_t devFieldNum, const char* name, const char* units,
                               SDK::Fit::BaseType baseType);
    void addMessageEvent(std::time_t t, SDK::Fit::EventType type);
    bool createAndOpenFile(std::time_t utc);
    bool saveSummary(const DayData& day);

    static std::time_t tm2epoch(const struct tm* tm);
    static std::time_t epochToLocal(std::time_t utc);
    static uint32_t    unixToFitTimestamp(std::time_t unixTimestamp);
    static int32_t     degreesToSemicircles(float degrees);
};

#endif // ACTIVITY_WRITER_HPP
