#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <netdb.h>
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_NETDB_H 1
#define HAVE_LINGER 1
#define HAVE_SOCKADDR_STORAGE 1
#define HAVE_SOCKADDR_IN6 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_UIO_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_POLL_H 1
#define HAVE_TIME_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#include <netinet/in.h>
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif
