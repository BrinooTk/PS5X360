// SPDX-License-Identifier: MIT
// POSIX ffs uses 1-based bit indexes and returns zero for an empty mask.
int __wrap_ffs(int value) {
  return value ? __builtin_ctz((unsigned)value) + 1 : 0;
}
