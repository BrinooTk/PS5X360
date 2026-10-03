// SPDX-License-Identifier: MIT
// Boot breadcrumbs and a fatal-signal report for the native title.
#pragma once
#include <cstddef>
namespace xbox360ps5 {
// Development log stream on a TCP port (platform/ps5/net_log.cpp).
bool StartNetLog(unsigned short port);
void NetLog(const char* text, size_t size);
void DrainNetLog();
// Opens `path` for the report and installs handlers for fatal signals. Call
// before the engine installs its own handlers so unhandled faults chain here.
void InstallCrashReport(const char* path);
// Logs where a thread (a pthread_t) is: its instruction and the return
// addresses on its stack. For a title that stopped without crashing.
void ProbeThread(void* thread, const char* name);
// One synchronous line in the kernel log and in the report file.
void Stage(const char* text);
// Logs the process address-space layout and memory budgets through Stage.
void ReportPlatformMemory();
}
