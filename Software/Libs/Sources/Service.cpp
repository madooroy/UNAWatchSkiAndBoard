/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   Ski Day service: sensors, the day, the FIT file, cues, season.
 ******************************************************************************
 */

#include "Service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

using Ski::DayState;
using Ski::Motion;
using VibroFx = SDK::Message::RequestVibroPlay::Effect;

namespace
{
constexpr const char kSettingsPath[] = "skiday.cfg";
constexpr uint32_t   kStartupGraceMs = 5000;

#if defined(SIMULATOR)
constexpr bool kSaveDemo = true;    // check the FIT output in the simulator
#else
constexpr bool kSaveDemo = false;   // never put a demo day in the watch's history
#endif

// -- Cue design ----------------------------------------------------------------
//
// Nothing plays while you ski. The end of a run is told on the chair (a run
// is only known to be over once the next lift starts), when you can look.
// Each cue differs by ear and by feel; the storm warning falls in pitch, as
// the pressure does.

struct Tone {
    uint16_t ms;
    uint16_t hz;       // 0 = silence
    uint8_t  volume;   // 0..100
};
struct Buzz {
    uint8_t  effect;   // VibroFx, 0 = pause
    uint16_t pauseMs;
};
struct CuePattern {
    Tone     tones[SDK::Message::RequestBuzzerPlay::skMaxNotes];
    uint8_t  toneCount;
    Buzz     buzz[SDK::Message::RequestVibroPlay::skMaxNotes];
    uint8_t  buzzCount;
    uint16_t backlightMs;
};

// clang-format off
const CuePattern kDayStart = {
    { { 120, 1600, 66 }, { 60, 0, 0 }, { 220, 2400, 100 } }, 3,
    { { VibroFx::STRONG_CLICK_100, 0 } }, 1,
    3000,
};
const CuePattern kRunEnd = {
    { { 140, 2000, 66 } }, 1,
    { { VibroFx::DOUBLE_CLICK_100, 0 } }, 1,
    6000,
};
const CuePattern kRunEndBest = {
    { { 120, 1800, 100 }, { 60, 0, 0 }, { 120, 2400, 100 }, { 60, 0, 0 }, { 280, 3000, 100 } }, 5,
    { { VibroFx::LONG_DOUBLE_SHARP_CLICK_STRONG_1_100, 0 } }, 1,
    8000,
};
const CuePattern kLiftStart = {
    { }, 0,
    { { VibroFx::SOFT_BUMP_100, 0 } }, 1,
    0,
};
const CuePattern kStorm = {
    { { 300, 2400, 100 }, { 120, 0, 0 }, { 300, 1800, 100 }, { 120, 0, 0 }, { 500, 1200, 100 } }, 5,
    { { VibroFx::ALERT_750MS_100, 0 }, { 0, 250 }, { VibroFx::ALERT_750MS_100, 0 }, { 0, 250 },
      { VibroFx::ALERT_750MS_100, 0 } }, 5,
    15000,
};
const CuePattern kFirstFix = {
    { { 80, 2600, 66 } }, 1,
    { { VibroFx::SHARP_CLICK_100, 0 } }, 1,
    0,
};
const CuePattern kLowBattery = {
    { { 200, 1200, 100 }, { 100, 0, 0 }, { 200, 1200, 100 } }, 3,
    { { VibroFx::DOUBLE_CLICK_100, 0 } }, 1,
    5000,
};
const CuePattern kPause = {
    { { 90, 1200, 66 } }, 1,
    { { VibroFx::SOFT_BUMP_100, 0 } }, 1,
    0,
};
const CuePattern kResume = {
    { { 90, 2000, 66 } }, 1,
    { { VibroFx::SHARP_CLICK_100, 0 } }, 1,
    0,
};
const CuePattern kDayEnd = {
    { { 150, 2093, 100 }, { 50, 0, 0 }, { 150, 2637, 100 }, { 50, 0, 0 }, { 400, 3136, 100 } }, 5,
    { { VibroFx::STRONG_BUZZ_100, 0 } }, 1,
    8000,
};
// clang-format on

/// "1.2.3" -> FIT app version (bytes LE: patch, minor, major, 0). The SDK's
/// ParseVersion() uses sscanf, which drags newlib's file I/O (and its missing
/// syscalls) into the service; this does not.
uint32_t appVersion(const char* s)
{
    uint32_t part[3] = { 0, 0, 0 };
    for (uint8_t i = 0; i < 3 && s && *s; ++i) {
        while (*s >= '0' && *s <= '9') {
            part[i] = part[i] * 10u + static_cast<uint32_t>(*s++ - '0');
        }
        if (*s == '.') {
            ++s;
        } else {
            break;
        }
    }
    return (part[0] & 0xFFu) << 16 | (part[1] & 0xFFu) << 8 | (part[2] & 0xFFu);
}

float baroAltitude(float pa, float p0)
{
    return 44330.77f * (1.0f - std::pow(pa / p0, 0.1902632f));
}

/// Median of the last three valid speeds (display and FIT), like the tracker.
struct SpeedMedian {
    float   v[3] = {};
    uint8_t n = 0, pos = 0;
    void reset() { n = pos = 0; }
    float add(float s)
    {
        v[pos] = s;
        pos    = static_cast<uint8_t>((pos + 1u) % 3u);
        if (n < 3) {
            ++n;
        }
        if (n < 3) {
            return s;
        }
        return std::max(std::min(v[0], v[1]), std::min(std::max(v[0], v[1]), v[2]));
    }
};
SpeedMedian gSpeedMedian;
float       gDisplaySpeed = 0.0f;

} // namespace

