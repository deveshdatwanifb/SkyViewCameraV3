#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <opencv2/opencv.hpp>
#include <time/time.h>

namespace skyview {

void handle_frame_queue(const std::string& streamURL, 
                               std::queue<cv::Mat>& q, 
                               std::mutex& mtx, 
                               std::condition_variable& cond_var,
                               bool& keep_running) 
{
    cv::VideoCapture cap(streamURL);

    
    std::cout << "Launching capture thread" << std::endl;
    std::cout << "Capture start time" << skyview::get_now_time_in_string() << std::endl;
    
    if (!cap.isOpened()) return;

    while (keep_running) {

        cv::Mat frame;

        if (cap.read(frame) && !frame.empty()) {

            cv::Mat frame_copy = frame.clone();

            {
                std::lock_guard<std::mutex> lock(mtx);

                if (q.size() >= 3) {
                    
                    q.pop();
                
                }
                
                q.push(std::move(frame_copy));
            
            } 
            
            cond_var.notify_one(); 
        }

        else if (frame.empty()) {
        
            std::cout << "Cannot grab frame" << std::endl;
            keep_running = false;
            break;
        
        }
    }

    cap.release();

}

} 