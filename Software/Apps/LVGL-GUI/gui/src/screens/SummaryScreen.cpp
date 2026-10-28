/**
 ******************************************************************************
 * @file    SummaryScreen.cpp
 ******************************************************************************
 */

#include "gui/screens/SummaryScreen.hpp"

#include <algorithm>
#include <cstdio>

#include "SDK/GUI/Button.hpp"
#include "SDK/GUI/LVGL/Draw.hpp"

#include "Ski/Format.hpp"
#include "gui/Assets.hpp"
#include "gui/Theme.hpp"

namespace Draw = SDK::LVGL::Draw;
namespace Fmt  = Ski::Fmt;
using SDK::LVGL::Buttons;

namespace
{
const char* const kDot = " \xC2\xB7 ";
// Rows narrow towards the top and bottom of the round face.
constexpr int32_t kRowY[5] = { 52, 80, 108, 136, 164 };
constexpr int32_t kRowX[5] = { 50, 32, 26, 32, 50 };
} // namespace

SummaryScreen::SummaryScreen(Model& model)
    : Screen(model)
{
    // -- Overview -----------------------------------------------------------
    mOverview = Draw::container(mRoot, 0, 0, 240, 240);
    mOHeader  = Draw::label(mOverview, &poppins_semibold_15, "", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kGood);
    mORuns    = Draw::label(mOverview, &poppins_semibold_72, "", 20, 38, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mORunsU   = Draw::label(mOverview, &poppins_medium_18, "runs", 0, 0, 60, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);
    mOVert    = Draw::label(mOverview, &poppins_medium_22, "", 20, 120, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mOLine    = Draw::label(mOverview, &poppins_regular_15, "", 30, 148, 180, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mOBar[0]  = Draw::box(mOverview, 60, 174, 40, 8, Theme::kRun, 3);
    mOBar[1]  = Draw::box(mOverview, 100, 174, 40, 8, Theme::kLift, 3);
    mOBar[2]  = Draw::box(mOverview, 140, 174, 40, 8, Theme::kTrack, 3);
    mOSplit   = Draw::label(mOverview, &poppins_regular_15, "", 40, 188, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mOHours   = Draw::label(mOverview, &poppins_regular_15, "", 45, 206, 150, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);

    // -- Profile ------------------------------------------------------------
    mProfile = Draw::container(mRoot, 0, 0, 240, 240);
    mPHeader = Draw::label(mProfile, &poppins_semibold_15, "YOUR DAY", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mChart   = std::make_unique<ProfileChart>(mProfile, 36, 56, 168, 80);
    mPLine   = Draw::label(mProfile, &poppins_medium_18, "", 20, 146, 200, LV_TEXT_ALIGN_CENTER, Theme::kText);
    mPRange  = Draw::label(mProfile, &poppins_regular_15, "", 30, 170, 180, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mPKeyRun   = Draw::box(mProfile, 70, 198, 14, 4, Theme::kRun, 2);
    mPKeyRunT  = Draw::label(mProfile, &poppins_regular_15, "runs", 88, 189, 50, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);
    mPKeyLift  = Draw::box(mProfile, 128, 198, 14, 4, Theme::kLift, 2);
    mPKeyLiftT = Draw::label(mProfile, &poppins_regular_15, "lifts", 146, 189, 50, LV_TEXT_ALIGN_LEFT, Theme::kTextDim);

    // -- Runs ---------------------------------------------------------------
    mRuns    = Draw::container(mRoot, 0, 0, 240, 240);
    mRHeader = Draw::label(mRuns, &poppins_semibold_15, "RUNS", 40, 26, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);
    mRSel    = Draw::box(mRuns, 22, kRowY[2] - 3, 196, 30, Theme::kSelect, 15);
    for (uint8_t i = 0; i < kRows; ++i) {
        mRRow[i] = Draw::label(mRuns, i == 2 ? &poppins_medium_22 : &poppins_medium_18, "", kRowX[i], kRowY[i] + (i == 2 ? 0 : 2),
                               240 - 2 * kRowX[i], LV_TEXT_ALIGN_CENTER, Theme::kText);
    }
    mRDetail = Draw::label(mRuns, &poppins_regular_15, "", 40, 194, 160, LV_TEXT_ALIGN_CENTER, Theme::kTextDim);

    mScroll = std::make_unique<SDK::LVGL::ScrollIndicator>(mRoot, SDK::LVGL::ScrollIndicator::kSmall);
    mScroll->setCount(PAGES);

    mButtons = std::make_unique<Buttons>(mRoot);
    mButtons->set(Buttons::WHITE, Buttons::WHITE, Buttons::GREEN, Buttons::WHITE);
    showPage(OVERVIEW);
}

SummaryScreen::~SummaryScreen()
{
    mScroll.reset();
    mChart.reset();
}

void SummaryScreen::onShow()
{
    mSel = 0;
    showPage(OVERVIEW);
}

void SummaryScreen::showPage(uint8_t p)
{
    mPage = p % PAGES;
    Draw::setHidden(mOverview, mPage != OVERVIEW);
    Draw::setHidden(mProfile, mPage != PROFILE);
    Draw::setHidden(mRuns, mPage != RUNS);
    mScroll->setActive(mPage);
    refresh();
}

void SummaryScreen::refresh()
{
    switch (mPage) {
        case OVERVIEW: refreshOverview(); break;
        case PROFILE:  refreshProfile();  break;
        case RUNS:     refreshRuns();     break;
        default: break;
    }
}

void SummaryScreen::refreshOverview()
{
    const Ski::DayLog&     log = mModel.log();
    const Ski::TotalsInfo& t   = log.totals;
    const bool             imp = mModel.imperial();
    char a[24], b[24], line[64];

    if (log.demo) {
        lv_label_set_text(mOHeader, log.saved ? "DEMO DAY SAVED" : "DEMO DAY");
        lv_obj_set_style_text_color(mOHeader, Draw::rgb(Theme::kBrand), LV_PART_MAIN);
    } else {
        lv_label_set_text(mOHeader, log.saved ? "DAY SAVED" : "NOT SAVED");
        lv_obj_set_style_text_color(mOHeader, Draw::rgb(log.saved ? Theme::kGood : Theme::kWarn), LV_PART_MAIN);
    }

    std::snprintf(a, sizeof(a), "%u", t.runs);
    lv_label_set_text(mORuns, a);
    lv_point_t sz;
    lv_text_get_size(&sz, a, &poppins_semibold_72, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_label_set_text(mORunsU, t.runs == 1 ? "run" : "runs");
    lv_obj_set_pos(mORunsU, 120 + sz.x / 2 + 4, 88);

    Fmt::vertical(a, sizeof(a), static_cast<int32_t>(t.verticalM), imp);
    std::snprintf(line, sizeof(line), "%s %s vertical", a, Fmt::verticalUnit(imp));
    lv_label_set_text(mOVert, line);

    Fmt::speed(a, sizeof(a), t.maxSpeedCms, imp);
    Fmt::distance(b, sizeof(b), t.distanceM, imp);
    std::snprintf(line, sizeof(line), "top %s %s%s%s %s", a, Fmt::speedUnit(imp), kDot, b, Fmt::distanceUnit(imp));
    lv_label_set_text(mOLine, line);

    const uint32_t total = std::max<uint32_t>(t.totalTimeS, 1);
    const int32_t  wRun  = static_cast<int32_t>(120u * t.runTimeS / total);
    const int32_t  wLift = static_cast<int32_t>(120u * t.liftTimeS / total);
    const int32_t  w[3]  = { wRun, wLift, 120 - wRun - wLift };
    int32_t x = 60;
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_pos(mOBar[i], x, 174);
        lv_obj_set_width(mOBar[i], std::max<int32_t>(w[i], 1));
        Draw::setHidden(mOBar[i], w[i] <= 0);
        x += w[i];
    }
    Fmt::hoursMinutes(a, sizeof(a), t.runTimeS);
    Fmt::hoursMinutes(b, sizeof(b), t.liftTimeS);
    std::snprintf(line, sizeof(line), "ski %s%slift %s", a, kDot, b);
    lv_label_set_text(mOSplit, line);

    Fmt::clock(a, sizeof(a), log.startMin, mModel.clock12h());
    Fmt::clock(b, sizeof(b), log.endMin, mModel.clock12h());
    if (log.hrAvg) {
        // Too narrow here for the hours as well; each run in the list has its time.
        std::snprintf(line, sizeof(line), "HR %u%smax %u", log.hrAvg, kDot, log.hrMax);
    } else {
        std::snprintf(line, sizeof(line), "%s - %s", a, b);
    }
    lv_label_set_text(mOHours, line);
}

void SummaryScreen::refreshProfile()
{
    const Ski::DayLog& log = mModel.log();
    const bool         imp = mModel.imperial();
    char a[24], b[24], line[64];

    mChart->set(log.profile);

    Fmt::vertical(a, sizeof(a), static_cast<int32_t>(log.totals.verticalM), imp);
    std::snprintf(line, sizeof(line), "%u runs%s%s %s", log.totals.runs, kDot, a, Fmt::verticalUnit(imp));
    lv_label_set_text(mPLine, line);

    int16_t lo = 0, hi = 0;
    log.profile.range(lo, hi);
    if (log.profile.count()) {
        Fmt::vertical(a, sizeof(a), lo, imp);
        Fmt::vertical(b, sizeof(b), hi, imp);
        std::snprintf(line, sizeof(line), "%s - %s %s", a, b, Fmt::verticalUnit(imp));
    } else {
        std::snprintf(line, sizeof(line), "No altitude recorded");
    }
    lv_label_set_text(mPRange, line);
}

void SummaryScreen::refreshRuns()
{
    const Ski::DayLog& log = mModel.log();
    const bool         imp = mModel.imperial();
    char a[24], b[24], line[96];

    if (log.count == 0) {
        for (uint8_t i = 0; i < kRows; ++i) {
            lv_label_set_text(mRRow[i], i == 2 ? "No runs" : "");
        }
        lv_label_set_text(mRDetail, "");
        return;
    }
    if (mSel >= log.count) {
        mSel = static_cast<uint8_t>(log.count - 1);
    }

    // The day's best, for the marks.
    uint16_t bestSpeed = 0, bestVert = 0;
    for (uint8_t i = 0; i < log.count; ++i) {
        bestSpeed = std::max(bestSpeed, log.runs[i].maxSpeedCms);
        bestVert  = std::max(bestVert, log.runs[i].verticalM);
    }

    for (uint8_t row = 0; row < kRows; ++row) {
        const int idx = static_cast<int>(mSel) + row - 2;
        if (idx < 0 || idx >= log.count) {
            lv_label_set_text(mRRow[row], "");
            continue;
        }
        const Ski::RunInfo& r = log.runs[idx];
        Fmt::vertical(a, sizeof(a), r.verticalM, imp);
        Fmt::speed(b, sizeof(b), r.maxSpeedCms, imp);
        std::snprintf(line, sizeof(line), "%u   %s   %s", r.number, a, b);   // units are in the header
        lv_label_set_text(mRRow[row], line);
        const bool best = r.maxSpeedCms == bestSpeed && bestSpeed > 0;
        lv_obj_set_style_text_color(mRRow[row], Draw::rgb(best ? Theme::kBest : (row == 2 ? Theme::kText : Theme::kTextDim)),
                                    LV_PART_MAIN);
    }

    const Ski::RunInfo& s = log.runs[mSel];
    char c[16], d[16];
    Fmt::clock(a, sizeof(a), s.startMin, mModel.clock12h());
    Fmt::distance(b, sizeof(b), s.distanceM, imp);
    Fmt::duration(c, sizeof(c), s.durationS);
    Fmt::speed(d, sizeof(d), s.avgSpeedCms, imp);
    std::snprintf(line, sizeof(line), "%s%s%s %s%s%s", a, kDot, b, Fmt::distanceUnit(imp), kDot, c);
    if (s.verticalM == bestVert && s.maxSpeedCms == bestSpeed) {
        std::snprintf(line, sizeof(line), "Biggest and fastest run");
    } else if (s.maxSpeedCms == bestSpeed) {
        std::snprintf(line, sizeof(line), "Fastest run%s%s", kDot, c);
    } else if (s.verticalM == bestVert) {
        std::snprintf(line, sizeof(line), "Biggest run%s%s", kDot, c);
    }
    lv_label_set_text(mRDetail, line);
    std::snprintf(line, sizeof(line), "RUN%s%s%s%s", kDot, imp ? "FT" : "M", kDot, imp ? "MPH" : "KM/H");
    lv_label_set_text(mRHeader, line);
    (void)d;
}

void SummaryScreen::onClick(uint8_t code)
{
    namespace Btn = SDK::GUI::Button;
    const uint8_t n = mModel.log().count;
    switch (code) {
        case Btn::L1:
            if (mPage == RUNS && mSel > 0) {
                --mSel;
                refresh();
            } else {
                showPage(static_cast<uint8_t>((mPage + PAGES - 1) % PAGES));
            }
            break;
        case Btn::L2:
            if (mPage == RUNS && n > 0 && mSel + 1 < n) {
                ++mSel;
                refresh();
            } else {
                showPage(static_cast<uint8_t>((mPage + 1) % PAGES));
            }
            break;
        case Btn::R1:
        case Btn::R2:
            mModel.control(CustomMessage::Control::HOME);
            break;
        default:
            break;
    }
}