// -----------------------------------------------------------------------------

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mClock(kernel.sys)
    , mGpsLocation(SDK::Sensor::Type::GPS_LOCATION, kSamplePeriodMs, kSamplePeriodMs)
    , mGpsSpeed(SDK::Sensor::Type::GPS_SPEED, kSamplePeriodMs, kSamplePeriodMs)
    , mGpsDistance(SDK::Sensor::Type::GPS_DISTANCE, kSamplePeriodMs, kSamplePeriodMs)
    , mPressure(SDK::Sensor::Type::PRESSURE, kSamplePeriodMs, kSamplePeriodMs)
    , mBatteryLevel(SDK::Sensor::Type::BATTERY_LEVEL)
    , mBatteryMetrics(SDK::Sensor::Type::BATTERY_METRICS, 60000, 60000)
    , mWristMotion(SDK::Sensor::Type::WRIST_MOTION)
    , mHeartRate(SDK::Sensor::Type::HEART_RATE, kSamplePeriodMs, 2 * kSamplePeriodMs)
    , mWriter(kernel, "Activity")
{
    mTracker.setListener(this);
}

Service::~Service()
{
    mTracker.setListener(nullptr);
    disconnectSensors();
}

void Service::run()
{
    LOG_INFO("Ski Day service started\n");
    mClock.init();
    loadSettings();

    if (mWriter.recoverInterrupted()) {
        LOG_INFO("Recovered an interrupted day\n");
        auto* m = mKernel.comm.allocateMessage<SDK::Message::CommandAppNewActivity>();
        if (m) {
            mKernel.comm.sendMessage(m);
            mKernel.comm.releaseMessage(m);
        }
    }

    const uint32_t startMs    = mKernel.sys.getTimeMs();
    std::time_t    lastSecond = 0;

    while (true) {
        const bool     busy    = mState == DayState::Active || mState == DayState::Paused;
        const uint32_t waitMs  = busy ? 100 : 500;

        SDK::MessageBase* msg = nullptr;
        if (mKernel.comm.getMessage(msg, waitMs)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Stop requested\n");
                    if (busy) {
                        endDay(true);   // never lose a day
                    }
                    disconnectSensors();
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    onStartGUI();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    onStopGUI();
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto* ev = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(ev->data, ev->count, ev->stride);
                    handleSensors(ev->handle, batch);
                } break;

                case CustomMessage::DAY_START:
                    startDay();
                    break;

                case CustomMessage::DAY_CONTROL:
                    control(static_cast<CustomMessage::DayControl*>(msg)->cmd);
                    break;

                case CustomMessage::SETTINGS_SAVE:
                    saveSettings(static_cast<CustomMessage::SettingsSave*>(msg)->settings);
                    break;

                case CustomMessage::DEMO_SPEED:
                    setDemoSpeed(static_cast<CustomMessage::DemoSpeed*>(msg)->speed);
                    break;

                default:
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        const std::time_t now = mClock.getExpectedUTC();
        if (now != lastSecond) {
            lastSecond = now;
            tick();
        }

        const bool idle = mState == DayState::Idle || mState == DayState::Done;
        if (!mGuiStarted && idle &&
            static_cast<int32_t>(mKernel.sys.getTimeMs() - startMs) > static_cast<int32_t>(kStartupGraceMs)) {
            LOG_INFO("No GUI and no day running: exiting\n");
            break;
        }
    }
    disconnectSensors();
}

// -- Lifecycle ------------------------------------------------------------------

void Service::onStartGUI()
{
    LOG_INFO("GUI running\n");
    mGuiStarted = true;
    readSystemSettings();
    mWristMotion.connect();
    // GPS starts acquiring on the home screen, so it is ready by the lift.
    if (mState == DayState::Idle && mSettings.demo == 0) {
        connectSensors(false);
    }
    sendSettings();
    sendState();
    if (mState == DayState::Done) {
        sendLog();
    }
    sendLive();
}

void Service::onStopGUI()
{
    const bool busy = mState == DayState::Active || mState == DayState::Paused;
    LOG_INFO("GUI stopped%s\n", busy ? " (the day goes on)" : "");
    mGuiStarted = false;
    mWristMotion.disconnect();
    if (!busy) {
        disconnectSensors();
    }
}

void Service::connectSensors(bool day)
{
    mSensorsWanted = true;
    // Idempotent: also retries a subscription that lost the connect race.
    if (!mGpsLocation.isConnected())    { mGpsLocation.connect(); }
    if (!mGpsSpeed.isConnected())       { mGpsSpeed.connect(); }
    if (!mBatteryLevel.isConnected())   { mBatteryLevel.connect(); }
    if (day) {
        if (!mGpsDistance.isConnected())    { mGpsDistance.connect(); }
        if (!mPressure.isConnected())       { mPressure.connect(); }
        if (!mBatteryMetrics.isConnected()) { mBatteryMetrics.connect(); }
        if (!mHeartRate.isConnected())      { mHeartRate.connect(); }
    }
}

