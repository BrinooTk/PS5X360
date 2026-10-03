// SPDX-License-Identifier: MIT
// POSIX ffs uses 1-based bit indexes and returns zero for an empty mask.
int __wrap_ffs(int value) {
  return value ? __builtin_ctz((unsigned)value) + 1 : 0;
}

// The title's working directory cannot be changed and is not searchable, so a
// relative path fails with an error other than "no such file"; std::filesystem
// then throws from calls that only ask whether a file exists.
#include <errno.h>
#include <sys/stat.h>
int __real_stat(const char* path, struct stat* status);
int __real_lstat(const char* path, struct stat* status);
int __wrap_stat(const char* path, struct stat* status) {
  const int result = __real_stat(path, status);
  if (result && path && path[0] != '/') errno = ENOENT;
  return result;
}
int __wrap_lstat(const char* path, struct stat* status) {
  const int result = __real_lstat(path, status);
  if (result && path && path[0] != '/') errno = ENOENT;
  return result;
}

// No system module exports timegm. Package (STFS/SVOD) timestamps need it.
#include <time.h>
time_t __wrap_timegm(struct tm* value) {
  long long year = value->tm_year + 1900LL + value->tm_mon / 12;
  int month = value->tm_mon % 12;
  if (month < 0) { month += 12; --year; }
  // Days since 1970-01-01 (Howard Hinnant's days_from_civil), month 0 = January.
  year -= month < 2;
  const long long era = (year >= 0 ? year : year - 399) / 400;
  const long long year_of_era = year - era * 400;
  const long long day_of_year = (153 * (month + (month > 1 ? -2 : 10)) + 2) / 5 + value->tm_mday - 1;
  const long long day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  const long long days = era * 146097 + day_of_era - 719468;
  return (time_t)(((days * 24 + value->tm_hour) * 60 + value->tm_min) * 60 + value->tm_sec);
}

/* Used by the Xenia Canary core; absent from the console's C library. */
#include <pthread.h>
#include <wchar.h>
/* Robust mutexes: a thread of the title never dies holding one of these. */
int pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robust) { (void)attr; (void)robust; return 0; }
int pthread_mutex_consistent(pthread_mutex_t* mutex) { (void)mutex; return 0; }
/* Column width of a string, for the tables the core prints in its log. */
int wcswidth(const wchar_t* text, size_t count) {
  int width = 0;
  for (size_t n = 0; n < count && text[n]; ++n) ++width;
  return width;
}
