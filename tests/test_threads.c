/* Tests for threads.c: the max_connections cap enforced by create_thread()
 * and the slot released by decrement_thread_count(). threads.o is excluded
 * from this binary's link. Worker threads block on a semaphore so the
 * number of live threads is known exactly, independent of scheduling. */

#include "../threads.c"

#include <semaphore.h>

#include "test.h"

static sem_t release;
static sem_t started;

static void *worker(void *arg)
{
    (void)arg;
    sem_post(&started);
    sem_wait(&release);
    decrement_thread_count();
    return NULL;
}

/* init_threads() initialises a mutex: call it once per process only */
static void setup(int max)
{
    static int initialised = 0;
    if (!initialised)
    {
        init_threads();
        initialised = 1;
    }
    sem_init(&release, 0, 0);
    sem_init(&started, 0, 0);
    max_connections = max;
}

static void wait_for_thread_count(int expected)
{
    for (int i = 0; i < 200; i++)
    {
        pthread_mutex_lock(&lock);
        int n = active_threads;
        pthread_mutex_unlock(&lock);
        if (n == expected)
            return;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

TEST(create_thread_refuses_past_max_connections_then_recovers)
{
    setup(2);

    ASSERT_INT_EQ(create_thread(worker, NULL), 0);
    ASSERT_INT_EQ(create_thread(worker, NULL), 0);
    sem_wait(&started);
    sem_wait(&started);

    ASSERT_INT_EQ(create_thread(worker, NULL), -1);
    ASSERT_INT_EQ(active_threads, 2);

    /* let one worker finish: its slot must become available again */
    sem_post(&release);
    wait_for_thread_count(1);
    ASSERT_INT_EQ(active_threads, 1);

    ASSERT_INT_EQ(create_thread(worker, NULL), 0);
    sem_wait(&started);
    ASSERT_INT_EQ(create_thread(worker, NULL), -1);

    sem_post(&release);
    sem_post(&release);
    wait_for_thread_count(0);
    ASSERT_INT_EQ(active_threads, 0);

    max_connections = 10;
}

TEST(max_connections_of_one_allows_exactly_one_thread)
{
    setup(1);

    ASSERT_INT_EQ(create_thread(worker, NULL), 0);
    sem_wait(&started);
    ASSERT_INT_EQ(create_thread(worker, NULL), -1);

    sem_post(&release);
    wait_for_thread_count(0);
    ASSERT_INT_EQ(active_threads, 0);

    max_connections = 10;
}

TEST(wait_for_threads_times_out_while_busy_then_returns_zero)
{
    setup(2);

    ASSERT_INT_EQ(create_thread(worker, NULL), 0);
    sem_wait(&started);

    ASSERT_INT_EQ(wait_for_threads(0), 1);

    sem_post(&release);
    ASSERT_INT_EQ(wait_for_threads(5), 0);

    max_connections = 10;
}
