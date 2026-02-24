#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>

using namespace std;

void logWithTime(const string &msg)
{
    using namespace chrono;
    auto now = system_clock::now();
    auto itt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    tm tm{};
    localtime_r(&itt, &tm);

    ostringstream oss;
    oss << "[" << put_time(&tm, "%H:%M:%S")
        << "." << setw(3) << setfill('0') << ms.count()
        << "] " << msg;

    cout << oss.str() << endl;
}
