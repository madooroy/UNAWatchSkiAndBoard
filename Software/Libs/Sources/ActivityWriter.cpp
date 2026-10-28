/**
 ******************************************************************************
 * @file    ActivityWriter.cpp
 * @brief   Ski day -> FIT file. Adapted from the Ride app's writer.
 ******************************************************************************
 */

#include "ActivityWriter.hpp"

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include <cassert>
#include <cstring>

#define LOG_MODULE_PRX      "ActivityWriter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace fit = SDK::Fit;
using Field       = fit::FitWriter::Field;
using DevFieldDef = fit::FitWriter::DevField;

namespace
{
constexpr uint8_t kNoHr = 0xFF;   // FIT "invalid" for a uint8 field
uint32_t invalidU32() { return static_cast<uint32_t>(fit::baseTypeInvalid(fit::BaseType::UInt32)); }

uint16_t clampU16(float v)
{
    return static_cast<uint16_t>(v <= 0.0f ? 0.0f : (v >= 65534.0f ? 65534.0f : v + 0.5f));
}
} // namespace

ActivityWriter::ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir)
    : mKernel(kernel), mPath(pathToDir), mMarker(kernel.fs, pathToDir)
{
    assert(pathToDir != nullptr);
}

void ActivityWriter::start(const AppInfo& info)
{
    mLapCounter   = 0;
    mLastFlushUtc = info.timestamp;
    mSport        = info.sport;

    if (!createAndOpenFile(info.timestamp)) {
        return;
    }

    mFit = std::make_unique<fit::FitWriter>(*mFile);
    const bool begun = mFit->begin(/*profileVersion=*/0);
    if (!begun) {
        LOG_ERROR("Failed to write FIT header\n");
    }

    // file_id
    const uint8_t productNameLen = static_cast<uint8_t>(std::strlen(fit::kProductName) + 1);
    mFit->defineMessage(L_FILE_ID, fit::mesgNum(fit::MesgNum::FileId),
        {fit::field::FileId::Type, fit::field::FileId::Manufacturer,
         fit::field::FileId::Product, fit::field::FileId::SerialNumber,
         fit::field::FileId::TimeCreated,
         {fit::field::FileId::kProductNameNum, fit::BaseType::String, productNameLen}});
    mFit->data(L_FILE_ID)
        .u8(static_cast<uint8_t>(fit::File::Activity))
        .u16(static_cast<uint16_t>(fit::Manufacturer::Una))
        .u16(static_cast<uint16_t>(fit::Product::UnaWatch))
        .u32(0)
        .u32(unixToFitTimestamp(info.timestamp))
        .str(fit::kProductName, productNameLen)
        .write();

    // developer_data_id
    mFit->defineMessage(L_DEV_ID, fit::mesgNum(fit::MesgNum::DeveloperDataId),
        {fit::field::DeveloperDataId::ApplicationId, fit::field::DeveloperDataId::DeveloperDataIndex});
    {
        uint8_t appId[16] = {};
        std::strncpy(reinterpret_cast<char*>(appId), info.appID.c_str(), sizeof(appId));
        mFit->data(L_DEV_ID).bytes(appId, sizeof(appId)).u8(0).write();
    }

    writeFieldDescription(DF_BATTERY_LEVEL, "batteryLevel", "%", fit::BaseType::UInt8);
    writeFieldDescription(DF_BATTERY_VOLTAGE, "battVoltage", "mV", fit::BaseType::UInt16);
    writeFieldDescription(DF_MOTION, "motion", nullptr, fit::BaseType::UInt8);   // 0 idle, 1 lift, 2 run

    mFit->defineMessage(L_EVENT, fit::mesgNum(fit::MesgNum::Event),
        {fit::field::Event::Timestamp, fit::field::Event::EventField,
         fit::field::Event::EventType, fit::field::Event::Data});

    defineRecordMessages();

    mFit->defineMessage(L_LAP, fit::mesgNum(fit::MesgNum::Lap),
        {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
         fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
         fit::field::Lap::TotalDistance, fit::field::Lap::MessageIndex,
         fit::field::Lap::AvgSpeed, fit::field::Lap::MaxSpeed,
         fit::field::Lap::TotalAscent, fit::field::Lap::TotalDescent,
         fit::field::Lap::Intensity, fit::field::Lap::LapTrigger,
         fit::field::Lap::AvgHeartRate, fit::field::Lap::MaxHeartRate});
    mFit->defineMessage(L_SESSION, fit::mesgNum(fit::MesgNum::Session),
        {fit::field::Session::Timestamp, fit::field::Session::StartTime,
         fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,
         fit::field::Session::TotalDistance, fit::field::Session::MessageIndex,
         fit::field::Session::AvgSpeed, fit::field::Session::MaxSpeed,
         fit::field::Session::TotalAscent, fit::field::Session::TotalDescent,
         fit::field::Session::NumLaps, fit::field::Session::Sport,
         fit::field::Session::SubSport,
         fit::field::Session::AvgHeartRate, fit::field::Session::MaxHeartRate});
    mFit->defineMessage(L_ACTIVITY, fit::mesgNum(fit::MesgNum::Activity),
        {fit::field::Activity::Timestamp, fit::field::Activity::TotalTimerTime,
         fit::field::Activity::LocalTimestamp, fit::field::Activity::NumSessions});

    addMessageEvent(info.timestamp, fit::EventType::Start);

    // Header and definitions on disk: drop the recovery marker at this clean
    // boundary, so a crash from here on is recoverable.
    if (begun && mFile->flush()) {
        mMarker.write(mFile->getPath(), static_cast<uint32_t>(mFile->getPosition()));
    }
}