void Service::disconnectSensors()
{
    mSensorsWanted = false;
    mGpsLocation.disconnect();
    mGpsSpeed.disconnect();
    mGpsDistance.disconnect();
    mPressure.disconnect();
    mBatteryLevel.disconnect();
    mBatteryMetrics.disconnect();
    mHeartRate.disconnect();
}

void Service::handleSensors(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    if (data.size() == 0) {
        return;
    }
    const auto& last = data[data.size() - 1];

    if (mGpsLocation.matchesDriver(handle)) {
        SDK::SensorDataParser::GpsLocation p(last);
        if (p.isDataValid()) {
            const bool fix = p.isCoordinatesValid();
            if (!mDemo) {
                mIn.fix = fix;
                if (fix) {
                    float lat = 0, lon = 0, alt = 0;
                    p.getCoordinates(lat, lon, alt);
                    mIn.lat    = lat;
                    mIn.lon    = lon;
                    mIn.gpsAlt = alt;
                }
            }
            if (fix && !mIn.hadFix) {
                mIn.hadFix = true;
                if (!mDemo) {
                    play(Cue::FirstFix);
                    Ski::EventData e;
                    e.kind = static_cast<uint8_t>(Ski::EventKind::FirstFix);
                    sendEvent(e);
                }
            }
        }
    } else if (mGpsSpeed.matchesDriver(handle)) {
        SDK::SensorDataParser::GpsSpeed p(last);
        if (p.isDataValid() && !mDemo) {
            mIn.speed      = p.getSpeed();
            mIn.speedValid = p.isSpeedValid();
        }
    } else if (mGpsDistance.matchesDriver(handle)) {
        SDK::SensorDataParser::GpsDistance p(last);
        if (p.isDataValid() && !mDemo) {
            mIn.distTotal = p.getDistance();
        }
    } else if (mPressure.matchesDriver(handle)) {
        SDK::SensorDataParser::Pressure p(last);
        if (p.isDataValid() && !mDemo) {
            mIn.pressPa    = p.getPressure();
            mIn.seaLevelPa = p.getP0();
            mIn.pressValid = std::isfinite(mIn.pressPa) && mIn.pressPa > 10000.0f;
        }
    } else if (mBatteryLevel.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryLevel p(last);
        if (p.isDataValid()) {
            mBatteryPct = p.getCharge();
        }
    } else if (mBatteryMetrics.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryMetrics p(last);
        if (p.isDataValid()) {
            mBatteryVolt = p.getVoltage();
        }
    } else if (mHeartRate.matchesDriver(handle)) {
        SDK::SensorDataParser::HeartRate p(last);
        const float bpm = p.isDataValid() ? p.getBpm() : 0.0f;
        if (bpm >= 30.0f && bpm <= 240.0f && !mDemo) {
            mIn.hr   = static_cast<uint8_t>(bpm + 0.5f);
            mIn.hrMs = mKernel.sys.getTimeMs();
        }
    } else if (mWristMotion.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion p(last);
        if (p.isDataValid()) {
            backlight(5000);   // a glance with gloves on: no button needed
        }
    }
}

// -- Commands -------------------------------------------------------------------

void Service::startDay()
{
    if (mState == DayState::Active || mState == DayState::Paused) {
        return;
    }
    mDemo      = mSettings.demo != 0;
    mDemoSpeed = static_cast<Ski::DemoSpeed>(mDemo ? mSettings.demo : 0);

    mClock.init();
    const std::time_t wall  = mClock.getExpectedUTC();
    const std::tm     local = mClock.getLocalTime(wall);
    mLocalMidnightUtc = wall - (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);

    mTracker.reset();
    mStorm.reset();
    mDemoHr = 0.0f;
    mIn.hr  = 0;
    mStorm.setThreshold(mSettings.stormHpa);
    mLog.reset();
    gSpeedMedian.reset();
    gDisplaySpeed = 0.0f;

    if (mDemo) {
        Ski::SkiDaySim::Options o;   // the demo day: storm in the afternoon
        mSim         = Ski::SkiDaySim(o);
        mDemoUtc0    = mLocalMidnightUtc;
        mDayStartUtc = mDemoUtc0 + Ski::SkiDaySim::kDayStartS;
        mIn          = Readings{};
        mIn.hadFix   = true;
        mGpsLocation.disconnect();   // the demo needs no GPS
        mGpsSpeed.disconnect();
        if (!mBatteryLevel.isConnected()) {
            mBatteryLevel.connect();
        }
    } else {
        mDayStartUtc   = wall;
        mLastDistTotal = -1.0f;
        connectSensors(true);
    }

    mNowUtc           = mDayStartUtc;
    mLastProcessedUtc = 0;
    mP0               = 0.0f;
    mAltOffset        = 0.0f;
    mAltOffsetValid   = false;
    mStillWithFixS    = 0;
    mAltDiffSum       = 0.0f;
    mLastBatteryLogT  = 0;
    mLowBatteryTold   = false;
    mLapKind          = LapKind::Idle;
    mLapStart         = mDayStartUtc;
    mSaved            = false;
    mHrLog.reset(static_cast<uint32_t>(mDayStartUtc));

    mLog.sport    = mSettings.sport;
    mLog.demo     = mDemo ? 1 : 0;
    mLog.startMin = localMinute(mDayStartUtc);
    mLog.profile.reset(static_cast<uint32_t>(mDayStartUtc), 30);

    if (!mDemo || kSaveDemo) {
        ActivityWriter::AppInfo info;
        info.timestamp  = mDayStartUtc;
        info.appVersion = appVersion(BUILD_VERSION);
        info.devID      = DEV_ID;
        info.appID      = APP_ID;
        info.sport      = mSettings.sport == static_cast<uint8_t>(Ski::Sport::Snowboard)
                              ? ActivityWriter::kSportSnowboarding
                              : ActivityWriter::kSportAlpineSkiing;
        mWriter.start(info);
    }

    mState = DayState::Active;
    LOG_INFO("Day started (%s)\n", mDemo ? "demo" : "real");
    play(Cue::DayStart);
    sendState();
}

