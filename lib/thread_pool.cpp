#include "thread_pool.h"
#include <cassert>
#include <cstddef>
#include <pthread.h>


static void *worker(void *arg) {
 ThreadPool *tp = (ThreadPool *)arg;
 while (true) {
    pthread_mutex_lock(&tp->mu);

    // Wait for a job
    while (tp->queue.empty()) {
      pthread_cond_wait(&tp->not_empty, &tp->mu);
    }

    Work w = tp->queue.front();
    tp->queue.pop_front();
    pthread_mutex_unlock(&tp->mu);

    w.f(w.arg);
  }
 return NULL;
}

void thread_pool_init(ThreadPool *tp, size_t num_threads) {
  assert(num_threads > 0);

  int rv = pthread_mutex_init(&tp->mu, NULL);
  assert(rv == 0);
  rv = pthread_cond_init(&tp->not_empty, NULL);
  assert(rv == 0);

  tp->threads.resize(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    int v = pthread_create(&tp->threads[i], NULL, &worker, tp);
    assert(v == 0);
  }
}
