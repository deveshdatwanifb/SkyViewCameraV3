#include <iostream> 
#include "processor/processor.h"
#include "drawing/drawing.h"
#include <opencv2/opencv.hpp>
#include <string>

int main(int argc, char* argv[])
{

    std::string url (argv[1]);
    std::cout << "Reading from " << url << std::endl;
    skyview::process_loop(url);

}