void Service::control(CustomMessage::Control cmd)
{
    using CustomMessage::Control;
    switch (cmd) {
        case Control::PAUSE:
            if (mState == DayState::Active) {
                mState = DayState::Paused;
                mWriter.pause(mNowUtc);
                play(Cue::Pause);
                sendState();
            }
            break;
        case Control::RESUME:
            if (mState == DayState::Paused) {
                mState = DayState::Active;
                if (!mDemo) {
                    mNowUtc = mClock.getExpectedUTC();
                }
                mWriter.resume(mNowUtc);
                play(Cue::Resume);
                sendState();
            }
            break;
        case Control::END_SAVE:
            endDay(true);
            break;
        case Control::DISCARD:
            endDay(false);
            break;
        case Control::HOME:
            if (mState == DayState::Done) {
                mState = DayState::Idle;
                if (mGuiStarted && mSettings.demo == 0) {
                    connectSensors(false);
                }
                sendState();
            }
            break;
    }
}

void Service::saveSettings(const Ski::Settings& s)
{
    const bool demoWas = mSettings.demo != 0;
    mSettings          = s;
    mSettings.sanitize();
    mStorm.setThreshold(mSettings.stormHpa);
    storeSettings();

    const bool demoNow = mSettings.demo != 0;
    if (mState == DayState::Idle && mGuiStarted && demoWas != demoNow) {
        if (demoNow) {
            disconnectSensors();
            mBatteryLevel.connect();
        } else {
            connectSensors(false);
        }
    }
    sendSettings();
}

void Service::setDemoSpeed(uint8_t speed)
{
    if (!mDemo || speed == 0 || speed >= static_cast<uint8_t>(Ski::DemoSpeed::COUNT)) {
        return;
    }
    mDemoSpeed = static_cast<Ski::DemoSpeed>(speed);
    sendState();
}

uint32_t Service::demoStepsPerSecond() const
{
    switch (mDemoSpeed) {
        case Ski::DemoSpeed::X10: return 10;
        case Ski::DemoSpeed::X60: return 60;
        default:                  return 1;
    }
}

// -- Per second -----------------------------------------------------------------

void Service::tick()
{
    if (mSensorsWanted && !mDemo) {
        connectSensors(mState == DayState::Active || mState == DayState::Paused);
    }

    if (mState == DayState::Active) {
        if (mDemo) {
            const uint32_t steps = demoStepsPerSecond();
            for (uint32_t i = 0; i < steps && mState == DayState::Active; ++i) {
                Ski::SkiDaySim::Output o;
                if (!mSim.step(o)) {
                    LOG_INFO("Demo day finished\n");
                    endDay(true);
                    break;
                }
                mIn.fix        = o.fix;
                mIn.lat        = o.lat;
                mIn.lon        = o.lon;
                mIn.gpsAlt     = o.gpsAlt;
                mIn.speed      = o.speed;
                mIn.speedValid = o.speedValid;
                mIn.pressPa    = o.pressurePa;
                mIn.seaLevelPa = o.seaLevelPa;
                mIn.pressValid = true;
                mIn.distStep   = o.dist;
                // Demo heart rate: easy on lifts and in queues, up with speed
                // on a run (the simulator's own sensor is random).
                const float target = (o.speedValid && o.speed > 3.0f)
                                         ? std::min(172.0f, 112.0f + 3.0f * o.speed)
                                         : 86.0f;
                mDemoHr  = mDemoHr > 0.0f ? mDemoHr + 0.06f * (target - mDemoHr) : 82.0f;
                mIn.hr   = static_cast<uint8_t>(mDemoHr + 0.5f);
                mIn.hrMs = mKernel.sys.getTimeMs();
                mNowUtc        = mDemoUtc0 + o.t;
                processSecond();
            }
        } else {
            mNowUtc = mClock.getExpectedUTC();
            if (mLastDistTotal >= 0.0f && mIn.distTotal >= mLastDistTotal) {
                mIn.distStep = mIn.distTotal - mLastDistTotal;
            } else {
                mIn.distStep = 0.0f;
            }
            mLastDistTotal = mIn.distTotal;
            if (mNowUtc != mLastProcessedUtc) {
                processSecond();
            }
        }
    } else if (mState == DayState::Paused && !mDemo) {
        // Paused: no tracking, but the storm watch keeps reading (a long
        // lunch indoors is exactly when it gets its best readings).
        mNowUtc = mClock.getExpectedUTC();
        mStorm.add(static_cast<uint32_t>(mNowUtc), mIn.pressPa, mIn.pressValid, mIn.speed,
                   mIn.speedValid, mIn.lat, mIn.lon, mIn.fix);
        if (mStorm.takeAlert()) {
            Ski::EventData e;
            e.kind      = static_cast<uint8_t>(Ski::EventKind::Storm);
            e.rateCenti = static_cast<int16_t>(std::lround(mStorm.trend().hpaPer3h * 100.0f));
            play(Cue::Storm);
            sendEvent(e);
        }
    }

    if (!mLowBatteryTold && mBatteryPct > 0.5f && mBatteryPct <= kLowBatteryPct &&
        (mState == DayState::Active || mState == DayState::Paused)) {
        mLowBatteryTold = true;
        Ski::EventData e;
        e.kind  = static_cast<uint8_t>(Ski::EventKind::LowBattery);
        e.value = static_cast<uint16_t>(mBatteryPct + 0.5f);
        play(Cue::LowBattery);
        sendEvent(e);
    }

    sendLive();
}

