//#define SYNCOPE_DETECT_DEADLOCKS
#include <syncope.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <pthread.h>
#include <boost/timer.hpp>
#include <google/profiler.h>
#include <random>

using namespace std;

syncope::SymmetricLockLayer ll0(STATIC_STRING("ll0"));
syncope::SymmetricLockLayer ll1(STATIC_STRING("ll1"));
syncope::SymmetricLockLayer ll2(STATIC_STRING("ll2"));
syncope::SymmetricLockLayer ll3(STATIC_STRING("ll3"));
syncope::SymmetricLockLayer ll4(STATIC_STRING("ll4"));
syncope::SymmetricLockLayer ll5(STATIC_STRING("ll5"));
syncope::SymmetricLockLayer ll6(STATIC_STRING("ll6"));
syncope::SymmetricLockLayer ll7(STATIC_STRING("ll7"));
syncope::SymmetricLockLayer ll8(STATIC_STRING("ll8"));
syncope::SymmetricLockLayer ll9(STATIC_STRING("ll9"));
syncope::SymmetricLockLayer llA(STATIC_STRING("llA"));
syncope::SymmetricLockLayer llB(STATIC_STRING("llB"));
syncope::SymmetricLockLayer llC(STATIC_STRING("llC"));
syncope::SymmetricLockLayer llD(STATIC_STRING("llD"));
syncope::SymmetricLockLayer llE(STATIC_STRING("llE"));
syncope::SymmetricLockLayer llF(STATIC_STRING("llF"));

void perftest() {
    for (int i = 0; i < 1000000; i++) {
        SYNCOPE_LOCK(ll0, &i);
        SYNCOPE_LOCK(ll1, &i);
        SYNCOPE_LOCK(ll2, &i);
        SYNCOPE_LOCK(ll3, &i);
        SYNCOPE_LOCK(ll4, &i);
        SYNCOPE_LOCK(ll5, &i);
        SYNCOPE_LOCK(ll6, &i);
        SYNCOPE_LOCK(ll7, &i);
        SYNCOPE_LOCK(ll8, &i);
        SYNCOPE_LOCK(ll9, &i);
        SYNCOPE_LOCK(llA, &i);
        SYNCOPE_LOCK(llB, &i);
        SYNCOPE_LOCK(llC, &i);
        SYNCOPE_LOCK(llD, &i);
        SYNCOPE_LOCK(llE, &i);
        SYNCOPE_LOCK(llF, &i);
    }
}

void create_deadlock() {
    int cnt = 0;
    for(auto i = 0u; i < 100000u; i++) {
        auto x = random();
        switch(x & 0x7) {
        case 0: {
            SYNCOPE_LOCK(ll0, &i);
            SYNCOPE_LOCK(ll1, &i);
            SYNCOPE_LOCK(ll2, &i);
            cnt++;
        }
        case 1: {
            SYNCOPE_LOCK(ll2, &i);
            SYNCOPE_LOCK(ll3, &i);
            SYNCOPE_LOCK(ll4, &i);
            cnt++;
        }
        case 2: {
            SYNCOPE_LOCK(ll4, &i);
            SYNCOPE_LOCK(ll5, &i);
            SYNCOPE_LOCK(ll6, &i);
            cnt++;
        }
        case 3: {
            SYNCOPE_LOCK(ll6, &i);
            SYNCOPE_LOCK(ll7, &i);
            SYNCOPE_LOCK(ll8, &i);
            cnt++;
        }
        case 4: {
            SYNCOPE_LOCK(ll9, &i);
            SYNCOPE_LOCK(llA, &i);
            SYNCOPE_LOCK(llB, &i);
            cnt++;
        }
        case 5: {
            SYNCOPE_LOCK(llB, &i);
            SYNCOPE_LOCK(llC, &i);
            SYNCOPE_LOCK(llD, &i);
            cnt++;
        }
        case 6: {
            SYNCOPE_LOCK(llD, &i);
            SYNCOPE_LOCK(llE, &i);
            SYNCOPE_LOCK(llF, &i);
            cnt++;
        }
        case 7: {
            SYNCOPE_LOCK(llE, &i);
            SYNCOPE_LOCK(llF, &i);
            SYNCOPE_LOCK(ll0, &i);
            cnt++;
        }
        case 8: {
            SYNCOPE_LOCK(ll1, &i);
            SYNCOPE_LOCK(ll0, &i);
            SYNCOPE_LOCK(llF, &i);
            cnt++;
        }
        };
    }
}

int main()
{
    {
        std::thread td1(create_deadlock);
        std::thread td2(create_deadlock);
        std::thread td3(create_deadlock);
        std::thread td4(create_deadlock);
        td1.join();
        td2.join();
        td3.join();
        td4.join();
    }
    const int NITER = 10000000;
    boost::timer tm;
    perftest();
    std::cout << "Perf test finished in " << tm.elapsed() << "s" << std::endl;
    {
        syncope::AsymmetricLockLayer layer(STATIC_STRING("base"));
        std::vector<int> shared_data;
        auto worker = [&]() {
            //ProfilerStart("test");
            size_t max_size = 0;
            for (int i = 0; i < NITER; i++) {
                if ((i & 0x1ff) == 0) {
                    SYNCOPE_LOCK_WRITE(layer, &shared_data);
                    shared_data.push_back(i);
                } else {
                    SYNCOPE_LOCK_READ(layer, &shared_data);
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
