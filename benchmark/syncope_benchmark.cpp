#include <syncope.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <pthread.h>
#include <boost/timer.hpp>
#include <google/profiler.h>

using namespace std;

int main()
{
    const int NITER = 10000000;
    boost::timer tm;
    {
        syncope::AsymmetricLockLayer layer(STATIC_STRING("base"));
        std::vector<int> shared_data;
        auto worker = [&]() {
            //ProfilerStart("test");
            size_t max_size = 0;
            for (int i = 0; i < NITER; i++) {
                if ((i & 0x1ff) == 0) {
                    auto write_lock = layer.write_lock(&shared_data);
                    shared_data.push_back(i);
                } else {
                    auto read_lock = layer.read_lock(&shared_data);
                    if (shared_data.size() > max_size) {
                        max_size = shared_data.size();
                    }
                }
            }
            //ProfilerStop();
            std::cout << max_size << std::endl;
        };
        std::thread test0(worker);
        std::thread test1(worker);
        std::thread test2(worker);
        std::thread test3(worker);
        test0.join();
        test1.join();
        test2.join();
        test3.join();
    }
    std::cout << "My = " << tm.elapsed() << std::endl;
    tm.restart();
    {
        std::vector<int> shared_data;
        pthread_rwlock_t rwlock;
        pthread_rwlock_init(&rwlock, 0);
        auto worker = [&]() {
            size_t max_size = 0;
            for (int i = 0; i < NITER; i++) {
                if ((i & 0x1ff) == 0) {
                    pthread_rwlock_wrlock(&rwlock);
                    shared_data.push_back(i);
                    pthread_rwlock_unlock(&rwlock);
                } else {
                    pthread_rwlock_rdlock(&rwlock);
                    if (shared_data.size() > max_size) {
                        max_size = shared_data.size();
                    }
                    pthread_rwlock_unlock(&rwlock);
                }
            }
            std::cout << max_size << std::endl;
        };
        std::thread test0(worker);
        std::thread test1(worker);
        std::thread test2(worker);
        std::thread test3(worker);
        test0.join();
        test1.join();
        test2.join();
        test3.join();
    }
    std::cout << "restart = " << tm.elapsed() << std::endl;
    return 0;
}