uint8_t Service::currentHr() const
{
    // A reading older than 5 s (sensor off the wrist, lost lock) is no reading.
    return (mIn.hr && mKernel.sys.getTimeMs() - mIn.hrMs < 5000u) ? mIn.hr : 0;
}

float Service::displayAltitude(float baroAlt) const
{
    return mAltOffsetValid ? baroAlt + mAltOffset : baroAlt;
}

void Service::processSecond()
{
    mLastProcessedUtc = mNowUtc;
    const uint32_t t  = static_cast<uint32_t>(mNowUtc);

    // Barometric altitude, against a sea-level pressure fixed for the day:
    // run and lift heights are differences over minutes, so the weather's
    // drift does not matter to them (the display altitude is corrected
    // separately, below).
    float baro = 0.0f;
    if (mIn.pressValid) {
        if (mP0 <= 0.0f) {
            mP0 = (mIn.seaLevelPa > 50000.0f && mIn.seaLevelPa < 120000.0f) ? mIn.seaLevelPa : 101325.0f;
        }
        baro = baroAltitude(mIn.pressPa, mP0);
    }

    Ski::Sample s;
    s.t          = t;
    s.alt        = baro;
    s.altValid   = mIn.pressValid;
    s.speed      = mIn.speed;
    s.speedValid = mIn.speedValid;
    s.dist       = mIn.distStep;
    mTracker.add(s);

    gDisplaySpeed = mIn.speedValid ? gSpeedMedian.add(mIn.speed) : 0.0f;

    const uint8_t hr = currentHr();
    mHrLog.add(t, hr);

    mStorm.add(t, mIn.pressPa, mIn.pressValid, mIn.speed, mIn.speedValid, mIn.lat, mIn.lon, mIn.fix);
    if (mStorm.takeAlert()) {
        Ski::EventData e;
        e.kind      = static_cast<uint8_t>(Ski::EventKind::Storm);
        e.rateCenti = static_cast<int16_t>(std::lround(mStorm.trend().hpaPer3h * 100.0f));
        LOG_INFO("STORM %d.%02d hPa/3h\n", e.rateCenti / 100, std::abs(e.rateCenti % 100));
        play(Cue::Storm);
        sendEvent(e);
    }

    // Display altitude: while standing still with a fix, pull the barometric
    // altitude towards the average GPS altitude (which the weather does not
    // move). A minute's average is good to a few metres.
    if (mIn.pressValid && mIn.fix && mIn.speedValid && mIn.speed < 0.7f) {
        mAltDiffSum += mIn.gpsAlt - baro;
        if (++mStillWithFixS >= 60) {
            const float mean = mAltDiffSum / static_cast<float>(mStillWithFixS);
            mAltOffset       = mAltOffsetValid ? mAltOffset + 0.3f * (mean - mAltOffset) : mean;
            mAltOffsetValid  = true;
            mStillWithFixS   = 0;
            mAltDiffSum      = 0.0f;
        }
    } else {
        mStillWithFixS = 0;
        mAltDiffSum    = 0.0f;
    }

    const float alt = displayAltitude(baro);
    if (mIn.pressValid) {
        mLog.profile.add(t, alt, mTracker.motion());
    }

    if (mWriter.active()) {
        ActivityWriter::RecordData r;
        r.timestamp = mNowUtc;
        r.hasPos    = mIn.fix;
        r.latitude  = static_cast<float>(mIn.lat);
        r.longitude = static_cast<float>(mIn.lon);
        r.hasAlt    = mIn.pressValid;
        r.altitude  = alt;
        r.hasSpeed  = mIn.speedValid;
        r.speed     = gDisplaySpeed;
        r.motion    = static_cast<uint8_t>(mTracker.motion());
        r.heartRate = hr;
        if (t - mLastBatteryLogT >= kBatteryLogS && mBatteryPct > 0.0f) {
            mLastBatteryLogT = t;
            r.hasBattery     = true;
            r.batteryLevel   = static_cast<uint8_t>(mBatteryPct + 0.5f);
            r.batteryVoltage = static_cast<uint16_t>(mBatteryVolt * 1000.0f);
        }
        mWriter.addRecord(r);
    }
}

// -- Laps and the end of the day --------------------------------------------------

