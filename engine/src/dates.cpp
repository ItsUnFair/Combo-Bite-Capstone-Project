#include "combobite/dates.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace combobite {

namespace {

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap_year(year) ? 29 : days[month - 1];
}

}  // namespace

std::optional<Date> parse_date(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    }
    Date date;
    date.year = std::stoi(text.substr(0, 4));
    date.month = std::stoi(text.substr(5, 2));
    date.day = std::stoi(text.substr(8, 2));
    if (date.month < 1 || date.month > 12) return std::nullopt;
    if (date.day < 1 || date.day > days_in_month(date.year, date.month)) return std::nullopt;
    return date;
}

std::string format_date(const Date& date) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", date.year, date.month, date.day);
    return buffer;
}

// Howard Hinnant's days_from_civil algorithm.
long long days_since_epoch(const Date& date) {
    const long long y = date.year - (date.month <= 2 ? 1 : 0);
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long year_of_era = y - era * 400;
    const long long m = date.month;
    const long long day_of_year = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + date.day - 1;
    const long long day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

Date today_local() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return Date{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

}  // namespace combobite
