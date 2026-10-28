/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   Background process of the Ski Day app.
 *
 * Owns the day: reads the barometer, GPS and battery once a second (or plays
 * the demo day instead), runs the run tracker and the storm watch, writes the
 * FIT file, plays every cue and keeps the season's totals. It keeps going
 * with the screen off or the GUI closed; the GUI only renders.
 *
 * Demo day: the scripted day of SkiDaySim goes through exactly the same
 * pipeline, at 1x, 10x or 60x. In the simulator it is saved as a FIT file
 * (to check the output); on the watch it is not, so a demo never lands in
 * the activity history.
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>
#include <ctime>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Metrics/MonotonicTime.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "ActivityWriter.hpp"
#include "Commands.hpp"
#include "Ski/HeartRateLog.hpp"
#include "Ski/RunTracker.hpp"
#include "Ski/SkiDaySim.hpp"
#include "Ski/StormWatch.hpp"
#include "SkiTypes.hpp"

class Service : public Ski::RunTracker::Listener
{
public:
    explicit Service(SDK::Kernel& kernel);
    ~Service() override;

    void run();

private:
    // -- Constants ------------------------------------------------------------
    static constexpr uint32_t kSamplePeriodMs = 1000;
    static constexpr uint32_t kBatteryLogS    = 300;
    static constexpr uint8_t  kLowBatteryPct  = 20;

    // -- Infrastructure -------------------------------------------------------
    SDK::Kernel& mKernel;
    bool         mGuiStarted = false;
    SDK::Metric::MonotonicTime<SDK::Interface::ISystem> mClock;

    // -- Settings & season ----------------------------------------------------
    Ski::Settings mSettings;
    Ski::Season   mSeason;
    bool          mImperial = false;
    bool          mClock12h = false;

    // -- Sensors --------------------------------------------------------------
    SDK::Sensor::Connection mGpsLocation;
    SDK::Sensor::Connection mGpsSpeed;
    SDK::Sensor::Connection mGpsDistance;
    SDK::Sensor::Connection mPressure;
    SDK::Sensor::Connection mBatteryLevel;
    SDK::Sensor::Connection mBatteryMetrics;
    SDK::Sensor::Connection mWristMotion;
    SDK::Sensor::Connection mHeartRate;
    bool mSensorsWanted = false;

    // Latest readings (real sensors; the demo writes the same fields)
    struct Readings {
        bool   fix        = false;
        double lat        = 0.0;
        double lon        = 0.0;
        float  gpsAlt     = 0.0f;
        bool   speedValid = false;
        float  speed      = 0.0f;
        bool   pressValid = false;
        float  pressPa    = 0.0f;
        float  seaLevelPa = 0.0f;
        float  distTotal  = 0.0f;   ///< cumulative GPS distance (real sensor)
        float  distStep   = 0.0f;   ///< since the last processed second
        bool   hadFix     = false;  ///< any fix since the GUI started
        uint8_t hr        = 0;      ///< bpm, 0 = no reading
        uint32_t hrMs     = 0;      ///< when it was read (kernel ms)
    } mIn;
    float   mLastDistTotal = -1.0f;
    float   mBatteryPct    = 0.0f;
    float   mBatteryVolt   = 0.0f;
    bool    mLowBatteryTold = false;

    // -- The day --------------------------------------------------------------
    Ski::DayState   mState = Ski::DayState::Idle;
    bool            mDemo  = false;
    Ski::DemoSpeed  mDemoSpeed = Ski::DemoSpeed::Off;
    Ski::SkiDaySim  mSim;
    Ski::RunTracker mTracker;
    Ski::StormWatch mStorm;
    Ski::HeartRateLog mHrLog;
    float           mDemoHr = 0.0f;
    Ski::DayLog     mLog;
    ActivityWriter  mWriter;
    bool            mSaved = false;

    std::time_t mDayStartUtc      = 0;
    std::time_t mLocalMidnightUtc = 0;   ///< UTC of local midnight on the day
    std::time_t mNowUtc           = 0;   ///< current second of the day (real or demo)
    std::time_t mLastProcessedUtc = 0;
    std::time_t mDemoUtc0         = 0;   ///< the demo's 00:00, as UTC
    uint32_t    mLastBatteryLogT  = 0;
    float       mP0               = 0.0f;   ///< sea-level pressure for altitude, fixed for the day
    float       mAltOffset        = 0.0f;   ///< GPS-calibrated display offset
    bool        mAltOffsetValid   = false;
    uint32_t    mStillWithFixS    = 0;
    float       mAltDiffSum       = 0.0f;   ///< GPS - baro altitude while standing

    // Laps: each starts at the start of a run or lift ride and runs to the next.
    enum class LapKind : uint8_t { Idle, Run, Lift };
    LapKind     mLapKind  = LapKind::Idle;
    std::time_t mLapStart = 0;

    // -- Lifecycle ------------------------------------------------------------
    void onStartGUI();
    void onStopGUI();
    void connectSensors(bool day);
    void disconnectSensors();
    void handleSensors(uint16_t handle, SDK::Sensor::DataBatch& data);
    uint8_t currentHr() const;

    // -- Commands -------------------------------------------------------------
    void startDay();
    void control(CustomMessage::Control cmd);
    void saveSettings(const Ski::Settings& s);
    void setDemoSpeed(uint8_t speed);

    // -- Per second -----------------------------------------------------------
    void tick();                 ///< once per wall-clock second
    void processSecond();        ///< one second of the day (real or demo)
    void sendLive();
    void sendState();
    void sendSettings();
    void sendLog();
    float displayAltitude(float baroAlt) const;

    // -- Day end --------------------------------------------------------------
    void endDay(bool save);
    void writeLap(LapKind kind, std::time_t start, std::time_t end, bool last);

    // -- RunTracker::Listener -------------------------------------------------
    void onRunStarted(const Ski::RunStats& run) override;
    void onRunEnded(const Ski::RunStats& run, bool bestSpeed, bool bestVert) override;
    void onLiftStarted(const Ski::LiftStats& lift) override;
    void onLiftEnded(const Ski::LiftStats& lift) override;

    // -- Cues -----------------------------------------------------------------
    enum class Cue : uint8_t { DayStart, RunEnd, RunEndBest, LiftStart, Storm, FirstFix,
                               LowBattery, Pause, Resume, DayEnd };
    void play(Cue cue);
    void backlight(uint32_t ms);
    void sendEvent(const Ski::EventData& e);

    // -- Persistence ----------------------------------------------------------
    void loadSettings();
    void storeSettings();
    void updateSeason();
    void readSystemSettings();

    uint16_t localMinute(std::time_t utc) const;
    uint32_t demoStepsPerSecond() const;
};

#endif // SERVICE_HPP