void ActivityWriter::defineRecordMessages()
{
    const DevFieldDef motion1[] = { {DF_MOTION, 1, 0} };
    const DevFieldDef batt3[]   = { {DF_MOTION, 1, 0}, {DF_BATTERY_LEVEL, 1, 0}, {DF_BATTERY_VOLTAGE, 2, 0} };

    mFit->defineMessage(L_RECORD, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::EnhancedAltitude,
         fit::field::Record::EnhancedSpeed, fit::field::Record::HeartRate},
        {motion1[0]});
    mFit->defineMessage(L_RECORD_G, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::PositionLat,
         fit::field::Record::PositionLong, fit::field::Record::EnhancedAltitude,
         fit::field::Record::EnhancedSpeed, fit::field::Record::HeartRate},
        {motion1[0]});
    mFit->defineMessage(L_RECORD_B, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::EnhancedAltitude,
         fit::field::Record::EnhancedSpeed, fit::field::Record::HeartRate},
        {batt3[0], batt3[1], batt3[2]});
    mFit->defineMessage(L_RECORD_GB, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::PositionLat,
         fit::field::Record::PositionLong, fit::field::Record::EnhancedAltitude,
         fit::field::Record::EnhancedSpeed, fit::field::Record::HeartRate},
        {batt3[0], batt3[1], batt3[2]});
}

void ActivityWriter::writeFieldDescription(uint8_t devFieldNum, const char* name, const char* units,
                                           fit::BaseType baseType)
{
    const uint8_t nameLen  = name ? static_cast<uint8_t>(std::strlen(name) + 1) : 1;
    const uint8_t unitsLen = units ? static_cast<uint8_t>(std::strlen(units) + 1) : 1;

    mFit->defineMessage(L_FIELD_DESC, fit::mesgNum(fit::MesgNum::FieldDescription),
        {fit::field::FieldDescription::DeveloperDataIndex,
         fit::field::FieldDescription::FieldDefinitionNumber,
         fit::field::FieldDescription::FitBaseTypeId,
         {fit::field::FieldDescription::kFieldNameNum, fit::BaseType::String, nameLen},
         {fit::field::FieldDescription::kUnitsNum, fit::BaseType::String, unitsLen}});
    mFit->data(L_FIELD_DESC)
        .u8(0)
        .u8(devFieldNum)
        .u8(fit::baseTypeId(baseType))
        .str(name ? name : "", nameLen)
        .str(units ? units : "", unitsLen)
        .write();
}

void ActivityWriter::pause(std::time_t timestamp)
{
    if (mFit) {
        addMessageEvent(timestamp, fit::EventType::Stop);
    }
}

