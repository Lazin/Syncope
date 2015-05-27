#ifndef SYNCOPE_HPP
#define SYNCOPE_HPP

#ifndef SYNCOPE_NUM_LOCKS
#   define SYNCOPE_NUM_LOCKS 0x100
#endif
#ifndef SYNCOPE_READ_SIDE_PARALLELISM
#   define SYNCOPE_READ_SIDE_PARALLELISM 0x8
#endif

#ifndef SYNCOPE_MAX_LAYERS
#   define SYNCOPE_MAX_LAYERS 100
#endif

#ifndef SYNCOPE_MAX_DEPTH
#   define SYNCOPE_MAX_DEPTH 0x10
#endif

#ifdef SYNCOPE_DETECT_DEADLOCKS
#include <iostream>
#include <string>
#include <exception>
#include <sstream>
#endif

#include <mutex>
#include <memory>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cassert>

namespace syncope {

namespace detail {

    static const int CACHE_LINE_BITS = 6;

    class LockLayerImpl;

    struct TraceRoot {
        typedef std::tuple<LockLayerImpl*, const char*> Owner;
        std::unique_ptr<Owner[]> owners;
        int top;

        TraceRoot() 
            : owners(new Owner[SYNCOPE_MAX_DEPTH])
            , top(0)
        {
        }
    };

    class Detector {
        static const int TRANSITIONS_SIZE = SYNCOPE_MAX_LAYERS * SYNCOPE_MAX_LAYERS;
        struct CounterWithPad {
            std::atomic<size_t> counter;
            char pad[64 - sizeof(counter)];
            CounterWithPad()
                : counter{0}
            {
            }
        };
        std::array<CounterWithPad, TRANSITIONS_SIZE> transitions;
        Detector() {}
    public:
        static Detector& inst() {
            static Detector d;
            return d;
        }

        void on_lock(TraceRoot::Owner const& prev, TraceRoot::Owner const& curr);

        void on_deadlock(LockLayerImpl* curr, const char* message);
    };

    class LockLayerImpl {
        enum {
            N = SYNCOPE_NUM_LOCKS
        };
        static_assert((N & (N - 1)) == 0, "N (SYNCOPE_NUM_LOCKS) must be a power of two");
        static const int MASK = N - 1;
        typedef std::mutex MutexT;
        mutable std::array<MutexT, N> mutexes_;
        const char* name_;
        int level_;
        const int id_;
        static thread_local TraceRoot tls_root;
        static std::atomic<int> layers_counter;
    public:
        LockLayerImpl(const char* name, int level)
            : name_(name)
            , level_(level)
            , id_(layers_counter++)
        {
        }

        LockLayerImpl(LockLayerImpl const&) = delete;
        LockLayerImpl& operator = (LockLayerImpl const&) = delete;

        void lock(size_t hash) {
            size_t ix = hash & MASK;
            mutexes_[ix].lock();
        }


        void unlock(size_t hash) {
            size_t ix = hash & MASK;
            mutexes_[ix].unlock();
        }

        int get_id() const {
            return id_;
        }

#ifdef SYNCOPE_DETECT_DEADLOCKS

        void detector_lock(const char* loc) {
            TraceRoot& root = tls_root;
            if (root.top > SYNCOPE_MAX_DEPTH) {
                report_error("max depth reached");
            }
            root.owners[root.top] = std::make_tuple(this, loc);
            root.top++;
            auto top = root.top;
            for (int i = top - 2; i >= 0; i--) {
                Detector::inst().on_lock( root.owners[i]
                                        , root.owners[root.top-1]);
                break;
            }
        }

        void detector_unlock() {
            TraceRoot& root = tls_root;
            if (root.top == 0) {
                report_error("double unlock");
            }
            root.top--;
        }

        void report_error(const char* message) const {
            TraceRoot& root = tls_root;
            std::stringstream sstream;
            sstream << "Deadlock detector - " << message << std::endl;
            for (auto i = 0; i < root.top; i++) {
                auto layer = std::get<0>(root.owners[i]);
                auto loc = std::get<1>(root.owners[i]);
                sstream << "layer[" << i << "] is " << layer->name_ << " at " << loc << std::endl;
            }
            std::cout << sstream.str() << std::endl;
            std::terminate();
        }
#endif
    };