void Service::writeLap(LapKind kind, std::time_t start, std::time_t end, bool last)
{
    const Ski::HeartRateLog::Stats hr =
        mHrLog.closeLap(static_cast<uint32_t>(start), static_cast<uint32_t>(end));
    if (!mWriter.active() || end <= start) {
        return;
    }
    ActivityWriter::LapData lap;
    lap.hrAvg     = hr.avg;
    lap.hrMax     = hr.max;
    lap.timeStart = start;
    lap.timestamp = end;
    lap.elapsedS  = static_cast<uint32_t>(end - start);
    lap.timerS    = lap.elapsedS;
    lap.trigger   = last ? SDK::Fit::LapTrigger::SessionEnd : SDK::Fit::LapTrigger::Manual;

    if (kind == LapKind::Run) {
        // Laps close when the next run or lift starts (or the day ends, after
        // finish()), so the segment they describe is always the last closed one.
        const Ski::RunStats& r = mTracker.lastRun();
        lap.distance  = r.distance;
        lap.speedAvg  = r.avgSpeed();
        lap.speedMax  = r.maxSpeed;
        lap.descent   = r.vertical();
        lap.intensity = SDK::Fit::Intensity::Active;
    } else if (kind == LapKind::Lift) {
        const Ski::LiftStats& l = mTracker.lastLift();
        lap.distance  = l.distance;
        lap.speedAvg  = l.duration() ? l.distance / static_cast<float>(l.duration()) : 0.0f;
        lap.ascent    = l.gain();
        lap.intensity = SDK::Fit::Intensity::Rest;
    } else {
        lap.intensity = SDK::Fit::Intensity::Rest;
    }
    mWriter.addLap(lap);
}

void Service::endDay(bool save)
{
    if (mState != DayState::Active && mState != DayState::Paused) {
        return;
    }
    const std::time_t end = mNowUtc;
    mTracker.finish();   // closes an open run or lift (listener events fire)
    writeLap(mLapKind, mLapStart, end, true);

    const Ski::DayTotals d = mTracker.totals();
    mLog.totals = Ski::toInfo(d);
    mLog.endMin = localMinute(end);

    mSaved = false;
    if (mWriter.active()) {
        if (save && d.totalTime > 0) {
            ActivityWriter::DayData day;
            day.timeStart = mDayStartUtc;
            day.timestamp = end;
            day.elapsedS  = static_cast<uint32_t>(end - mDayStartUtc);
            day.timerS    = d.totalTime;
            day.distance  = d.distance;
            day.speedMax  = d.maxSpeed;
            day.ascent    = d.liftVertical;
            day.descent   = d.vertical;
            day.runs      = d.runs;
            day.hrAvg     = mHrLog.day().avg;
            day.hrMax     = mHrLog.day().max;
            float moving  = 0.0f;
            for (uint8_t i = 0; i < mLog.count; ++i) {
                if (mLog.runs[i].avgSpeedCms) {
                    moving += static_cast<float>(mLog.runs[i].distanceM) * 100.0f / mLog.runs[i].avgSpeedCms;
                }
            }
            day.speedAvg = moving > 0.0f ? d.distance / moving : 0.0f;
            mSaved       = mWriter.stop(day);
            if (mSaved) {
                auto* m = mKernel.comm.allocateMessage<SDK::Message::CommandAppNewActivity>();
                if (m) {
                    mKernel.comm.sendMessage(m);
                    mKernel.comm.releaseMessage(m);
                }
            }
        } else {
            mWriter.discard();
        }
    }
    mLog.saved = mSaved ? 1 : 0;
    mLog.hrAvg = mHrLog.day().avg;
    mLog.hrMax = mHrLog.day().max;

    if (save && !mDemo && d.totalTime > 0) {
        updateSeason();
    }

    mState = DayState::Done;
    LOG_INFO("Day ended: %u runs, %lu m, saved=%u\n", d.runs, static_cast<unsigned long>(mLog.totals.verticalM),
             mSaved ? 1u : 0u);
    disconnectSensors();
    play(Cue::DayEnd);
    sendState();
    sendLog();
    sendLive();
}

void Service::updateSeason()
{
    const Ski::TotalsInfo& t = mLog.totals;
    mSeason.days        = static_cast<uint16_t>(std::min<uint32_t>(mSeason.days + 1u, 0xFFFF));
    mSeason.runs        = static_cast<uint16_t>(std::min<uint32_t>(mSeason.runs + t.runs, 0xFFFF));
    mSeason.verticalM  += t.verticalM;
    mSeason.topSpeedCms = std::max(mSeason.topSpeedCms, t.maxSpeedCms);
    mSeason.bestDayM    = static_cast<uint16_t>(std::max<uint32_t>(mSeason.bestDayM, std::min<uint32_t>(t.verticalM, 0xFFFF)));
    mSeason.lastRuns    = t.runs;
    mSeason.lastVertM   = static_cast<uint16_t>(std::min<uint32_t>(t.verticalM, 0xFFFF));
    mSeason.lastDayUtc  = static_cast<uint32_t>(mDayStartUtc);
    storeSettings();
    sendSettings();
}

// -- RunTracker events ------------------------------------------------------------

void Service::onRunStarted(const Ski::RunStats& run)
{
    writeLap(mLapKind, mLapStart, run.tStart, false);
    mLapKind  = LapKind::Run;
    mLapStart = run.tStart;
    LOG_INFO("Run %u started\n", run.number);
}

