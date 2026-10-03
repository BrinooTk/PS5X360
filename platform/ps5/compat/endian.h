// SPDX-License-Identifier: MIT
// <endian.h> as Linux has it, for the FreeBSD-derived PS5 SDK.
#pragma once
#include <sys/endian.h>
#ifndef __BYTE_ORDER
#define __BYTE_ORDER _BYTE_ORDER
#define __BIG_ENDIAN _BIG_ENDIAN
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#endif
