README
======

Syncope is a data driven synchronization library for C++11. Unlike any other C++ synchronization library, Syncope is unitrusive. That means that there is no need to include something in your data-structures or use inheritance. Syncope is completely external and decoupled synchronization mechanism. Syncope is a proof-of-consept implementation.

## Lock hierarchy
The very basic concept in syncope is lock-hierarchy. Syncope makes lock-hierarchies explicit. Actually, you don't need any mutexes instead of that just define layers of your lock hierarchy and than uses them to lock objects.

## Locking objects
```C++
// Somewhere in header file
class KVStore {
  ...
  void insert(std::string key, std::shared_ptr<Object> value);
};
```
Implementation in *.cpp file:
```C++
static syncope::SymmetricLockLayer ds_lock_layer(STATIC_STRING("DataStore"));

void KVStore::insert(std::string key, std::shared_ptr<Object> value) {
  auto lock_guard = ds_lock_layer.synchronize(this);
  // Modify object state
}
```
In first line of the `insert` method scope variable `lock_guard` is created. You can think about it as analoge of the std::lock_guard instance. Object `this` is locked as long as this scoped variable lives. Many instances of `KVStore` class can be created and `insert` calls on different instances wouldn't block each other form different thread. But if you try to call `insert` method of the same object from different threads - all of them will be executed sequentially.

This example behaves almost exactly the same as with mutex embedded in `KVStore`. In this example I embed `synchronize` call into `insert` method but it isn't nececary.
```C++
std::unique_ptr<KVStore> store(new KVStore());
...
// insert value to store
{
  auto lock_guard = ds_lock_layer.synchronize(store.get());
  store->insert("foo", bar);
}
```

## Asymmetric locks
What if most of the time we just read the data from our KVStore instances? In this case we need asymmetric lock layer (analog of the read-write lock).
```C++
class KVStore {
  ...
  void insert(std::string key, std::shared_ptr<Object> value);
  std::shared_ptr<Object> get(std::string key) const;
};
```
Implementation
```C++
static syncope::AsymmetricLockLayer ds_lock_layer(STATIC_STRING("DataStore"));

void KVStore::insert(std::string key, std::shared_ptr<Object> value) {
  auto lock_guard = ds_lock_layer.write_lock(this);
  // Modify object state
}

std::shared_ptr<Object> KVStore::get(std::string key) const {
  auto lock_guard = ds_lock_layer.read_lock(this);
  // Read object state
  return result;
}
```
In this example I've add method `get` that doesn't change object's internal state and can be called in parallel. This method acquires read-lock and our good old `insert` mthod now acquires write-lock. Asymmetric lock layer is biased toward readers. This means that read-lock is very cheap (cheaper than normal symmetric lock) and write-lock is much more expencive (more expencive than symmetric lock).

## Organaizing your lock hierarchy.
Only one lock from any lock layer can be acquired from one thread any time. Different threads can acquire multiple locks from multiple lock layer only in the same order! I'm planning to add deadlock detector to check this invariant in runtime (this feature can be disabled). For example: you have two lock layers - "DataLayer" and "BusinessLogicLayer". Every thread must acquire locks in the same order (even when their are aceccing different objects) in the same global order, for example "DataLayer" first and the "BusinessLogicLayer" second.

It's not safe to lock objects under the lock from the same lock layer. Example:
```C++
Foo a, b;
...
auto outer_lock = lock_layer.synchronize(&a);
...
{
  auto inner_lock = lock_layer.synchronize(&b);  // can cause deadlock
}
```
Syncope uses lock pool and hashing unser the hood so you can get deadlock even if you lock on different objects this way. You must synchronize both objects with one call instead:
```C++
Foo a, b;
...
auto outer_lock = lock_layer.synchronize(&a, &b);
...
```
Or use separate lock layers:
```C++
Foo a, b;
...
auto outer_lock = outer_lock_layer.synchronize(&a);
...
{
  auto inner_lock = internal_lock_layer.synchronize(&b);  // OK!
}
```
Just remember that nesting locks from the same lock layer is bad and completely defeats the purpose of the library (splitting all your locks between lock-hierarchy layers).

