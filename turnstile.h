#ifndef _TURNSTILE_H_
#define _TURNSTILE_H_

#include <type_traits>

class Mutex {
    struct turnstile; // Implemented in cpp: PIMPL idiom
    static turnstile gate_turnstile;

    /*
     * Pointer has three states:
     * 1. Points to nullptr iff no thread locked Mutex - default, set by constructor.
     * 2. Points to gate_turnstile iff exactly one thread locked Mutex and no thread is waiting on it.
     * 3. Points to dynamically created turnstile iff some thread sleeps on it.
     * Therefore all new threads wishing to acquire mutex are supposed to wait as well.
     */
    turnstile* p_turnstile;

public:
    Mutex();
    Mutex(const Mutex&) = delete;

    void lock();    // NOLINT
    void unlock();  // NOLINT
};

#endif  // _TURNSTILE_H_