    std::atomic<int> LockLayerImpl::layers_counter{0};
    thread_local TraceRoot LockLayerImpl::tls_root;

    void Detector::on_deadlock(LockLayerImpl* curr, const char* message) {
#ifdef SYNCOPE_DETECT_DEADLOCKS
        curr->report_error(message);
#endif
    }

    void Detector::on_lock(TraceRoot::Owner const& prev, TraceRoot::Owner const& curr) {
        LockLayerImpl* prev_layer = std::get<0>(prev);
        LockLayerImpl* curr_layer = std::get<0>(curr);
        auto id_prev = prev_layer->get_id();
        auto id_curr = curr_layer->get_id();
        int x, y, dir;
        if (id_prev > id_curr)  {
            x = id_prev;
            y = id_curr;
            dir = 1;
        } else if (id_prev < id_curr){
            y = id_prev;
            x = id_curr;
            dir = 2;
        } else {
            on_deadlock(curr_layer, "recursion detected");
            return;
        }
        int addr = y*SYNCOPE_MAX_LAYERS + x;
        int res = transitions[addr].counter.exchange(dir);
        if (res && res != dir) {
            on_deadlock(curr_layer, "deadlock detected");
        }
    }
} // namespace detail

// namespace locks

    template<class T>
    class LockGuard {
        size_t value_;
        bool owns_lock_;
        detail::LockLayerImpl& lock_pool_;
#ifdef  SYNCOPE_DETECT_DEADLOCKS
        const char* loc_;
#endif

        void lock() {
#ifdef  SYNCOPE_DETECT_DEADLOCKS
            lock_pool_.detector_lock(loc_);
#endif
            lock_pool_.lock(value_);
            owns_lock_ = true;
        }

        void unlock() {
#ifdef SYNCOPE_DETECT_DEADLOCKS
            lock_pool_.detector_unlock();
#endif
            lock_pool_.unlock(value_);
            owns_lock_ = false;
        }
    public:
        template<typename Hash>
        LockGuard( T const* ptr
                 , detail::LockLayerImpl& lockpool
#ifdef SYNCOPE_DETECT_DEADLOCKS
                 , const char* loc
#endif
                 , Hash const& hash)
            : value_(hash(reinterpret_cast<size_t>(ptr)))
            , owns_lock_(false)
            , lock_pool_(lockpool)
#ifdef SYNCOPE_DETECT_DEADLOCKS
            , loc_(loc)
#endif
        {
            lock();
        }

        LockGuard(LockGuard const&) = delete;

        LockGuard& operator = (LockGuard const&) = delete;

        LockGuard(LockGuard&& other)
            : value_(other.value_)
            , owns_lock_(other.owns_lock_)
            , lock_pool_(other.lock_pool_)
#ifdef SYNCOPE_DETECT_DEADLOCKS
            , loc_(other.loc_)
#endif
        {
            other.owns_lock_ = false;
        }

        LockGuard& operator = (LockGuard&& other) {
            value_ = other.value_;
            other.owns_lock_ = false;
            assert(&lock_pool_ == &other.lock_pool_);
#ifdef SYNCOPE_DETECT_DEADLOCKS
            loc_ = other.loc_;
#endif
        }

        ~LockGuard() {
            if (owns_lock_) {
                unlock();
            }
        }
    };

    template<int P, typename... T>
    class LockGuardMany {
        enum {
            H = sizeof...(T)*P  // hashes array size (can be greater than sizeof...(T))
        };
        detail::LockLayerImpl& impl_;
        std::array<size_t, H> hashes_;
        size_t hashes_count_;
        bool owns_lock_;
#ifdef  SYNCOPE_DETECT_DEADLOCKS
        const char* loc_;
#endif

        template<int I, int M, int H>
        struct fill_hashes
        {
            template<typename Hash>
            void operator () (std::array<size_t, H>& hashes, std::tuple<T const*...> const& items, Hash const& hash) {
                const auto p = std::get<I>(items);
                for (int i = 0; i < P; i++) {
                    size_t h = hash(reinterpret_cast<size_t>(p), i);
                    hashes[I*P + i] = h;
                }
                fill_hashes<I + 1, M, H> fh;
                fh(hashes, items, hash);
            }
        };