void ActivityWriter::resume(std::time_t timestamp)
{
    if (mFit) {
        addMessageEvent(timestamp, fit::EventType::Start);
    }
}

void ActivityWriter::addRecord(const RecordData& r)
{
    if (!mFit) {
        return;
    }
    const uint8_t local = r.hasBattery ? (r.hasPos ? L_RECORD_GB : L_RECORD_B)
                                       : (r.hasPos ? L_RECORD_G : L_RECORD);
    fit::FitWriter::Data d = mFit->data(local);
    d.u32(unixToFitTimestamp(r.timestamp));
    if (r.hasPos) {
        d.i32(degreesToSemicircles(r.latitude)).i32(degreesToSemicircles(r.longitude));
    }
    // enhanced_altitude (5 * (m + 500)), enhanced_speed (1000 * m/s)
    d.u32(r.hasAlt ? static_cast<uint32_t>((r.altitude + 500.0f) * 5.0f) : invalidU32());
    d.u32(r.hasSpeed ? static_cast<uint32_t>(r.speed * 1000.0f) : invalidU32());
    d.u8(r.heartRate ? r.heartRate : kNoHr);
    d.u8(r.motion);
    if (r.hasBattery) {
        d.u8(r.batteryLevel).u16(r.batteryVoltage);
    }
    d.write();

    // Periodic durability flush, advancing the crash-recovery marker.
    if (r.timestamp - mLastFlushUtc >= skFlushIntervalSec) {
        if (mFile->flush()) {
            mMarker.update(static_cast<uint32_t>(mFile->getPosition()));
            mLastFlushUtc = r.timestamp;
        }
    }
}

void ActivityWriter::addLap(const LapData& lap)
{
    if (!mFit) {
        return;
    }
    mFit->data(L_LAP)
        .u32(unixToFitTimestamp(lap.timestamp))
        .u32(unixToFitTimestamp(lap.timeStart))
        .u32(lap.elapsedS * 1000u)
        .u32(lap.timerS * 1000u)
        .u32(static_cast<uint32_t>(lap.distance * 100.0f))
        .u16(mLapCounter)
        .u16(clampU16(lap.speedAvg * 1000.0f))
        .u16(clampU16(lap.speedMax * 1000.0f))
        .u16(clampU16(lap.ascent))
        .u16(clampU16(lap.descent))
        .u8(static_cast<uint8_t>(lap.intensity))
        .u8(static_cast<uint8_t>(lap.trigger))
        .u8(lap.hrAvg ? lap.hrAvg : kNoHr)
        .u8(lap.hrMax ? lap.hrMax : kNoHr)
        .write();
    ++mLapCounter;

    if (mFile->flush()) {
        mMarker.update(static_cast<uint32_t>(mFile->getPosition()));
        mLastFlushUtc = lap.timestamp;
    }
}

bool ActivityWriter::stop(const DayData& day)
{
    if (!mFit) {
        return false;
    }
    bool ok = mFit->ok();

    ok = mFit->data(L_SESSION)
        .u32(unixToFitTimestamp(day.timestamp))
        .u32(unixToFitTimestamp(day.timeStart))
        .u32(day.elapsedS * 1000u)
        .u32(day.timerS * 1000u)
        .u32(static_cast<uint32_t>(day.distance * 100.0f))
        .u16(0)
        .u16(clampU16(day.speedAvg * 1000.0f))
        .u16(clampU16(day.speedMax * 1000.0f))
        .u16(clampU16(day.ascent))
        .u16(clampU16(day.descent))
        .u16(mLapCounter)
        .u8(mSport)
        .u8(static_cast<uint8_t>(fit::SubSport::Generic))
        .u8(day.hrAvg ? day.hrAvg : kNoHr)
        .u8(day.hrMax ? day.hrMax : kNoHr)
        .write() && ok;

    ok = mFit->data(L_ACTIVITY)
        .u32(unixToFitTimestamp(day.timestamp))
        .u32(day.timerS * 1000u)
        .u32(unixToFitTimestamp(epochToLocal(day.timestamp)))
        .u16(1)
        .write() && ok;

    const bool finishOk = mFit->finish();
    if (!finishOk) {
        LOG_ERROR("Failed to finalize FIT file\n");
    }
    ok = finishOk && mFit->ok() && ok;
    mFit.reset();

    if (mFile) {
        ok = mFile->flush() && ok;
        ok = mFile->close() && ok;
    } else {
        ok = false;
    }
    if (ok) {
        mMarker.remove();
    }
    if (ok && !saveSummary(day)) {
        LOG_ERROR("Activity summary (.json) save failed; the FIT file is saved\n");
    }
    return ok;
}

