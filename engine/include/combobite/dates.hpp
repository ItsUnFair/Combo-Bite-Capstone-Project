// Calendar dates for the "prices last checked" date (tasks 6.4 and 7.2).
#pragma once

#include <optional>
#include <string>

namespace combobite {

struct Date {
    int year = 1970;
    int month = 1;
    int day = 1;
};

// Reads a date written exactly as YYYY-MM-DD; returns nullopt for anything
// else, including impossible dates such as 2026-02-30.
std::optional<Date> parse_date(const std::string& text);

std::string format_date(const Date& date);

// Days since 1970-01-01, so two dates can be subtracted.
long long days_since_epoch(const Date& date);

inline long long days_between(const Date& from, const Date& to) {
    return days_since_epoch(to) - days_since_epoch(from);
}

// Today's date on this computer's clock.
Date today_local();

}  // namespace combobite
