#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <capture/capture.h>

namespace skyview {

class Processor {

public:

    cv::Mat frame;
    std::queue<cv::Mat> q;
    std::mutex mtx;
    std::condition_variable cond_var;
    
    const int ESC_KEY = 27;

    Processor (const std::string& stream) {

        streamUrl = stream;
    
    }

    void get_frame_from_queue (cv::Mat& frame, std::mutex& mtx, std::queue<cv::Mat>& q, bool& keep_running) {

        {
        
        std::unique_lock<std::mutex> lock(mtx);

        if (!q.empty()) {

            frame = std::move(q.front());
            q.pop();
        
        }

        } 

    }

    cv::Mat process_loop (cv::Mat& mat, std::mutex& mtx2) {
        
        bool keep_running {true};
        
        std::thread capture_thread(skyview::handle_frame_queue, streamUrl, std::ref(q), std::ref(mtx), std::ref(cond_var), std::ref(keep_running));

        while (keep_running) {

            get_frame_from_queue(frame, std::ref(mtx), std::ref(q), keep_running);
            
            {

                std::unique_lock locker(mtx2);
                mat = frame;

            }


        }

        if (capture_thread.joinable()) {

            capture_thread.join();
        
        }
        
        return frame;

    }

private:

    cv::VideoCapture cap;
    
    std::string streamUrl;

};

}