        template<int M, int H>
        struct fill_hashes<M, M, H>{
            template<typename Hash>
            void operator() (std::array<size_t, H>& hashes, std::tuple<T const*...> const& items, Hash const& hash) {}
        };

        void lock() {
#ifdef SYNCOPE_DETECT_DEADLOCKS
            impl_.detector_lock(loc_);
#endif
            for (size_t i = 0; i < hashes_count_; i++) {
                impl_.lock(hashes_[i]);
            }
            owns_lock_ = true;
        }

        void unlock() {
#ifdef SYNCOPE_DETECT_DEADLOCKS
            impl_.detector_unlock();
#endif
            for (size_t i = hashes_count_ - 1; i != size_t(0u) - 1; i--) {
                impl_.unlock(hashes_[i]);
            }
            owns_lock_ = false;
        }

    public:

        template<typename Hash>
        LockGuardMany( detail::LockLayerImpl& impl
#ifdef SYNCOPE_DETECT_DEADLOCKS
                     , const char* loc
#endif
                     , Hash const& hash
                     , T const*... others)
            : impl_(impl)
            , owns_lock_(false)
#ifdef SYNCOPE_DETECT_DEADLOCKS
            , loc_(loc)
#endif
        {
            auto all = std::tie(others...);
            fill_hashes<0, sizeof...(others), H> fill_all;
            fill_all(hashes_, all, hash);
            std::sort(hashes_.begin(), hashes_.end());
            auto it = std::unique(hashes_.begin(), hashes_.end());
            hashes_count_ = std::distance(hashes_.begin(), it);
            lock();
        }

        ~LockGuardMany() {
            if (owns_lock_) {
                unlock();
            }
        }

        LockGuardMany(LockGuardMany const&) = delete;
        LockGuardMany& operator = (LockGuardMany const&) = delete;

        LockGuardMany(LockGuardMany&& other)
            : impl_(other.impl_)
            , hashes_count_(other.hashes_count_)
            , owns_lock_(other.owns_lock_)
        {
            std::swap(hashes_, other.hashes_);
            other.owns_lock_ = false;
        }

