#include <iostream>
#include <opencv2/opencv.hpp>
#include <drawing/drawing.h>
#include <time/time.h>

namespace skyview {

void detect(cv::Mat frame) {
    // something
}

int drawing_key (0);

int process_loop(std::string streamURL)
{
    
    cv::VideoCapture cap(streamURL);

    double video_fps = cap.get(cv::CAP_PROP_FPS);
    int delay = static_cast<int>(1000.0 / video_fps);

    std::cout << "Playing at " << video_fps << " fps" << std::endl;
    
    if (!cap.isOpened()){
        std::cerr << "Error: Could not open stream" << std::endl;
        return -1;
    }

    cv::Mat frame;
    
    while (true) {
        
        bool success;

        success = cap.read(frame);
        
        if (!success || frame.empty()) {
            std::cerr << "Stream ended or failed to grab frame" << std::endl;
            break;
        }

        std::string timestamp = skyview::get_now_time();

        skyview::draw_time(frame, timestamp);
        skyview::draw_fps(frame, video_fps);

        cv::imshow("Live stream", frame);

        if (cv::waitKey(delay) ==27) break;

    }

    cap.release();
    cv::destroyAllWindows();
    return 0;

}

}