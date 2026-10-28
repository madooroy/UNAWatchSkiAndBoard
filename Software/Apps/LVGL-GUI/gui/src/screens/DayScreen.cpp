/**
 ******************************************************************************
 * @file    DayScreen.cpp
 ******************************************************************************
 */

#include "gui/screens/DayScreen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "SDK/GUI/Button.hpp"
#include "SDK/GUI/LVGL/Draw.hpp"

#include "Ski/Format.hpp"
#include "Ski/StormWatch.hpp"
#include "gui/Assets.hpp"
#include "gui/Theme.hpp"

namespace Draw = SDK::LVGL::Draw;
namespace Fmt  = Ski::Fmt;
using SDK::LVGL::Buttons;
using Ski::Motion;

namespace
{
const char* const kDot = " \xC2\xB7 ";   // " · "

lv_obj_t* face(lv_obj_t* root)
{
    return Draw::container(root, 0, 0, 240, 240);
}

const char* stormText(Ski::StormWatch::Level l)
{
    using L = Ski::StormWatch::Level;
    switch (l) {
        case L::Learning:    return "Learning";
        case L::Rising:      return "Rising";
        case L::Steady:      return "Steady";
        case L::Falling:     return "Falling slowly";
        case L::FallingFast: return "Falling fast";
        case L::Storm:       return "Storm coming";
    }
    return "";
}

uint32_t stormColor(Ski::StormWatch::Level l)
{
    using L = Ski::StormWatch::Level;
    switch (l) {
        case L::Storm:       return Theme::kDanger;
        case L::FallingFast: return Theme::kWarn;
        case L::Falling:     return Theme::kWarn;
        case L::Rising:      return Theme::kGood;
        case L::Steady:      return Theme::kText;
        default:             return Theme::kTextDim;
    }
}
} // namespace

