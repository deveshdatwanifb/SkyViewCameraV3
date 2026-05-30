#pragma once
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

void get_frame (cv::Mat& frame, std::mutex& mtx, std::queue<cv::Mat>& q,
                bool& keep_running) 

{

    {
    
    std::unique_lock<std::mutex> lock(mtx);

    if (!q.empty()) {

        frame = std::move(q.front());
        q.pop();
    
    }

    } 

}

int process_loop (const std::string& streamURL)

{

    cv::Mat frame;
    
    std::queue<cv::Mat> q;
    std::mutex mtx;
    std::condition_variable cond_var;
    
    bool keep_running {true};
    
    std::thread capture_thread(skyview::handle_frame_queue, streamURL, std::ref(q), std::ref(mtx), std::ref(cond_var), std::ref(keep_running));
    
    const int ESC_KEY = 27;

    while (keep_running) {

        skyview::get_frame(frame, std::ref(mtx), std::ref(q), keep_running);

        if (!frame.empty()) {

            cv::imshow("Frame", frame);

        }

        if (cv::waitKey(1) == ESC_KEY) {
        
            keep_running = false;
            cond_var.notify_all(); 
            break;
        
        }

    }

    if (capture_thread.joinable()) {

        capture_thread.join();
    
    }
    
    cv::destroyAllWindows();
    return 1;

}

}