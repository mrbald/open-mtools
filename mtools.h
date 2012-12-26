#ifndef MTOOLS_MTOOLS_H
#define MTOOLS_MTOOLS_H

/*
 *
 * Author: J.P.Knight@lut.ac.uk (heavily modified by 29West/Informatica)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.
 *
 * Note: this program is based on the sd_listen program by Tom Pusateri
 * (pusateri@cs.duke.edu) and developed by Jon Knight (J.P.Knight@lut.ac.uk).
 *
  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES
  EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF
  NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
  PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE
  UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES,
  BE LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR
  INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE
  TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
  THE LIKELIHOOD OF SUCH DAMAGES.
 */

/*
 * modified by Aviad Rozenhek [aviadr1@gmail.com] for open-mtools
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>


/* Many of the following definitions are intended to make it easier to write
 * portable code between windows and unix. */

/* use our own form of getopt */
extern int toptind;
extern int toptreset;
extern char *toptarg;
extern int tgetopt(int nargc, char * const *nargv, const char *ostr);

#define mprintf(opts, format, ...)                                \
    fprintf(stderr, format, __VA_ARGS__);  fflush(stderr);         \
    if(opts && opts->o_output) { fprintf(opts->o_output, format, __VA_ARGS__); fflush(opts->o_output); }


#if defined(_MSC_VER)
// Windows-only includes
#define HAVE_WINSOCK2_H 1

#define SLEEP_SEC(s) Sleep((s) * 1000)
#define SLEEP_MSEC(s) Sleep(s)
#define CLOSESOCKET closesocket
#define TLONGLONG signed __int64


#else
// Unix-only includes
#define HAVE_PTHREAD_H
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#define SLEEP_SEC(s) sleep(s)
#define SLEEP_MSEC(s) usleep((s) * 1000)
#define CLOSESOCKET close
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define TLONGLONG signed long long
#endif

#if defined(_WIN32)
#   include <ws2tcpip.h>
#   include <sys\types.h>
#   include <sys\timeb.h>
#   define perror(opts, x) mprintf(opts, "%s: %d\n",x,GetLastError())
#else
#   include <sys/time.h>
#endif

#include <string.h>
#include <time.h>

#define MAXPDU 65536

#if HAVE_WINSOCK2_H
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT       WSAETIMEDOUT
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED    WSAECONNREFUSED
#endif
#ifndef EINPROGRESS
#define EINPROGRESS     WSAEINPROGRESS
#endif

#define getsockopt(a, b, c, d, e) getsockopt(a, b, c, (char*) d, e)
#define setsockopt(a, b, c, d, e) setsockopt(a, b, c, (const char*) d, e)

#pragma comment(lib, "Ws2_32.lib")

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#endif


#endif
