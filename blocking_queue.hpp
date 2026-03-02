#pragma once   
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>

template <typename T>
class blocking_queue {
    std::queue<T> q;
    std::mutex mut;
    std::condition_variable cond;
    bool done = false;
    public: 
        bool pop(T& popto){
            
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [&](){ return(done) || (!q.empty()); });

            if (q.empty()) return false;


            popto = std::move(q.front());
            q.pop();

            return true;
        }

        void push(const T& pushit){

            {
                std::lock_guard<std::mutex> lock(mut);
                q.push(pushit);
            }
            cond.notify_one();
        }

        bool isitdone() {
            std::lock_guard<std::mutex> lock(mut); // hold and release after check
            return q.empty() && done;
        }

        void all_done(){
            {
                std::lock_guard<std::mutex> lock(mut);
                done = true;
            } 
            cond.notify_all();
            
        }



};