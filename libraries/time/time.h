#include <chrono>
#include <ctime>

namespace skyview {

std::string get_now_time () 
{

    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;

}

}