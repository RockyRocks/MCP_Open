#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t n){
    if(n==0) n=1;
    stopping = false;
    for(size_t i=0;i<n;i++){
        workers.emplace_back([this]{
            while(true){
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this]{ return stopping || !tasks.empty(); });
                    if(stopping && tasks.empty()) return;
                    task = std::move(tasks.front()); tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool(){ shutdown(); }

void ThreadPool::shutdown(){
    {
        std::unique_lock<std::mutex> lock(mtx);
        stopping = true;
    }
    cv.notify_all();
    for(auto &t: workers) if(t.joinable()) t.join();
}

// template implementation
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    using R = decltype(f(args...));
    auto task = std::make_shared<std::packaged_task<R()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<R> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mtx);
        if(stopping) throw std::runtime_error("submit on stopped ThreadPool");
        tasks.emplace([task](){ (*task)(); });
    }
    cv.notify_one();
    return res;
}

// explicit instantiation to avoid link errors for common types is omitted; keeping template in this translation unit only