        LockGuardMany& operator = (LockGuardMany&& other) {
            assert(&other.impl_ == &impl_);
            std::swap(hashes_, other.hashes_);
            hashes_count_ = other.hashes_count_;
            owns_lock_ = other.owns_lock_;
            other.owns_lock_ = false;
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

    //! Simple hash - simply returns it's argument
    struct SimpleHash {
        size_t operator() (size_t value) const {
            return value >> CACHE_LINE_BITS;
        }
    };

    //! Simple hash
    struct SimpleHash2 {
        size_t operator() (size_t value, int bias) const {
            return value >> CACHE_LINE_BITS;
        }
    };

    template<int P>
    struct BiasedHash {
        static_assert((P & (P - 1)) == 0, "P must be a power of two");
        size_t operator() (size_t value) const {
            std::hash<std::thread::id> hash;
            auto id = std::this_thread::get_id();
            size_t bias = hash(id);
            return (value >> CACHE_LINE_BITS) + (bias & (P - 1));
        }
    };

    template<int P>
    struct BiasedHash2 {
        static_assert((P & (P - 1)) == 0, "P must be a power of two");
        size_t operator() (size_t value, int bias) const {
            return (value >> CACHE_LINE_BITS) + (bias & (P - 1));
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
        SymmetricLockLayer(detail::StaticString name, int level = -1) : impl_(name.str(), level) {}


#ifdef SYNCOPE_DETECT_DEADLOCKS
        template<class T>
        LockGuard<T> synchronize(
                const char* loc,
                T const* ptr) {
            return std::move(LockGuard<T>(ptr, impl_, loc, detail::SimpleHash()));
        }
#else
        template<class T>
        LockGuard<T> synchronize(T const* ptr) {
            return std::move(LockGuard<T>(ptr, impl_, detail::SimpleHash()));
        }
#endif

#ifdef SYNCOPE_DETECT_DEADLOCKS
        template<typename... T>
        LockGuardMany<1, T...> synchronize_all(
                const char* loc,
                T const*... args) {
            return std::move(LockGuardMany<1, T...>(impl_, loc, detail::SimpleHash2(), args...));
        }
#else
        template<typename... T>
        LockGuardMany<1, T...> synchronize_all(T const*... args) {
            return std::move(LockGuardMany<1, T...>(impl_, detail::SimpleHash2(), args...));
        }
#endif
    };

    /** Asymmetric lock hierarchy layer.
      */
    class AsymmetricLockLayer {
        detail::LockLayerImpl impl_;
        enum {
            P = SYNCOPE_READ_SIDE_PARALLELISM  // Parallelism factor for readers and writers
        };
    public:

        /** C-tor
          * @param name statically initialized string
          */
        AsymmetricLockLayer(detail::StaticString name, int level = -1) : impl_(name.str(), level) {}

#ifdef SYNCOPE_DETECT_DEADLOCKS
        template<class T>
        LockGuard<T> synchronize_read(
                const char* loc,
                T const* ptr) {
            return std::move(LockGuard<T>(ptr, impl_, loc, detail::BiasedHash<P>()));
        }
#else
        template<class T>
        LockGuard<T> synchronize_read(T const* ptr) {
            return std::move(LockGuard<T>(ptr, impl_, detail::BiasedHash<P>()));
        }
#endif

#ifdef SYNCOPE_DETECT_DEADLOCKS
        template<typename T>
        LockGuardMany<P, T> synchronize_write(
                const char* loc,
                T const* arg) {
            return std::move(LockGuardMany<P, T>(impl_, loc, detail::BiasedHash2<P>(), arg));
        }
#else
        template<typename T>
        LockGuardMany<P, T> synchronize_write(T const* arg) {
            return std::move(LockGuardMany<P, T>(impl_, detail::BiasedHash2<P>(), arg));
        }
#endif
    };
}  // namespace syncope

#define STATIC_STRING(x) syncope::detail::StaticString(x"")
#define SYNCOPE_STRINGIFY_DETAIL(x) #x
#define SYNCOPE_STRINGIFY(x) SYNCOPE_STRINGIFY_DETAIL(x)

#ifdef SYNCOPE_DETECT_DEADLOCKS

#define _SYNCOPE_LOCK_IMPL(layer, msg, ptr) auto __scope_lock_guard_##layer = layer.synchronize(msg, ptr)
#define SYNCOPE_LOCK(layer, ptr) _SYNCOPE_LOCK_IMPL(layer, __FILE__ ":" SYNCOPE_STRINGIFY(__LINE__), ptr);

#define _SYNCOPE_LOCK_ALL_IMPL(layer, msg, ...) auto __scope_lock_guard_##layer = layer.synchronize_all(msg, __VA_ARGS__)
#define SYNCOPE_LOCK_ALL(layer, ...) _SYNCOPE_LOCK_ALL_IMPL(layer, __FILE__ ":" SYNCOPE_STRINGIFY(__LINE__), __VA_ARGS__);

#define _SYNCOPE_LOCK_READ_IMPL(layer, msg, ptr) auto __scope_lock_guard_##layer = layer.synchronize_read(msg, ptr)
#define SYNCOPE_LOCK_READ(layer, ptr) _SYNCOPE_LOCK_READ_IMPL(layer, __FILE__ ":" SYNCOPE_STRINGIFY(__LINE__), ptr);

#define _SYNCOPE_LOCK_WRITE_IMPL(layer, msg, ptr) auto __scope_lock_guard_##layer = layer.synchronize_write(msg, ptr)
#define SYNCOPE_LOCK_WRITE(layer, ptr) _SYNCOPE_LOCK_WRITE_IMPL(layer, __FILE__ ":" SYNCOPE_STRINGIFY(__LINE__), ptr);

#else

#define SYNCOPE_LOCK(layer, ptr)  auto __scope_lock_guard_##layer = layer.synchronize(ptr);

#define SYNCOPE_LOCK_ALL(layer, ...)  auto __scope_lock_guard_##layer = layer.synchronize_all(__VA_ARGS__);

#define SYNCOPE_LOCK_READ(layer, ptr) auto __scope_lock_guard_##layer = layer.synchronize_read(ptr);

#define SYNCOPE_LOCK_WRITE(layer, ptr) auto __scope_lock_guard_##layer = layer.synchronize_write(ptr);

#endif

#endif