void Service::onRunEnded(const Ski::RunStats& run, bool bestSpeed, bool bestVert)
{
    const Ski::RunInfo info = Ski::toInfo(run, static_cast<uint32_t>(mLocalMidnightUtc));
    mLog.addRun(info);
    LOG_INFO("Run %u: %u m, %lu m, top %u cm/s\n", info.number, info.verticalM,
             static_cast<unsigned long>(info.distanceM), info.maxSpeedCms);

    Ski::EventData e;
    e.kind      = static_cast<uint8_t>(Ski::EventKind::RunEnded);
    e.bestSpeed = (bestSpeed && run.number > 1) ? 1 : 0;   // the first run is trivially best
    e.bestVert  = (bestVert && run.number > 1) ? 1 : 0;
    e.run       = info;
    if (mSettings.runAlerts) {
        play(e.bestSpeed ? Cue::RunEndBest : Cue::RunEnd);
    }
    sendEvent(e);
}

void Service::onLiftStarted(const Ski::LiftStats& lift)
{
    writeLap(mLapKind, mLapStart, lift.tStart, false);
    mLapKind  = LapKind::Lift;
    mLapStart = lift.tStart;
    LOG_INFO("Lift %u started\n", lift.number);
    if (mSettings.runAlerts) {
        play(Cue::LiftStart);
    }
    Ski::EventData e;
    e.kind = static_cast<uint8_t>(Ski::EventKind::LiftStarted);
    sendEvent(e);
}

void Service::onLiftEnded(const Ski::LiftStats& lift)
{
    LOG_INFO("Lift %u: %u m in %u s\n", lift.number, static_cast<unsigned>(lift.gain()),
             static_cast<unsigned>(lift.duration()));
}

// -- Messages to the GUI ------------------------------------------------------------

uint16_t Service::localMinute(std::time_t utc) const
{
    if (utc < mLocalMidnightUtc) {
        return 0;
    }
    return static_cast<uint16_t>(((utc - mLocalMidnightUtc) / 60) % 1440);
}

void Service::sendState()
{
    if (!mGuiStarted) {
        return;
    }
    auto g = SDK::make_msg<CustomMessage::State>(mKernel);
    if (!g) {
        return;
    }
    g->dayState  = static_cast<uint8_t>(mState);
    g->demo      = mDemo ? 1 : 0;
    g->demoSpeed = static_cast<uint8_t>(mDemoSpeed);
    g->saved     = mSaved ? 1 : 0;
    g.send();
}

void Service::sendSettings()
{
    if (!mGuiStarted) {
        return;
    }
    auto g = SDK::make_msg<CustomMessage::Settings>(mKernel);
    if (!g) {
        return;
    }
    g->settings = mSettings;
    g->season   = mSeason;
    g->imperial = mImperial ? 1 : 0;
    g->clock12h = mClock12h ? 1 : 0;
    g.send();
}

void Service::sendLog()
{
    if (mGuiStarted) {
        SDK::send_msg<CustomMessage::DayLog>(mKernel, &mLog);
    }
}

void Service::sendEvent(const Ski::EventData& e)
{
    if (mGuiStarted) {
        SDK::send_msg<CustomMessage::Event>(mKernel, e);
    }
}

void Service::sendLive()
{
    if (!mGuiStarted) {
        return;
    }
    auto g = SDK::make_msg<CustomMessage::Live>(mKernel);
    if (!g) {
        return;
    }
    Ski::LiveData& d = g->data;
    d.battery        = static_cast<uint8_t>(std::min(100.0f, std::max(0.0f, mBatteryPct + 0.5f)));
    d.gpsFix         = mIn.fix ? 1 : 0;

    const bool day = mState != DayState::Idle;
    if (!day) {
        const std::time_t wall  = mClock.getExpectedUTC();
        const std::tm     local = mClock.getLocalTime(wall);
        d.todMin = static_cast<uint16_t>(local.tm_hour * 60 + local.tm_min);
        g.send();
        return;
    }

    const Motion m = mTracker.motion();
    d.motion   = static_cast<uint8_t>(m);
    d.heartRate = currentHr();
    d.todMin   = localMinute(mNowUtc);
    d.stillS   = static_cast<uint16_t>(std::min<uint32_t>(mTracker.stillSeconds(), 0xFFFF));
    d.speedCms = Ski::toCms(gDisplaySpeed);
    d.vsCms    = static_cast<int16_t>(std::lround(std::max(-300.0f, std::min(300.0f, mTracker.verticalSpeed() * 100.0f))));
    d.altValid = mTracker.altitudeValid() ? 1 : 0;
    d.altM     = static_cast<int16_t>(std::lround(displayAltitude(mTracker.altitude())));
    d.demoPermille = mDemo ? mSim.progressPermille() : 0;

    const uint32_t mid = static_cast<uint32_t>(mLocalMidnightUtc);
    if (m == Motion::Run) {
        d.run = Ski::toInfo(mTracker.currentRun(), mid);
    }
    if (mTracker.lastRun().number) {
        d.lastRun = Ski::toInfo(mTracker.lastRun(), mid);
    }
    if (m == Motion::Lift) {
        d.lift = Ski::toInfo(mTracker.currentLift());
    }
    d.totals = Ski::toInfo(mTracker.totals());

    // Storm watch
    const Ski::StormWatch::Trend& tr = mStorm.trend();
    Ski::StormInfo& st = d.storm;
    st.level        = static_cast<uint8_t>(tr.level);
    st.spots        = tr.spots;
    st.readings     = tr.readings;
    st.rateCenti    = static_cast<int16_t>(std::lround(std::max(-300.0f, std::min(300.0f, tr.hpaPer3h)) * 100.0f));
    st.pressureDeci = static_cast<uint16_t>(std::lround(std::max(0.0f, tr.pressureHpa) * 10.0f));
    st.thresholdHpa = mSettings.stormHpa;
    st.ageMin       = (tr.level == Ski::StormWatch::Level::Learning || mNowUtc < tr.t)
                          ? 0xFFFF
                          : static_cast<uint16_t>(std::min<std::time_t>((mNowUtc - tr.t) / 60, 0xFFFE));
    if (const Ski::StormWatch::Spot* sp = mStorm.busiestSpot(static_cast<uint32_t>(mNowUtc))) {
        const uint8_t n     = std::min<uint8_t>(sp->count, Ski::StormInfo::kSpark);
        const uint8_t first = static_cast<uint8_t>(sp->count - n);
        const float   ref   = sp->r[sp->count - 1].pa;
        for (uint8_t i = 0; i < n; ++i) {
            st.spark[i] = static_cast<int16_t>(std::lround((sp->r[first + i].pa - ref) / 10.0f));
        }
        st.sparkCount = n;
    }
    g.send();
}

