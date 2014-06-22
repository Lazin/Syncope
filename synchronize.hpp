#pragma once

#ifndef SYNCHRONIZE_LOCKS_NUM
#   define SYNCHRONIZE_LOCKS_NUM 0x1000
#endif

#include <array>
#include <mutex>
#include <memory>
#include <functional>

namespace sync {

    template<int N>
    class Locker {
        static_assert((N & (N - 1)) == 0, "N must be a power of two");
        static const int MASK = N - 1;
        mutable std::array<std::mutex, N> mutexes_;
    public:

        void lock(size_t hash) const {
            // Get correct index from hashconstexpr
            size_t ix = hash & MASK;
            mutexes_[ix].lock();
        }

        void unlock(size_t hash) const {
            size_t ix = hash & MASK;
            mutexes_[ix].unlock();
        }
    };

    typedef Locker<SYNCHRONIZE_LOCKS_NUM> DefaultLockerT;

    namespace detail {
        // global Locker
        DefaultLockerT const* get_default_locker() {
            static DefaultLockerT default_locker;
            return &default_locker;
        }
    };

    template<class T>
    class SyncRef {
        T* ptr_;

        void lock() const {
            std::hash<T*> hash;
            detail::get_default_locker()->lock(hash(ptr_));
        }

        void unlock() const {
            std::hash<T*> hash;
            detail::get_default_locker()->unlock(hash(ptr_));
        }
    public:
        SyncRef(T* ptr) : ptr_(ptr) {
            lock();
        }

        SyncRef(SyncRef const&) = delete;

        SyncRef& operator = (SyncRef const&) = delete;

        SyncRef(SyncRef&& other) : ptr_(other.ptr_) {
            other.ptr_ = nullptr;
        }

        SyncRef& operator = (SyncRef&& other) {
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }

        ~SyncRef() {
            if (ptr_ != nullptr) {
                unlock();
            }
        }

        T* operator -> () { return ptr_; }
    };

    template<class T>
    SyncRef<T> synchronize(T);

    template<class T>
    SyncRef<T> synchronize(std::unique_ptr<T> const& ptr) {
        return std::move(SyncRef<T>(ptr.get()));
    }

    template<class T>
    SyncRef<T> synchronize(std::shared_ptr<T> const& ptr) {
        return std::move(SyncRef<T>(ptr.get()));
    }

    template<class T>
    SyncRef<T> synchronize(T const& ref) {
        return std::move(SyncRef<T>(&ref));
    }

    template<class T>
    SyncRef<T> synchronize(T& ref) {
        return std::move(SyncRef<T>(&ref));
    }

    template<class T>
    SyncRef<T> synchronize(T const* ptr) {
        return std::move(SyncRef<T>(ptr));
    }

    template<class T>
    SyncRef<T> synchronize(T* ptr) {
        return std::move(SyncRef<T>(ptr));
    }
}
