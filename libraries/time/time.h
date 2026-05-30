#include <chrono>
#include <ctime>

namespace skyview {

std::string get_now_time_in_string () 

{

    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;

}

int get_now_time_in_int () 

{

    std::time_t time = std::time(nullptr);
    return time;

}

}