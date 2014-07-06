README
======

Syncope is a data driven synchronization library for C++11. Unlike any other C++ synchronization library, Syncope is unitrusive. That means that there is no need to include something in your data-structures or use inheritance. Syncope is completely external and decoupled synchronization mechanism. Syncope is a proof-of-concept implementation and intended to become stable in future.

The name "Syncope" is perfect for synchronization library. First of all in many cases lock must be held for a short time, only when necessary. This reminds me syncopation in music because it's all about timings. Second - both words "synchronization" and "syncopation" sounds similar. And finally - synchronization is often a source of headache and can cause a syncope :)

## Lock hierarchy
The very basic concept in syncope is lock-hierarchy. Syncope makes lock-hierarchies explicit. Actually, you don't need any mutexes instead of that just define layers of your lock hierarchy that can be used to lock objects.

## Locking objects
Let's start with example:
```C++
// Somewhere in header file
class KVStore {
  ...
  void insert(std::string key, std::shared_ptr<Object> value);
};

// Implementation in *.cpp file:
static syncope::SymmetricLockLayer ds_lock_layer(STATIC_STRING("DataStore"));

void KVStore::insert(std::string key, std::shared_ptr<Object> value) {
  SYNCOPE_LOCK(ds_lock_layer, this);
  // Modify state
}
```
SYNCOPE_LOCK macro creates variable on the stack that works exactly like std::lock_guard. Time spent under lock is limited by the lifetime of this stack variable.

This example behaves almost exactly the same way as with mutex embedded in `KVStore`. In this example I've used SYNCOPE_LOCK inside `insert` method but it isn't necessary. It's up to you to decide where to put your synchronization code.
```C++
std::unique_ptr<KVStore> store(new KVStore());
...
// insert value to store
{
  SYNCOPE_LOCK(ds_lock_layer, store.get());
  store->insert("foo", bar);
}
```

## Asymmetric locks
What if most of the time we just read the data from our KVStore instances? In this case we need asymmetric lock layer (analog of the read-write lock).
```C++
// In a header file
class KVStore {
  ...
  void insert(std::string key, std::shared_ptr<Object> value);
  std::shared_ptr<Object> get(std::string key) const;
};

// In *.cpp file
static syncope::AsymmetricLockLayer ds_lock_layer(STATIC_STRING("DataStore"));

void KVStore::insert(std::string key, std::shared_ptr<Object> value) {
  SYNCOPE_LOCK_WRITE(ds_lock_layer, this);
  // Modify object state
}

std::shared_ptr<Object> KVStore::get(std::string key) const {
  SYNCOPE_LOCK_READ(ds_lock_layer, this);
  // Read object state
  return result;
}
```
In this example I've add method `get` that doesn't change object's internal state and can be called in parallel. This method acquires read-lock and our good old `insert` mthod now acquires write-lock. Asymmetric lock layer is biased toward readers. This means that read-lock is very cheap (cheaper than normal symmetric lock) and write-lock is much more expencive (more expencive than symmetric lock).

## Organaizing your lock hierarchy
Only one lock from any lock layer can be acquired from one thread any time. Different threads can acquire multiple locks from multiple lock layer only in the same order. For example: you have two lock layers - "DataLayer" and "BusinessLogicLayer". Every thread must acquire locks in the same order (even when their are aceccing different objects) in the same global order, for example "DataLayer" first and the "BusinessLogicLayer" second.

It's not safe to lock objects under the lock from the same lock layer. Example:
```C++
Foo a, b;
...
SYNCOPE_LOCK(lock_layer, &a);
...
{
  SYNCOPE_LOCK(lock_layer, &b);  // can cause deadlock
}
```
Syncope uses lock pool and hashing unser the hood so you can get deadlock even if you lock on different objects this way. You must synchronize both objects with one call instead:
```C++
Foo a, b;
...
SYNCOPE_LOCK_ALL(lock_layer, &a, &b);
...
```
Or use separate lock layers:
```C++
Foo a, b;
...
SYNCOPE_LOCK(outer_lock_layer, &a);
...
{
  SYNCOPE_LOCK(internal_lock_layer, &b);  // OK!
}
```
Just remember that nesting locks from the same lock layer is bad and completely defeats the purpose of the library (splitting all your locks between lock-hierarchy layers).

##Deadlock detection
This library implements deadlock detector. It searches for deadlocks between different lock layers and doesn't needs deadlock to actually happend. You can acquire locks in one order from one thread then release them and then you can try to acquire locks in different order from another thread - actual deadlock wouldn't happen but Syncope's deadlock detector will trigger alarm. Deadlock detector is disabled by default, to enable it you must define SYNCOPE_DETECT_DEADLOCKS before including `syncope.hpp`.

##Performance
Syncope adds very little overhead. If deadlock detector disabled SYNCOPE_LOCK will calculate very simple hash from pointer to object that will be used to acquire mutex from the pool. If deadlock detector is enabled - some additional overhead will be introduced in particular - one RMW operation per lock will be performed. It can cause contention and performance degradation.

`AsymmetricLockLayer` can be up to eight times faster than pthread_rwlock in read-heavy scenarios. Readers acquires only one uncontended lock and this can be done really fast (something about 10ns). Writers needs to acquire many locks (number of locks defined by the SYNCOPE_READ_SIDE_PARALLELISM macro-definition) but usually only some of them are contended. Acquiring contended lock is pricey (hundreds of nanoseconds) but most of the acquired locks is unconended so almost free in compare with contended locks. Because of that read-locks are fast and write-locks is only moderately slow.