// -- Cues -------------------------------------------------------------------------

void Service::backlight(uint32_t ms)
{
    auto* m = mKernel.comm.allocateMessage<SDK::Message::RequestBacklightSet>();
    if (m) {
        m->brightness       = 100;
        m->autoOffTimeoutMs = ms;
        mKernel.comm.sendMessage(m);
        mKernel.comm.releaseMessage(m);
    }
}

void Service::play(Cue cue)
{
    const CuePattern* p = nullptr;
    switch (cue) {
        case Cue::DayStart:   p = &kDayStart;   break;
        case Cue::RunEnd:     p = &kRunEnd;     break;
        case Cue::RunEndBest: p = &kRunEndBest; break;
        case Cue::LiftStart:  p = &kLiftStart;  break;
        case Cue::Storm:      p = &kStorm;      break;
        case Cue::FirstFix:   p = &kFirstFix;   break;
        case Cue::LowBattery: p = &kLowBattery; break;
        case Cue::Pause:      p = &kPause;      break;
        case Cue::Resume:     p = &kResume;     break;
        case Cue::DayEnd:     p = &kDayEnd;     break;
    }
    if (!p) {
        return;
    }
    const auto mode  = static_cast<Ski::AlertMode>(mSettings.alertMode);
    const bool beep  = mode != Ski::AlertMode::BuzzOnly;
    const bool vibro = mode != Ski::AlertMode::BeepOnly;

    if (p->backlightMs) {
        backlight(p->backlightMs);
    }
    if (vibro && p->buzzCount) {
        auto* m = mKernel.comm.allocateMessage<SDK::Message::RequestVibroPlay>();
        if (m) {
            for (uint8_t i = 0; i < p->buzzCount; ++i) {
                m->notes[i].effect = p->buzz[i].effect;
                m->notes[i].pause  = p->buzz[i].pauseMs;
            }
            m->notesCount = p->buzzCount;
            mKernel.comm.sendMessage(m);
            mKernel.comm.releaseMessage(m);
        }
    }
    if (beep && p->toneCount) {
        auto* m = mKernel.comm.allocateMessage<SDK::Message::RequestBuzzerPlay>();
        if (m) {
            for (uint8_t i = 0; i < p->toneCount; ++i) {
                m->notes[i].time      = p->tones[i].ms;
                m->notes[i].volume    = p->tones[i].volume;
                m->notes[i].frequency = p->tones[i].hz;
            }
            m->notesCount = p->toneCount;
            mKernel.comm.sendMessage(m);
            mKernel.comm.releaseMessage(m);
        }
    }
}

// -- Persistence ------------------------------------------------------------------

void Service::readSystemSettings()
{
    if (auto msg = SDK::make_msg<SDK::Message::RequestSystemSettings>(mKernel)) {
        if (msg.send(100) && msg.ok()) {
            mImperial = msg->imperialUnits;
            mClock12h = msg->timeFormat;
        }
    }
#if defined(SIMULATOR)
    // The simulator's watch is always metric; UNA_SIM_IMPERIAL=1 tries imperial.
    if (const char* e = std::getenv("UNA_SIM_IMPERIAL"); e && e[0] == '1') {
        mImperial = true;
        mClock12h = true;
    }
#endif
}

void Service::loadSettings()
{
    auto file = mKernel.fs.file(kSettingsPath);
    if (!file || !file->exist()) {
        LOG_INFO("No settings yet, using defaults\n");
        return;
    }
    char   buf[320] = {};
    size_t br       = 0;
    if (file->open()) {
        file->read(buf, sizeof(buf) - 1, br);
        file->close();
    }
    buf[std::min(br, sizeof(buf) - 1)] = '\0';
    Ski::settingsFromText(buf, mSettings, mSeason);
}

void Service::storeSettings()
{
    char         buf[320];
    const size_t len = Ski::settingsToText(mSettings, mSeason, buf, sizeof(buf));
    if (len == 0) {
        return;
    }
    auto file = mKernel.fs.file(kSettingsPath);
    if (!file) {
        return;
    }
    size_t bw = 0;
    if (file->open(true, true)) {
        file->write(buf, len, bw);
        file->close();
    }
    if (bw != len) {
        LOG_WARNING("Settings not saved\n");
    }
}
