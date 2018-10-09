#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct cv *intersection_cv;
static struct lock *intersection_lock;
static struct array *intersection_vehicles;

typedef struct Vehicle {
    Direction origin;
    Direction destination;
} Vehicle;

// Prototypes
// Can the vehicle enter the intersection?
bool can_enter(Vehicle*);
// Will the vehicle a crash with vehicle b?
bool enterable(Vehicle*, Vehicle*);
// Is this a right turn?
bool right_turn(Vehicle*);

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
    intersection_cv = cv_create("intersection_cv");
    intersection_lock = lock_create("intersection_lock");
    intersection_vehicles = array_create();

    KASSERT(intersection_cv != NULL);
    KASSERT(intersection_lock != NULL);
    KASSERT(intersection_vehicles != NULL);
    return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
    KASSERT(intersection_cv != NULL);
    KASSERT(intersection_lock != NULL);
    KASSERT(intersection_vehicles != NULL);

    cv_destroy(intersection_cv);
    lock_destroy(intersection_lock);
    array_destroy(intersection_vehicles);
    return;
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{

    KASSERT(intersection_cv != NULL);
    KASSERT(intersection_lock != NULL);
    KASSERT(intersection_vehicles != NULL);

    lock_acquire(intersection_lock);

    Vehicle *v = kmalloc(sizeof(struct Vehicle));
    v->origin = origin;
    v->destination = destination;

    // Block if vehicle cannot enter the intersection
    while(!can_enter(v)) {
        cv_wait(intersection_cv, intersection_lock);
    }

    // Vehicle passes the can_enter test, add car to intersection
    array_add(intersection_vehicles, v, NULL);

    lock_release(intersection_lock);
    return;
}

bool
can_enter(Vehicle *v)
{
    unsigned int intersection_vehicles_size = array_num(intersection_vehicles);

    for (unsigned int i = 0; i < intersection_vehicles_size; i++) {
        if (!enterable(v, array_get(intersection_vehicles, i))) {
            return false;
        }
    }
    return true;
}

bool
enterable(Vehicle *a, Vehicle *b)
{
    KASSERT(a != NULL);
    KASSERT(b != NULL);
    return ((a->origin == b->origin) ||
            (a->origin == b->destination && a->destination == b->origin) ||
            (a->destination != b->destination && (right_turn(a) || right_turn(b))));
}

bool
right_turn(Vehicle *v)
{
    KASSERT(v != NULL);
    return ((v->origin == east && v->destination == north) ||
            (v->origin == south && v->destination == east) ||
            (v->origin == north && v->destination == west) ||
            (v->origin == west && v->destination == south));
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{

    KASSERT(intersection_cv != NULL);
    KASSERT(intersection_lock != NULL);
    KASSERT(intersection_vehicles != NULL);

    lock_acquire(intersection_lock);

    unsigned int intersection_vehicles_size = array_num(intersection_vehicles);

    // Remove the car from the intersection
    for (unsigned int i = 0; i < intersection_vehicles_size; i++) {
        Vehicle *v = array_get(intersection_vehicles, i);
        if (v->origin == origin && v->destination == destination) {
            array_remove(intersection_vehicles, i);
            // Wake blocked threads (cars)
            cv_broadcast(intersection_cv, intersection_lock);
            break;
        }
    }

    lock_release(intersection_lock);
}
