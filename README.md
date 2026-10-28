# Ski Day: a ski and snowboard tracker for UNA Watch

![Ski Day on UNA Watch](Docs/screenshots/tour.png)

**Ski Day** counts your runs by itself and tells you about each one while you
ride the next lift. It warns you when a storm is coming, and records the day
as a FIT file. Everything works with the four buttons, in gloves.

Entry for the [UNA App Development Competition – October 2026](https://unawatch.com/blogs/news/una-apps-competition-october-2026).

---

**Demo video (90 s):** [`Docs/SkiDay_demo.mp4`](Docs/SkiDay_demo.mp4), a whole
simulated day in the simulator.

**Ready-made build:** [`Release/SkiDay_1.0.0.uapp`](Release/SkiDay_1.0.0.uapp)
(for sideloading by USB) and [`Release/SkiDay_1.0.0_store.zip`](Release/SkiDay_1.0.0_store.zip)
(the community-store upload package).

> **Status:** works in the UNA simulator and passes 49 host unit tests, but has
> **not yet been used on snow or on a UNA watch** (it is October). It has been
> tested with the demo day below and with six real resort days recorded on
> another watch: it counted the same number of runs as that watch on every
> day, top speed within 1 mph, distance within 3.5 % and vertical within 5 %
> (a little lower: it counts each run top to bottom, not every bit of descent).

## What it does

- **Runs count themselves.** The barometer tells lift rides from runs, so it
  works in a gondola with no GPS. A fall, or waiting a few minutes for a
  friend, does not split a run. A bump or a short uphill skate does not end one.
- **Every run, measured:** vertical drop, distance, top speed, average speed
  (stops left out) and time. Queues count as neither run nor lift time.
- **The recap comes when you look.** A run is finished when the next lift is
  confirmed, a few seconds after you sit down. The watch buzzes, shows the run,
  and keeps it on the lift face for the rest of the ride.
- **The day:** runs, total vertical, top speed, distance skied, and time
  skiing vs on lifts vs stopped. At the end: an altitude profile of the day and
  a list of every run. Season totals carry over between days.
- **Storm watch** buzzes when the air pressure falls fast (3 hPa in 3 h by
  default; 2–4 hPa or off). See [how it avoids false alarms](#storm-watch).
- **Imperial units** (mph, ft, mi, inHg, 12-hour clock) when the watch is set to
  imperial; metric otherwise.
- **FIT file** with the sport set to alpine skiing or snowboarding, a lap per
  run (active) and per lift ride (rest), GPS track, altitude and speed.
- **Demo day:** a scripted day at a made-up resort, played through the real
  detection code at 1×, 10× or 60×. It is in Settings, so you can see the app
  at work in the simulator, or on a watch in October.

## Using it

**Home**

| Button | Action |
|---|---|
| L1 | Ski ↔ Snowboard (the sport written to the FIT file) |
| L2 | Settings |
| R1 | **Start** the day (or the demo day, if it is switched on) |
| R2 | Exit |

**During the day:** L1/L2 step through the faces, R2 jumps back to the live
face, R1 opens the menu (Pause/Resume, End day, Discard). Any button dismisses
a banner or the storm warning.

| Face | Shows |
|---|---|
| Live | **Skiing:** speed, drop so far, distance and time of this run. **On a lift:** the run you just did, plus altitude and climb rate. **Stopped:** the last run |
| Day | Runs, vertical, top speed, distance, and a ski / lift / stopped bar |
| Storm watch | Pressure trend in hPa per 3 h with a sparkline, Rising / Steady / Falling / Storm coming |
| Clock | Time, altitude, battery, GPS |

**Summary:** totals, the day's profile (runs white, lifts blue) and the runs
list (L1/L2 scroll; the best run is starred). R1 or R2 goes home.

![Settings and menu](Docs/screenshots/settings.png)

### Settings

| Setting | What it does |
|---|---|
| Sport | Ski or Snowboard |
| Storm alert | 2, 3 or 4 hPa fall in 3 h, or Off. The storm face still shows the trend when off |
| Run alerts | Buzz and banner at the end of each run |
| Sound | Beep + buzz, Buzz only, or Beep only |
| Demo day | Off, 1×, 10× or 60× |

### How the alerts feel

| Event | Beep | Buzz | Backlight |
|---|---|---|---|
| Day started | low-high double tone | strong click | 3 s |
| Lift confirmed | — | soft bump | — |
| Run finished | one tone | double click | 6 s |
| Run with a new top speed or biggest vertical | rising three notes | long double click | 8 s |
| **Storm coming** | three falling tones | three long buzzes | 15 s |
| GPS fix | short tick | sharp click | — |
| Battery low | two low tones | double click | 5 s |
| Pause / resume | low / high blip | soft bump / click | — |
| Day ended | rising fanfare | strong buzz | 8 s |

Nothing buzzes while you are skiing.

![Summary: totals, profile, runs](Docs/screenshots/summary.png)

![Imperial units](Docs/screenshots/imperial.png)

## How it works

The logic is plain C++ in `Software/Libs/*/Ski/`, with no SDK, sensor or GUI
dependency, so it is unit-tested on a PC and run unchanged by the watch, the
simulator and the demo day.

### Lifts and runs (`RunTracker`)

Vertical speed is the least-squares slope of the barometric altitude over the
last 20 s. Climbing faster than 0.3 m/s is a lift; dropping faster than
0.25 m/s while GPS shows you moving is skiing. A run is confirmed once you are
25 m below the high point, a lift once you have climbed 15 m. Both are then
dated back: a run starts when you pushed off the top and ends when you reached
the bottom; a lift starts when you boarded. A run ends at the next lift, when
you walk 12 m back up, or after 15 minutes without skiing (lunch; a shorter
break at a hut stays in the run). Two lift
rides with no run between are merged (a gondola that stopped in the wind, or
two chained lifts). Top speed is filtered so one bad GPS fix cannot set it.

### Storm watch

Air pressure falls about 1.2 hPa for every 10 m you climb, so a 3 hPa drop in
the weather looks just like riding 25 m up a lift. Pressure readings can only
be compared at the same height, and the height cannot come from the barometer
(that is what is being read). GPS position is used instead:

1. A reading is taken only after you have stood still for 90 s (the median of
   the last minute, so a gust or a slammed door does not count).
2. Readings within 20 m of a spot's average position belong to that spot.
   A ski day supplies these for free: you queue at the same lift base again
   and again.
3. The trend at a spot is the least-squares slope of its readings over the
   last 3 hours, once there are three spanning at least 2 hours. If they
   scatter about that line by more than 0.45 hPa (4 m of height), they were
   not taken at one height (stops spread along a slope) and are ignored.
   The rate is scaled to sea level, where the 3 hPa rule comes from.

The alert fires when the fall reaches the threshold and repeats at most hourly
while it lasts. A test plays 20 days of stops scattered along a 40 % slope in
steady weather and expects no alarm.

### The demo day

`SkiDaySim` plays a day at a made-up resort (a gondola, a chairlift and a
T-bar; red, blue and nursery runs and a valley run home) with a sensor model: GPS noise, a 20 s first fix, no GPS
in the gondola cabin, barometer noise, and pressure that starts falling after
lunch. The demo feeds the service exactly where the real sensors would.

## Project layout

```
Software/
  Libs/                         service process + shared logic (no GUI)
    Header/Ski/RunTracker.hpp   lifts and runs (SDK-free, unit-tested)
    Header/Ski/StormWatch.hpp   storm warning (SDK-free, unit-tested)
    Header/Ski/SkiDaySim.hpp    the scripted demo day
    Sources/Service.cpp         sensors, day state, cues, settings, season totals
    Sources/ActivityWriter.cpp  FIT recording
    Header/Commands.hpp         service <-> GUI messages
  Apps/
    SkiDay-CMake/               watch build -> .uapp
    LVGL-GUI/                   GUI process (LVGL): home, settings, day, summary
      simulator/                PC simulator (SDL2), with audible beeps
Tests/                          host unit tests
Tools/                          packaging, icons, simulator tour, day replay
Resources/                      launcher and store icons
```

## Building

Requirements: the [UNA SDK](https://github.com/UNAWatch/una-sdk) with its LVGL
submodule, CMake ≥ 3.21, an ARM GCC toolchain (the distro `gcc-arm-none-eabi`
works) and Python 3.

```bash
git clone https://github.com/UNAWatch/una-sdk.git
git -C una-sdk submodule update --init ThirdParty/lvgl
export UNA_SDK=$PWD/una-sdk
python3 -m pip install -r $UNA_SDK/Utilities/Scripts/app_packer/requirements.txt

# Watch app (.uapp in build/watch/ and Output/)
cmake -G "Unix Makefiles" -S Software/Apps/SkiDay-CMake -B build/watch
cmake --build build/watch
```

To install by USB: copy `SkiDay_<version>.uapp` into `Apps/SkiDay/` on the
watch drive, eject it, power-cycle the watch, then find **Ski Day** in the
launcher.

### Simulator (Linux / WSL / Windows)

```bash
sudo apt-get install build-essential cmake ninja-build libsdl2-dev   # Debian/Ubuntu
cd Software/Apps/LVGL-GUI/simulator
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
(cd build/bin && ./SkiDaySimulator)
```

Keys **1 2 3 4** = L1 L2 R1 R2, **Esc** quits. To watch a day: L2 (Settings),
L1 to *Demo day*, R1 to pick a speed, R2, then R1 to start.

### Tests and tools

```bash
cmake -S Tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
```

- `Tools/sim_tour.sh OUTDIR [--video]` plays the whole demo day in the
  simulator, saves a screenshot of every screen (and optionally a video), and
  copies out the day's FIT file. `UNA_SIM_IMPERIAL=1` shows it in imperial.
- `Tools/make_video.py OUTDIR` cuts that recording into the 90 s demo video.
- `Tools/replay_day.cpp` replays a simulated day (seed, speed, extra stops,
  GPS loss, storm on/off) through the tracker and prints it next to the truth.
- `Tools/roundify.py` masks screenshots to the round face.

## Publishing (competition entry)

1. Sign up at [apps.unawatch.com](https://apps.unawatch.com), click **Add New**,
   name it *Ski Day* and generate an **App ID**.
2. Put that ID in `APP_ID` in `Software/Apps/SkiDay-CMake/CMakeLists.txt`
   (it now holds the store App ID, `DC480768426D7784`).
3. Set `BUILD_VERSION` in the same file and run `UNA_SDK=... Tools/package.sh`.
   This builds and writes `Release/SkiDay_<version>_store.zip` (`.uapp`,
   `app-manifest.json` and icon; the store accepts nothing else in the zip or the
   manifest). Paste the description and add the screenshots from
   `Docs/screenshots/store_*.png` on the app's page in the portal.
4. On the app page: **Version → Upload New** → upload the zip → **Release** →
   **Confirm and Publish**.
5. Submit the entry through the competition's [Google Form](https://forms.gle/MTWZYWsVgB6UvrxA7)
   before **30 October 2026, 4 pm UK time**.

## Safety

Storm watch is a hint from one barometer, not a forecast. Check the resort's
weather and avalanche bulletins, and follow the ski patrol.

## License

MIT. See [LICENSE](LICENSE).
