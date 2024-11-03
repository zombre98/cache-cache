#include "server.h"

uint64_t get_monotonic_usec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

static uint32_t next_timer_ms() {
  uint64_t now_us = get_monotonic_usec();
  uint64_t next_us = -1;

  // idle timers
  if (!dlist_empty(&g_data.idle_list)) {
    Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
    next_us = next->idle_start + IDLE_TIMEOUT_MS * 1000;
  }

  // ttl timers
  if (!g_data.heap.empty() && g_data.heap[0].val < next_us) {
    next_us = g_data.heap[0].val;
  }

  if (next_us == -1) {
    return 1000;
  }
  if (next_us <= now_us) {
    return 0;
  }

  return (uint32_t)((next_us - now_us) / 1000);
}