DayScreen::DayScreen(Model& model)
    : Screen(model)
{
    // -- Live ---------------------------------------------------------------
    mLive = face(mRoot);
    mRing = std::make_unique<SDK::LVGL::TimerRing>(mLive);
    mLHeader  = Draw::label(mLive, &poppins_semibold_15, "", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mLCaption = Draw::label(mLive, &poppins_regular_15, "", 40, 46, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mLBig     = Draw::label(mLive, &poppins_semibold_72, "", 10, 50, 220, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mLUnit    = Draw::label(mLive, &poppins_medium_18, "", 0, 0, 80, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);
    mLLine1   = Draw::label(mLive, &poppins_medium_22, "", 20, 132, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mLLine2   = Draw::label(mLive, &poppins_regular_15, "", 30, 160, 180, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mLFoot    = Draw::label(mLive, &poppins_regular_15, "", 50, 184, 140, LV_TEXT_ALIGN_CENTER, Theme::kLift);

    // -- Day ----------------------------------------------------------------
    mDay     = face(mRoot);
    mDHeader = Draw::label(mDay, &poppins_semibold_15, "TODAY", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mDRuns   = Draw::label(mDay, &poppins_semibold_72, "0", 20, 38, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mDRunsU  = Draw::label(mDay, &poppins_medium_18, "runs", 0, 0, 60, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);
    mDVert   = Draw::label(mDay, &poppins_medium_22, "", 20, 120, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mDLine   = Draw::label(mDay, &poppins_regular_15, "", 30, 148, 180, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mDBarRun  = Draw::box(mDay, 60, 174, 40, 8, Theme::kRun, 3);
    mDBarLift = Draw::box(mDay, 100, 174, 40, 8, Theme::kLift, 3);
    mDBarIdle = Draw::box(mDay, 140, 174, 40, 8, Theme::kTrack, 3);
    mDSplit   = Draw::label(mDay, &poppins_regular_15, "", 40, 188, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);

    // -- Storm --------------------------------------------------------------
    mStorm   = face(mRoot);
    mSHeader = Draw::label(mStorm, &poppins_semibold_15, "STORM WATCH", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mSRate   = Draw::label(mStorm, &poppins_semibold_48, "", 20, 46, 150, LV_TEXT_ALIGN_RIGHT, Theme::kText);
    mSUnit   = Draw::label(mStorm, &poppins_regular_15, "", 172, 58, 60, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);
    mSpark   = std::make_unique<Sparkline>(mStorm, 64, 106, 112, 36);
    mSStatus = Draw::label(mStorm, &poppins_medium_22, "", 20, 146, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mSInfo   = Draw::label(mStorm, &poppins_regular_15, "", 36, 174, 168, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mSPress  = Draw::label(mStorm, &poppins_regular_15, "", 50, 194, 140, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);

    // -- Clock --------------------------------------------------------------
    mClock = face(mRoot);
    mCTime = Draw::label(mClock, &poppins_semibold_72, "", 0, 50, 240, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mCDate = Draw::label(mClock, &poppins_semibold_15, "", 40, 34, 160, LV_TEXT_ALIGN_CENTER, Theme::kBrand);
    mCAlt  = Draw::label(mClock, &poppins_medium_22, "", 20, 134, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mCBatt = Draw::label(mClock, &poppins_regular_15, "", 36, 164, 168, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mCGps  = Draw::label(mClock, &poppins_regular_15, "", 50, 184, 140, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mCDemo = Draw::label(mClock, &poppins_regular_15, "", 50, 204, 140, LV_TEXT_ALIGN_CENTER, Theme::kBrand);

    mScroll = std::make_unique<SDK::LVGL::ScrollIndicator>(mRoot, SDK::LVGL::ScrollIndicator::kSmall);
    mScroll->setCount(FACES);

    // -- Menu (over the faces) ----------------------------------------------
    mMenu = Draw::container(mRoot, 0, 0, 240, 240);
    lv_obj_set_style_bg_color(mMenu, Draw::rgb(SDK::GUI::Color::BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mMenu, LV_OPA_COVER, LV_PART_MAIN);
    mMTitle = Draw::label(mMenu, &poppins_semibold_15, "", 40, 30, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mMSel   = Draw::box(mMenu, 34, 0, 172, 36, Theme::kSelect, 18);
    for (uint8_t i = 0; i < kMenuMax; ++i) {
        mMItem[i] = Draw::label(mMenu, &poppins_medium_22, "", 34, 58 + i * 40, 172, LV_TEXT_ALIGN_CENTER, Theme::kText);
    }
    Draw::setHidden(mMenu, true);

    mButtons = std::make_unique<Buttons>(mRoot);
    showFace(LIVE);
}

DayScreen::~DayScreen()
{
    mScroll.reset();
    mSpark.reset();
    mRing.reset();
}

void DayScreen::onShow()
{
    closeMenu();
    showFace(LIVE);
}

void DayScreen::showFace(uint8_t f)
{
    mFace = f % FACES;
    Draw::setHidden(mLive, mFace != LIVE);
    Draw::setHidden(mDay, mFace != DAY);
    Draw::setHidden(mStorm, mFace != STORM);
    Draw::setHidden(mClock, mFace != CLOCK);
    mScroll->setActive(mFace);
    refresh();
}

void DayScreen::refresh()
{
    if (mMenuOpen) {
        refreshMenu();
        mButtons->set(Buttons::WHITE, Buttons::WHITE, mConfirmDiscard ? Buttons::RED : Buttons::AMBER, Buttons::WHITE);
        return;
    }
    mButtons->set(Buttons::WHITE, Buttons::WHITE, Buttons::AMBER, Buttons::NONE);
    switch (mFace) {
        case LIVE:  refreshLive();  break;
        case DAY:   refreshDay();   break;
        case STORM: refreshStorm(); break;
        case CLOCK: refreshClock(); break;
        default: break;
    }
}

void DayScreen::setBig(const char* value, const char* unit, int32_t y)
{
    // The value centred, its unit in a smaller face just after it. Values too
    // wide for the round face at 72 px (an altitude like 1,945) drop to 48.
    lv_point_t sz;
    lv_text_get_size(&sz, value, &poppins_semibold_72, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const bool small = sz.x > 150;
    if (small) {
        lv_text_get_size(&sz, value, &poppins_semibold_48, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }
    lv_obj_set_style_text_font(mLBig, small ? &poppins_semibold_48 : &poppins_semibold_72, LV_PART_MAIN);
    lv_label_set_text(mLBig, value);
    lv_obj_set_y(mLBig, small ? y + 16 : y);
    if (unit && *unit) {
        lv_label_set_text(mLUnit, unit);
        lv_obj_set_pos(mLUnit, 120 + sz.x / 2 + 4, small ? y + 46 : y + 50);
        Draw::setHidden(mLUnit, false);
    } else {
        Draw::setHidden(mLUnit, true);
    }
}

// -- Live -----------------------------------------------------------------------

void DayScreen::refreshLive()
{
    const Ski::LiveData& L   = mModel.live();
    const bool           imp = mModel.imperial();
    const bool           paused = mModel.dayState() == Ski::DayState::Paused;
    const auto           m   = static_cast<Motion>(L.motion);
    char a[32], b[24], c[24], line[64];

    const uint32_t longest = std::max<uint32_t>(L.totals.longestRunM, 300);

    if (m == Motion::Run && !paused) {
        const Ski::RunInfo& r       = L.run;
        const bool          stopped = L.stillS >= 15;
        std::snprintf(line, sizeof(line), "RUN %u%s%s", r.number, kDot, stopped ? "STOPPED" : "SKIING");
        lv_label_set_text(mLHeader, line);
        lv_obj_set_style_text_color(mLHeader, Draw::rgb(stopped ? Theme::kIdle : Theme::kRun), LV_PART_MAIN);
        lv_label_set_text(mLCaption, "");

        Fmt::vertical(b, sizeof(b), -static_cast<int32_t>(r.verticalM), imp);
        if (stopped) {
            // Standing mid-run: the drop so far is the number that matters.
            setBig(b, Fmt::verticalUnit(imp), 50);
            Fmt::speed(c, sizeof(c), r.maxSpeedCms, imp);
            std::snprintf(line, sizeof(line), "top %s %s", c, Fmt::speedUnit(imp));
        } else {
            Fmt::speed(a, sizeof(a), L.speedCms, imp);
            setBig(a, Fmt::speedUnit(imp), 50);
            std::snprintf(line, sizeof(line), "%s %s", b, Fmt::verticalUnit(imp));
        }
        lv_label_set_text(mLLine1, line);
        Fmt::distance(a, sizeof(a), r.distanceM, imp);
        Fmt::duration(c, sizeof(c), r.durationS);
        std::snprintf(line, sizeof(line), "%s %s%s%s", a, Fmt::distanceUnit(imp), kDot, c);
        lv_label_set_text(mLLine2, line);
        lv_label_set_text(mLFoot, "");

        mRing->setColor(Theme::kRun);
        mRing->setProgress(std::min<uint32_t>(1000, r.verticalM * 1000u / longest));
        return;
    }

    // Not skiing: the run just done is the most useful thing to show.
    const Ski::RunInfo& last = L.lastRun;
    if (paused) {
        lv_label_set_text(mLHeader, "PAUSED");
        lv_obj_set_style_text_color(mLHeader, Draw::rgb(Theme::kWarn), LV_PART_MAIN);
    } else if (m == Motion::Lift) {
        Fmt::duration(a, sizeof(a), L.lift.durationS);
        std::snprintf(line, sizeof(line), "ON LIFT%s%s", kDot, a);
        lv_label_set_text(mLHeader, line);
        lv_obj_set_style_text_color(mLHeader, Draw::rgb(Theme::kLift), LV_PART_MAIN);
    } else {
        lv_label_set_text(mLHeader, L.totals.runs ? "STOPPED" : "READY");
        lv_obj_set_style_text_color(mLHeader, Draw::rgb(Theme::kIdle), LV_PART_MAIN);
    }

    if (last.number) {
        std::snprintf(line, sizeof(line), "LAST RUN %u", last.number);
        lv_label_set_text(mLCaption, line);
        Fmt::vertical(a, sizeof(a), last.verticalM, imp);
        setBig(a, Fmt::verticalUnit(imp), 56);
        Fmt::speed(c, sizeof(c), last.maxSpeedCms, imp);
        std::snprintf(line, sizeof(line), "top %s %s", c, Fmt::speedUnit(imp));
        lv_label_set_text(mLLine1, line);
        Fmt::distance(b, sizeof(b), last.distanceM, imp);
        Fmt::duration(c, sizeof(c), last.durationS);
        std::snprintf(line, sizeof(line), "%s %s%s%s", b, Fmt::distanceUnit(imp), kDot, c);
        lv_label_set_text(mLLine2, line);
    } else if (m == Motion::Lift) {
        lv_label_set_text(mLCaption, "CLIMBING");
        Fmt::vertical(a, sizeof(a), L.lift.gainM, imp);
        setBig(a, Fmt::verticalUnit(imp), 56);
        lv_label_set_text(mLLine1, "First lift of the day");
        lv_label_set_text(mLLine2, "");
    } else {
        lv_label_set_text(mLCaption, "ALTITUDE");
        if (L.altValid) {
            Fmt::vertical(a, sizeof(a), L.altM, imp);
        } else {
            std::snprintf(a, sizeof(a), "--");
        }
        setBig(a, Fmt::verticalUnit(imp), 56);
        lv_label_set_text(mLLine1, "Just go ski");
        lv_label_set_text(mLLine2, "Runs count themselves");
    }

    // Foot: altitude and, on a lift, the climb rate.
    if (L.altValid) {
        Fmt::vertical(a, sizeof(a), L.altM, imp);
        if (m == Motion::Lift) {
            Fmt::verticalSpeed(b, sizeof(b), L.vsCms, imp);
            std::snprintf(line, sizeof(line), "%s %s%s%s %s", a, Fmt::verticalUnit(imp), kDot, b,
                          Fmt::verticalSpeedUnit(imp));
        } else {
            Fmt::clock(b, sizeof(b), L.todMin, mModel.clock12h());
            std::snprintf(line, sizeof(line), "%s %s%s%s", a, Fmt::verticalUnit(imp), kDot, b);
        }
        lv_label_set_text(mLFoot, line);
    } else {
        lv_label_set_text(mLFoot, "");
    }
    lv_obj_set_style_text_color(mLFoot, Draw::rgb(m == Motion::Lift ? Theme::kLift : Theme::kTextDim), LV_PART_MAIN);

    if (m == Motion::Lift && !paused) {
        mRing->setColor(Theme::kLift);
        const uint32_t ref = std::max<uint32_t>(L.lastRun.verticalM, 300);
        mRing->setProgress(std::min<uint32_t>(1000, L.lift.gainM * 1000u / ref));
    } else {
        mRing->setColor(Theme::kTrack);
        mRing->setProgress(0);
    }
}

// -- Day ------------------------------------------------------------------------

void DayScreen::refreshDay()
{
    const Ski::LiveData&    L   = mModel.live();
    const Ski::TotalsInfo&  t   = L.totals;
    const bool              imp = mModel.imperial();
    char a[24], b[24], line[64];

    std::snprintf(a, sizeof(a), "%u", t.runs);
    lv_label_set_text(mDRuns, a);
    lv_point_t sz;
    lv_text_get_size(&sz, a, &poppins_semibold_72, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_label_set_text(mDRunsU, t.runs == 1 ? "run" : "runs");
    lv_obj_set_pos(mDRunsU, 120 + sz.x / 2 + 4, 88);

    Fmt::vertical(a, sizeof(a), static_cast<int32_t>(t.verticalM), imp);
    std::snprintf(line, sizeof(line), "%s %s vertical", a, Fmt::verticalUnit(imp));
    lv_label_set_text(mDVert, line);

    Fmt::speed(a, sizeof(a), t.maxSpeedCms, imp);
    Fmt::distance(b, sizeof(b), t.distanceM, imp);
    std::snprintf(line, sizeof(line), "top %s %s%s%s %s", a, Fmt::speedUnit(imp), kDot, b, Fmt::distanceUnit(imp));
    lv_label_set_text(mDLine, line);

    // Skiing / lifts / standing, as a bar 120 px wide.
    const uint32_t total = std::max<uint32_t>(t.totalTimeS, 1);
    const uint32_t idle  = t.totalTimeS > t.runTimeS + t.liftTimeS ? t.totalTimeS - t.runTimeS - t.liftTimeS : 0;
    int32_t wRun  = static_cast<int32_t>(120u * t.runTimeS / total);
    int32_t wLift = static_cast<int32_t>(120u * t.liftTimeS / total);
    int32_t wIdle = 120 - wRun - wLift;
    const int32_t x0 = 60;
    lv_obj_set_pos(mDBarRun, x0, 174);
    lv_obj_set_width(mDBarRun, std::max<int32_t>(wRun, 1));
    lv_obj_set_pos(mDBarLift, x0 + wRun, 174);
    lv_obj_set_width(mDBarLift, std::max<int32_t>(wLift, 1));
    lv_obj_set_pos(mDBarIdle, x0 + wRun + wLift, 174);
    lv_obj_set_width(mDBarIdle, std::max<int32_t>(wIdle, 1));
    Draw::setHidden(mDBarRun, wRun <= 0);
    Draw::setHidden(mDBarLift, wLift <= 0);
    Draw::setHidden(mDBarIdle, wIdle <= 0);
    (void)idle;

    Fmt::hoursMinutes(a, sizeof(a), t.runTimeS);
    Fmt::hoursMinutes(b, sizeof(b), t.liftTimeS);
    std::snprintf(line, sizeof(line), "ski %s%slift %s", a, kDot, b);
    lv_label_set_text(mDSplit, line);
}

// -- Storm ----------------------------------------------------------------------

void DayScreen::refreshStorm()
{
    const Ski::StormInfo& s   = mModel.live().storm;
    const bool            imp = mModel.imperial();
    const auto            lvl = static_cast<Ski::StormWatch::Level>(s.level);
    char a[24], line[64];

    const uint32_t col = stormColor(lvl);
    lv_obj_set_style_text_color(mSHeader, Draw::rgb(lvl >= Ski::StormWatch::Level::Falling ? Theme::kWarn : Theme::kTextDim),
                                LV_PART_MAIN);

    if (lvl == Ski::StormWatch::Level::Learning) {
        lv_label_set_text(mSRate, "--");
        // Short enough for the round face (the trend needs readings at one spot
        // spanning two hours; see StormWatch).
        lv_label_set_text(mSInfo, s.readings ? "Needs 2 h at one spot" : "Reads it when you stop");
    } else {
        Fmt::pressureRate(a, sizeof(a), s.rateCenti, imp);
        lv_label_set_text(mSRate, a);
        std::snprintf(line, sizeof(line), "%u readings%s%u spot%s", s.readings, kDot, s.spots, s.spots == 1 ? "" : "s");
        lv_label_set_text(mSInfo, line);
    }
    std::snprintf(line, sizeof(line), "%s\n/ 3 h", Fmt::pressureUnit(imp));
    lv_label_set_text(mSUnit, line);
    lv_obj_set_style_text_color(mSRate, Draw::rgb(col), LV_PART_MAIN);

    lv_label_set_text(mSStatus, stormText(lvl));
    lv_obj_set_style_text_color(mSStatus, Draw::rgb(col), LV_PART_MAIN);

    mSpark->set(s.spark, s.sparkCount, lvl == Ski::StormWatch::Level::Learning ? Theme::kTextDim : col);

    if (s.pressureDeci) {
        Fmt::pressure(a, sizeof(a), s.pressureDeci, imp);
        if (s.thresholdHpa) {
            std::snprintf(line, sizeof(line), "%s %s%salert %u", a, Fmt::pressureUnit(imp), kDot, s.thresholdHpa);
        } else {
            std::snprintf(line, sizeof(line), "%s %s%salert off", a, Fmt::pressureUnit(imp), kDot);
        }
        lv_label_set_text(mSPress, line);
    } else {
        lv_label_set_text(mSPress, "");
    }
}

// -- Clock ----------------------------------------------------------------------

void DayScreen::refreshClock()
{
    const Ski::LiveData& L   = mModel.live();
    const bool           imp = mModel.imperial();
    char a[24], line[64];

    Fmt::clock(a, sizeof(a), L.todMin, mModel.clock12h());
    lv_point_t sz;
    lv_text_get_size(&sz, a, &poppins_semibold_72, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const bool small = sz.x > 180;
    lv_obj_set_style_text_font(mCTime, small ? &poppins_semibold_48 : &poppins_semibold_72, LV_PART_MAIN);
    lv_obj_set_y(mCTime, small ? 66 : 50);
    lv_label_set_text(mCTime, a);
    lv_label_set_text(mCDate, mModel.settings().sport == static_cast<uint8_t>(Ski::Sport::Snowboard) ? "SNOWBOARD DAY"
                                                                                                      : "SKI DAY");
    if (L.altValid) {
        Fmt::vertical(a, sizeof(a), L.altM, imp);
        std::snprintf(line, sizeof(line), "%s %s", a, Fmt::verticalUnit(imp));
    } else {
        std::snprintf(line, sizeof(line), "--");
    }
    lv_label_set_text(mCAlt, line);
    if (L.heartRate) {
        std::snprintf(line, sizeof(line), "Battery %u%%%s%u bpm", L.battery, kDot, L.heartRate);
    } else {
        std::snprintf(line, sizeof(line), "Battery %u%%", L.battery);
    }
    lv_label_set_text(mCBatt, line);
    lv_obj_set_style_text_color(mCBatt, Draw::rgb(L.battery && L.battery <= 20 ? Theme::kWarn : Theme::kTextDim),
                                LV_PART_MAIN);
    lv_label_set_text(mCGps, L.gpsFix ? "GPS ok" : "No GPS (still counting)");
    lv_obj_set_style_text_color(mCGps, Draw::rgb(L.gpsFix ? Theme::kGood : Theme::kWarn), LV_PART_MAIN);

    if (mModel.demo()) {
        static const char* const kSpeed[] = { "", "1x", "10x", "60x" };
        const uint8_t sp = static_cast<uint8_t>(mModel.demoSpeed());
        std::snprintf(line, sizeof(line), "DEMO %s%s%u%%", kSpeed[sp < 4 ? sp : 0], kDot, L.demoPermille / 10u);
        lv_label_set_text(mCDemo, line);
    } else {
        lv_label_set_text(mCDemo, "");
    }
}

// -- Menu -----------------------------------------------------------------------

void DayScreen::openMenu()
{
    mMenuOpen       = true;
    mConfirmDiscard = false;
    mSel            = 0;
    Draw::setHidden(mMenu, false);
    refresh();
}

void DayScreen::closeMenu()
{
    mMenuOpen       = false;
    mConfirmDiscard = false;
    Draw::setHidden(mMenu, true);
    refresh();
}

void DayScreen::refreshMenu()
{
    const bool paused = mModel.dayState() == Ski::DayState::Paused;
    mItemCount = 0;
    mItems[mItemCount++] = Item::PauseResume;
    mItems[mItemCount++] = Item::End;
    mItems[mItemCount++] = Item::Discard;
    if (mModel.demo()) {
        mItems[mItemCount++] = Item::Speed;
    }
    if (mSel >= mItemCount) {
        mSel = 0;
    }

    lv_label_set_text(mMTitle, paused ? "PAUSED" : (mModel.demo() ? "DEMO DAY" : "SKI DAY"));
    lv_obj_set_style_text_color(mMTitle, Draw::rgb(paused ? Theme::kWarn : Theme::kTextDim), LV_PART_MAIN);

    for (uint8_t i = 0; i < kMenuMax; ++i) {
        if (i >= mItemCount) {
            Draw::setHidden(mMItem[i], true);
            continue;
        }
        Draw::setHidden(mMItem[i], false);
        const char* text  = "";
        uint32_t    color = Theme::kText;
        char        buf[24];
        switch (mItems[i]) {
            case Item::PauseResume: text = paused ? "Resume" : "Pause"; color = paused ? Theme::kGood : Theme::kText; break;
            case Item::End:         text = "End day"; break;
            case Item::Discard:
                text  = (mConfirmDiscard && i == mSel) ? "Discard? R1" : "Discard";
                color = Theme::kDanger;
                break;
            case Item::Speed: {
                static const char* const kSpeed[] = { "", "1x", "10x", "60x" };
                const uint8_t sp = static_cast<uint8_t>(mModel.demoSpeed());
                std::snprintf(buf, sizeof(buf), "Speed %s", kSpeed[sp < 4 ? sp : 0]);
                text  = buf;
                color = Theme::kBrand;
            } break;
        }
        lv_label_set_text(mMItem[i], text);
        lv_obj_set_style_text_color(mMItem[i], Draw::rgb(color), LV_PART_MAIN);
    }
    lv_obj_set_y(mMSel, 58 + mSel * 40 - 4);
}

void DayScreen::menuClick(uint8_t code)
{
    namespace Btn = SDK::GUI::Button;
    using CustomMessage::Control;
    switch (code) {
        case Btn::L1:
            mSel            = static_cast<uint8_t>((mSel + mItemCount - 1) % mItemCount);
            mConfirmDiscard = false;
            refresh();
            break;
        case Btn::L2:
            mSel            = static_cast<uint8_t>((mSel + 1) % mItemCount);
            mConfirmDiscard = false;
            refresh();
            break;
        case Btn::R2:
            closeMenu();
            break;
        case Btn::R1:
            switch (mItems[mSel]) {
                case Item::PauseResume:
                    mModel.control(mModel.dayState() == Ski::DayState::Paused ? Control::RESUME : Control::PAUSE);
                    closeMenu();
                    break;
                case Item::End:
                    closeMenu();
                    mModel.control(Control::END_SAVE);
                    break;
                case Item::Discard:
                    if (mConfirmDiscard) {
                        closeMenu();
                        mModel.control(Control::DISCARD);
                    } else {
                        mConfirmDiscard = true;   // one stray press never throws a day away
                        refresh();
                    }
                    break;
                case Item::Speed: {
                    uint8_t sp = static_cast<uint8_t>(mModel.demoSpeed()) + 1u;
                    if (sp >= static_cast<uint8_t>(Ski::DemoSpeed::COUNT)) {
                        sp = static_cast<uint8_t>(Ski::DemoSpeed::X1);
                    }
                    mModel.setDemoSpeed(static_cast<Ski::DemoSpeed>(sp));
                    refresh();
                } break;
            }
            break;
        default:
            break;
    }
}

void DayScreen::onClick(uint8_t code)
{
    namespace Btn = SDK::GUI::Button;
    if (mMenuOpen) {
        menuClick(code);
        return;
    }
    switch (code) {
        case Btn::L1: showFace(static_cast<uint8_t>((mFace + FACES - 1) % FACES)); break;
        case Btn::L2: showFace(static_cast<uint8_t>((mFace + 1) % FACES)); break;
        case Btn::R1: openMenu(); break;
        case Btn::R2: showFace(LIVE); break;
        default: break;
    }
}
