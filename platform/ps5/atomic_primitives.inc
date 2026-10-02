// SPDX-License-Identifier: MIT
// Included inside namespace xe by the generated upstream atomic.h overlay.
// Match the interlocked API's return values and sequential consistency.
inline int32_t atomic_inc(volatile int32_t* value) {
  return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline int32_t atomic_exchange(int32_t next, volatile int32_t* value) {
  return __atomic_exchange_n(value, next, __ATOMIC_SEQ_CST);
}
inline int64_t atomic_exchange(int64_t next, volatile int64_t* value) {
  return __atomic_exchange_n(value, next, __ATOMIC_SEQ_CST);
}
inline int32_t atomic_exchange_add(int32_t amount, volatile int32_t* value) {
  return __atomic_fetch_add(value, amount, __ATOMIC_SEQ_CST);
}
inline int64_t atomic_exchange_add(int64_t amount, volatile int64_t* value) {
  return __atomic_fetch_add(value, amount, __ATOMIC_SEQ_CST);
}
inline bool atomic_cas(int32_t expected, int32_t next, volatile int32_t* value) {
  return __atomic_compare_exchange_n(value, &expected, next, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
inline bool atomic_cas(int64_t expected, int64_t next, volatile int64_t* value) {
  return __atomic_compare_exchange_n(value, &expected, next, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
