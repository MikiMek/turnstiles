#include "turnstile.h"
#include <condition_variable>

// Comments are used to explain algorithm meticulously, not to serve as documentation.

namespace {
/*
 * Global mutexes used for synchronization.
 * Every Mutex has exactly one mutex assigned, but one mutex can be assigned to
 * multiple Mutexes. mutex is assigned by taking address of Mutex %
 * NUMBER_OF_MUTEXES;
 */
    constexpr int NUMBER_OF_MUTEXES = 389;
    std::mutex mutex[NUMBER_OF_MUTEXES];
}  // namespace

struct Mutex::turnstile {
    uint64_t cnt_waiting;  /// Number of threads waiting on condition variable.
    std::mutex* cv_m;
    std::condition_variable* cv;
    bool wait;  /// If true then thread should wait on condition variable.

    turnstile()
            : cnt_waiting(0),
              cv_m(nullptr),
              cv(nullptr),
              wait(true){};  // Empty turnstile
    explicit turnstile(std::mutex* _mutex, std::condition_variable* _cv)
            : cnt_waiting(0), cv_m(_mutex), cv(_cv), wait(true){};

    /*
     * Function simulates going through the turnstile.
     * Thread will finish executing this function when its turn comes.
     * Meanwhile it waits on condition variable.
     *
     * If thread happens to be the last one waiting on condition variable then it
     * destroys the turnstile. It's guaranteed that no other thread will try to
     * use this turnstile (turnstile is dropped by Mutex.unlock() method)
     */
    void go_through(uintptr_t mutex_id) {
        std::unique_lock<std::mutex> lck(*this->cv_m);

        this->cnt_waiting++;

        mutex[mutex_id].unlock();

        this->cv->wait(lck, [this] {
            if (!this->wait) {
                this->cnt_waiting--;
                this->wait = true;

                return true;
            } else {
                return false;
            }
        });  // Note: First mutex is reacquired and then the condition is checked!

        if (this->empty()) {  // No other thread waits on this turnstile, thus it's
            // to be destroyed.
            lck.unlock();
            delete this;
        }
    }

    /*
     * Unlocks turnstile, exactly one thread will go through.
     */
    void spin() {
        std::unique_lock<std::mutex> lck(*this->cv_m);
        this->wait = false;
        this->cv->notify_one();
    }

    bool empty() { return this->cnt_waiting == 0; }

    bool canDropAfterSpin() { return this->cnt_waiting == 1; }

    ~turnstile() {
        delete this->cv_m;
        delete this->cv;
    }
};

/*
 * Global turnstile used as a flag, when Mutex's pointer points to it it means
 * that exactly one thread is using the Mutex.
 */
Mutex::turnstile Mutex::gate_turnstile{};

Mutex::Mutex() : p_turnstile(nullptr) {}

/*
 * k - number of threads trying to lock Mutex
 *
 * (1) First thread(k == 1) can use Mutex freely. It sets pointer to the gate,
 * therefore second thread will know that it has to create turnstile to sleep
 * on.
 *
 * (2) Second thread(k == 2) creates turnstile to sleep on.
 *
 * (3) Every thread(k >= 2) except first one must go through the turnstile and
 * wait for its turn.
 */
void Mutex::lock() {
    uintptr_t mutex_id = reinterpret_cast<uintptr_t>(this) % NUMBER_OF_MUTEXES;
    mutex[mutex_id].lock();

    if (this->p_turnstile == nullptr) {  // (1)
        this->p_turnstile = &gate_turnstile;
        mutex[mutex_id].unlock();
    } else {
        if (this->p_turnstile == &gate_turnstile)  // (2)
            this->p_turnstile =
                    new Mutex::turnstile(new std::mutex, new std::condition_variable);

        this->p_turnstile->go_through(mutex_id);  // (3)
    }
}

/*
 * (1) Incorrect use of Mutex
 *
 * (2) Thread was the only one using Mutex (the only one called M.lock())
 * therefore no thread uses Mutex now, pointer is set back to the nullptr;
 *
 * (3) Thread wasn't the only one using Mutex therefore there is at least one
 * thread that waits on condition variable. Thus turnstile is spun and some
 * thread is woke up. (3b) If there were exactly one thread waiting on turnstile
 * then it can be dropped -> pointer is set back to gate_turnstile. Dropped
 * turnstile will be destroyed by the only one thread waiting on it (in
 * go_through function) but it's not current_thread's concern.
 */
void Mutex::unlock() {
    uintptr_t mutex_id = reinterpret_cast<uintptr_t>(this) % NUMBER_OF_MUTEXES;
    mutex[mutex_id].lock();

    if (this->p_turnstile == nullptr) {  // (1)
        throw "Attempt to unlock Mutex which hasn't been locked";
    } else if (this->p_turnstile == &gate_turnstile) {  // (2)
        this->p_turnstile = nullptr;
    } else {  // (3)
        bool canDrop = this->p_turnstile->canDropAfterSpin();

        this->p_turnstile->spin();
        if (canDrop)  // (3b)
            this->p_turnstile = &gate_turnstile;
    }

    mutex[mutex_id].unlock();
}
