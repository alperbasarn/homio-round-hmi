// test_environment_model (MDL-21): the 0.5 C hysteresis (0.4 suppressed, 0.6 admitted), the
// first write admitted even at the old default (a true 18.0 C is a reading), sentinel and NaN
// rejection, civil time from an epoch, the minute filter, legacy string truncation, band
// continuity, and zero allocations over 10k updates.
#include <cmath>
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/EnvironmentModel.h"

using namespace model;

namespace {

void countNotify(const EnvironmentState&, void* context) { ++*static_cast<int*>(context); }

bool text(const char* actual, const char* expected) { return std::strcmp(actual, expected) == 0; }

constexpr int64_t kFeb7_2026_1234Utc = 1770467640;  // 2026-02-07 12:34:00 UTC, a Saturday

}  // namespace

TEST_CASE(first_write_is_admitted_even_when_it_equals_the_old_default) {
    EnvironmentModel e;
    CHECK(!e.state().indoorValid);
    CHECK(e.state().indoorClass.band == IndoorBand::Invalid);
    CHECK(e.setIndoorTemp(18.0f));               // used to be discarded as "unchanged"
    CHECK(e.state().indoorValid);
    CHECK(e.state().indoorClass.band == IndoorBand::Cold);
    CHECK(!e.state().outdoorValid);
    CHECK(e.setOutdoorTemp(0.0f));
    CHECK(e.state().outdoorValid);
    CHECK(e.state().outdoorClass.band == OutdoorBand::Freezing);
}

TEST_CASE(hysteresis_suppresses_0_4_and_admits_0_6) {
    EnvironmentModel e;
    CHECK(e.setIndoorTemp(20.0f));
    CHECK(!e.setIndoorTemp(20.4f));
    CHECK_EQ(e.state().indoorTemp, 20.0f);
    CHECK(!e.setIndoorTemp(20.5f));              // exactly the threshold: suppressed, as before
    CHECK(e.setIndoorTemp(20.6f));
    CHECK_EQ(e.state().indoorTemp, 20.6f);
    CHECK(e.setOutdoorTemp(5.0f));
    CHECK(!e.setOutdoorTemp(4.6f));
    CHECK(e.setOutdoorTemp(4.4f));
}

TEST_CASE(sentinel_nan_and_impossible_values_are_rejected) {
    EnvironmentModel e;
    CHECK(!e.setIndoorTemp(kNoTemperature));
    CHECK(!e.setIndoorTemp(std::nanf("")));
    CHECK(!e.setIndoorTemp(-300.0f));
    CHECK(!e.setOutdoorTemp(kNoTemperature));
    CHECK(!e.state().indoorValid);
    CHECK(!e.state().outdoorValid);
}

TEST_CASE(civil_time_from_an_epoch_and_an_offset) {
    EnvironmentModel e;
    CHECK(text(e.state().date.c_str(), "----/--/--"));
    CHECK(text(e.state().formatted.c_str(), "-- ---"));
    CHECK(e.setClock(kFeb7_2026_1234Utc, 180));   // UTC+3
    CHECK(e.state().clockValid);
    CHECK(text(e.state().date.c_str(), "2026/02/07"));
    CHECK(text(e.state().time.c_str(), "15:34"));
    CHECK(text(e.state().day.c_str(), "SAT"));
    CHECK(text(e.state().formatted.c_str(), "7 FEB"));
    CHECK(!e.setClock(kFeb7_2026_1234Utc + 30, 180));   // same minute: no change
    CHECK(e.setClock(kFeb7_2026_1234Utc + 60, 180));
    CHECK(text(e.state().time.c_str(), "15:35"));
    CHECK(e.setClock(kFeb7_2026_1234Utc + 60, 0));      // offset change
    CHECK(text(e.state().time.c_str(), "12:35"));
    CHECK(e.setClock(kFeb7_2026_1234Utc + 11 * 3600 + 26 * 60, 180));  // 23:60 UTC-> next day local
    CHECK(text(e.state().date.c_str(), "2026/02/08"));
    CHECK(text(e.state().day.c_str(), "SUN"));
    CHECK(text(e.state().formatted.c_str(), "8 FEB"));
    CHECK(!e.setClock(0, -60));                          // before the epoch
}

TEST_CASE(civil_from_days_matches_known_dates) {
    int y = 0, m = 0, d = 0, w = 0;
    CHECK(EnvironmentModel::civilFromDays(0, y, m, d, w));
    CHECK(y == 1970 && m == 1 && d == 1 && w == 4);       // Thursday
    CHECK(EnvironmentModel::civilFromDays(19723, y, m, d, w));
    CHECK(y == 2024 && m == 1 && d == 1 && w == 1);       // Monday
    CHECK(EnvironmentModel::civilFromDays(19782, y, m, d, w));
    CHECK(y == 2024 && m == 2 && d == 29 && w == 4);      // the leap day, a Thursday
    CHECK(EnvironmentModel::civilFromDays(20491, y, m, d, w));
    CHECK(y == 2026 && m == 2 && d == 7 && w == 6);
    CHECK(!EnvironmentModel::civilFromDays(-1, y, m, d, w));
    CHECK(text(EnvironmentModel::dayName(0), "SUN"));
    CHECK(text(EnvironmentModel::dayName(7), "---"));
}

TEST_CASE(legacy_strings_truncate_instead_of_overflowing) {
    EnvironmentModel e;
    const char* longDate = "2026/02/07 this date is forty characters";
    CHECK(e.setLegacyDateTime(longDate, "12:34:56", "SATURDAY"));
    CHECK_EQ(e.state().date.size(), 10u);
    CHECK(text(e.state().date.c_str(), "2026/02/07"));
    CHECK(text(e.state().formatted.c_str(), "7 FEB"));
    CHECK_EQ(e.state().time.size(), 5u);
    CHECK_EQ(e.state().day.size(), 3u);
    CHECK(text(e.state().day.c_str(), "SAT"));
    CHECK(!e.setLegacyDateTime(longDate, "12:34:56", "SATURDAY"));  // unchanged
    CHECK(e.setLegacyDateTime("garbage", "--:--", "---"));
    CHECK(text(e.state().formatted.c_str(), "-- ---"));
    CHECK(!e.state().clockValid);
}

TEST_CASE(bands_stay_continuous_across_a_sweep) {
    EnvironmentModel e;
    int lastBand = -1;
    int regressions = 0;
    for (int i = 0; i <= 25; ++i) {
        const float t = 15.0f + 0.6f * static_cast<float>(i);  // 15.0 .. 30.0 in admitted steps
        CHECK(e.setIndoorTemp(t));
        const int band = static_cast<int>(e.state().indoorClass.band);
        if (band < lastBand) ++regressions;
        lastBand = band;
        CHECK(e.state().indoorClass.progress >= 0.0f && e.state().indoorClass.progress <= 1.0f);
    }
    CHECK_EQ(regressions, 0);
    CHECK(e.state().indoorClass.band == IndoorBand::Hot);
}

TEST_CASE(environment_allocates_nothing_over_10k_updates) {
    NO_HEAP_SCOPE();
    EnvironmentModel e;
    int notified = 0;
    CHECK(e.subscribe(&countNotify, &notified));
    for (int i = 0; i < 10000; ++i) {
        e.setClock(kFeb7_2026_1234Utc + static_cast<int64_t>(i) * 60, 180);
        e.setIndoorTemp(15.0f + static_cast<float>(i % 20));
        e.setOutdoorTemp(-5.0f + static_cast<float>(i % 30));
        e.commit();
    }
    CHECK(notified > 0);
}