void ActivityWriter::discard()
{
    mFit.reset();
    mMarker.remove();
    if (!mFile) {
        return;
    }
    if (mFile->isOpen()) {
        mFile->close();
    }
    mFile->remove();
    mFile.reset();
}

void ActivityWriter::addMessageEvent(std::time_t t, fit::EventType type)
{
    mFit->data(L_EVENT)
        .u32(unixToFitTimestamp(t))
        .u8(static_cast<uint8_t>(fit::Event::Timer))
        .u8(static_cast<uint8_t>(type))
        .u32(static_cast<uint32_t>(fit::TimerTrigger::Manual))
        .write();
}

bool ActivityWriter::recoverInterrupted()
{
    const auto result = mMarker.recover();
    if (result.recovered) {
        LOG_INFO("Recovery: finalized interrupted activity [%s]\n", result.path.c_str());
    }
    return result.recovered;
}

bool ActivityWriter::createAndOpenFile(std::time_t utc)
{
    char    buff[256]{};
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif
    const int len = snprintf(buff, sizeof(buff), "%s/%04u%02u/", mPath,
                             localTime.tm_year + 1900, localTime.tm_mon + 1);
    if (len <= 0 || !mKernel.fs.mkdir(buff)) {
        LOG_ERROR("Failed to create dir [%s]\n", buff);
        return false;
    }
    snprintf(&buff[len], sizeof(buff) - len, "activity_%04u%02u%02uT%02u%02u%02u.fit",
             localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
             localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

    mFile = mKernel.fs.file(buff);
    if (!mFile || !mFile->open(true, true)) {
        LOG_ERROR("Failed to create file [%s]\n", buff);
        mFile.reset();
        return false;
    }
    return true;
}

bool ActivityWriter::saveSummary(const DayData& day)
{
    if (!mFile) {
        return false;
    }
    char   buff[256]{};
    size_t nameLen = strlen(mFile->getPath());
    snprintf(buff, sizeof(buff), "%.*s%s", static_cast<int>(nameLen - 3), mFile->getPath(), "json");
    mFile->setPath(buff);
    if (!mFile->open(true, true)) {
        LOG_ERROR("Failed to open activity summary [%s]\n", buff);
        mFile.reset();
        return false;
    }

    SDK::JsonStreamWriter writer(mFile.get());
    writer.startMap();
    writer.add("time_start", static_cast<uint32_t>(day.timeStart));
    writer.add("duration", static_cast<uint32_t>(day.elapsedS));
    writer.add("distance", day.distance);
    writer.add("elevation", day.descent);   // skied vertical: the number a skier quotes
    writer.add("activity_type", mSport == kSportSnowboarding ? "snowboarding" : "alpine_skiing");
    writer.add("runs", static_cast<uint32_t>(day.runs));
    writer.endMap();

    const bool ok = mFile->flush();
    return mFile->close() && ok;
}

std::time_t ActivityWriter::tm2epoch(const struct tm* tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int d = tm->tm_mday;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    uint32_t doy = (153 * (m - 3) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t  days = era * 146097 + static_cast<int64_t>(doe) - 719468;
    int64_t  secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    return static_cast<std::time_t>(secs);
}

std::time_t ActivityWriter::epochToLocal(std::time_t utc)
{
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif
    return tm2epoch(&localTime);
}

uint32_t ActivityWriter::unixToFitTimestamp(std::time_t unixTimestamp)
{
    const std::time_t kFitEpochOffset = 631065600;
    return static_cast<uint32_t>(unixTimestamp - kFitEpochOffset);
}

int32_t ActivityWriter::degreesToSemicircles(float degrees)
{
    return static_cast<int32_t>(degrees * (2147483648.0 / 180.0));
}
