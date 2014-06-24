#ifndef SYNCHRONIZE_LOCKS_NUM
#   define SYNCHRONIZE_LOCKS_NUM 0x100
#endif
#ifndef READ_SIDE_PARALLELISM
#   define READ_SIDE_PARALLELISM 0x8
#endif

#include <iostream>
#include <array>
#include <mutex>
#include <memory>
#include <functional>
#include <algorithm>
#include <thread>

namespace syncope {

namespace detail {

    class LockLayerImpl {
        enum {
            N = SYNCHRONIZE_LOCKS_NUM
        };
        static_assert((N & (N - 1)) == 0, "N (SYNCHRONIZE_LOCKS_NUM) must be a power of two");
        static const int MASK = N - 1;
        typedef std::recursive_mutex MutexT;
        mutable std::array<MutexT, N> mutexes_;
        const char* name_;
    public:
        LockLayerImpl(const char* name) : name_(name) {}
        LockLayerImpl(LockLayerImpl const&) = delete;
        LockLayerImpl& operator = (LockLayerImpl const&) = delete;

        void lock(size_t hash) const {
            size_t ix = hash & MASK;
            mutexes_[ix].lock();
        }

        void unlock(size_t hash) const {
            size_t ix = hash & MASK;
            mutexes_[ix].unlock();
        }
    };
} // namespace detail

// namespace locks

    template<class Hash, class T>
    class LockGuard {
        T const* ptr_;
        detail::LockLayerImpl& lock_pool_;
        Hash hash_;

        void lock() const {
            lock_pool_.lock(hash_(reinterpret_cast<size_t>(ptr_)));
        }

        void unlock() const {
            lock_pool_.unlock(hash_(reinterpret_cast<size_t>(ptr_)));
        }
    public:
        LockGuard(T const* ptr, detail::LockLayerImpl& lockpool) : ptr_(ptr), lock_pool_(lockpool) {
            lock();
        }

        LockGuard(LockGuard const&) = delete;

        LockGuard& operator = (LockGuard const&) = delete;

        LockGuard(LockGuard&& other) : ptr_(other.ptr_), lock_pool_(other.lock_pool_) {
            other.ptr_ = nullptr;
        }

        LockGuard& operator = (LockGuard&& other) {
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
            assert(&lock_pool_ == &other.lock_pool_);
        }

        ~LockGuard() {
            if (ptr_ != nullptr) {
                unlock();
            }
        }
    };

    template<class Hash, int P, typename... T>
    class LockGuardMany {
        enum {
            H = sizeof...(T)*P  // hashes array size (can be greater than sizeof...(T))
        };
        detail::LockLayerImpl& impl_;
        std::array<size_t, H> hashes_;

        template<int I, int M, int H>
        struct fill_hashes
        {
            void operator () (std::array<size_t, H>& hashes, std::tuple<T const*...> const& items) {
                Hash hash;
                const auto p = std::get<I>(items);
                for (int i = 0; i < P; i++) {
                    size_t h = hash(reinterpret_cast<size_t>(p), i);
                    hashes[I*P + i] = h;
                }
                fill_hashes<I + 1, M, H> fh;
                fh(hashes, items);
            }
        };

        template<int M, int H>
        struct fill_hashes<M, M, H>{
            void operator() (std::array<size_t, H>& hashes, std::tuple<T const*...> const& items) {}
        };

        void lock() const {
            for (auto h: hashes_) {
                impl_.lock(h);
            }
        }

        void unlock() const {
            for (auto it = hashes_.crbegin(); it != hashes_.crend(); it++) {
                impl_.unlock(*it);
            }
        }

    public:

        LockGuardMany(detail::LockLayerImpl& impl, T const*... others)
            : impl_(impl)
        {
            auto all = std::tie(others...);
            fill_hashes<0, sizeof...(others), H> fill_all;
            fill_all(hashes_, all);
            std::sort(hashes_.begin(), hashes_.end());
            lock();
        }

        ~LockGuardMany() {
            unlock();
        }
    };

    namespace detail {

    class StaticString {
        const char* str_;
    public:
        StaticString(const char* s) : str_(s) {
            // TODO: assert(string is static)
        }

        const char* str() const { return str_; }
    };

    //! Simple hash - uses std::hash internally
    struct SimpleHash {
        size_t operator() (size_t value) const {
            std::hash<size_t> hash;
            return hash(value);
        }
    };

    //! Simple hash - uses std::hash and ignores bias
    struct SimpleHash2 {
        size_t operator() (size_t value, int bias) const {
            std::hash<size_t> hash;
            return hash(value);
        }
    };

    template<int P>
    struct BiasedHash {
        static_assert((P & (P - 1)) == 0, "P must be a power of two");
        size_t operator() (size_t value) const {
            std::hash<size_t> hash;
            std::hash<std::thread::id> thash;
            auto id = std::this_thread::get_id();
            size_t bias = thash(id);
            return hash(value) + (bias & (P - 1));
        }
    };

    template<int P>
    struct BiasedHash2 {
        static_assert((P & (P - 1)) == 0, "P must be a power of two");
        size_t operator() (size_t value, int bias) const {
            std::hash<size_t> hash;
            return hash(value) + (bias & (P - 1));
        }
    };

    }

    /** Lock hierarchy layer.
      */
    class SymmetricLockLayer {
        detail::LockLayerImpl impl_;
    public:
    
        /** C-tor
          * @param name statically initialized string
          */
        SymmetricLockLayer(detail::StaticString name) : impl_(name.str()) {}

        template<class T>
        LockGuard<detail::SimpleHash, T> synchronize(T const* ptr) {
            return std::move(LockGuard<detail::SimpleHash, T>(ptr, impl_));
        }

        template<typename... T>
        LockGuardMany<detail::SimpleHash2, 1, T...> synchronize(T const*... args) {
            return std::move(LockGuardMany<detail::SimpleHash2, 1, T...>(impl_, args...));
        }
    };
    
    /** Asymmetric lock hierarchy layer.
      */
    class AsymmetricLockLayer {
        detail::LockLayerImpl impl_;
        enum {
            P = READ_SIDE_PARALLELISM  // Parallelism factor for readers and writers
        };
    public:

        /** C-tor
          * @param name statically initialized string
          */
        AsymmetricLockLayer(detail::StaticString name) : impl_(name.str()) {}

        template<class T>
        LockGuard<detail::BiasedHash<P>, T> read_lock(T const* ptr) {
            return std::move(LockGuard<detail::BiasedHash<P>, T>(ptr, impl_));
        }

        template<typename... T>
        LockGuardMany<detail::BiasedHash2<P>, P, T...> write_lock(T const*... args) {
            return std::move(LockGuardMany<detail::BiasedHash2<P>, P, T...>(impl_, args...));
        }
    };
}  // namespace syncope

#define STATIC_STRING(x) locks::detail::StaticString(x"")
