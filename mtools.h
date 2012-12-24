#ifndef MTOOLS_MTOOLS_H
#define MTOOLS_MTOOLS_H

/* mdump.c */
/*   Program to dump the contents of all datagrams arriving on a specified
 * multicast address and port.  The dump gives both the hex and ASCII
 * equivalents of the datagram payload.
 * See https://community.informatica.com/solutions/1470 for more info
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

#include <stdio.h>
#include <stdlib.h>

/* Many of the following definitions are intended to make it easier to write
 * portable code between windows and unix. */

/* use our own form of getopt */
extern int toptind;
extern int toptreset;
extern char *toptarg;
int tgetopt(int nargc, char * const *nargv, const char *ostr);

#if defined(_MSC_VER)
// Windows-only includes
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#define SLEEP_SEC(s) Sleep((s) * 1000)
#define SLEEP_MSEC(s) Sleep(s)
#define ERRNO GetLastError()
#define CLOSESOCKET closesocket
#define TLONGLONG signed __int64
#define strdup _strdup
#pragma comment(lib, "Ws2_32.lib")


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
#define ERRNO errno
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define TLONGLONG signed long long
#endif

#if defined(_WIN32)
#   include <ws2tcpip.h>
#   include <sys\types.h>
#   include <sys\timeb.h>
#   define perror(x) fprintf(stderr,"%s: %d\n",x,GetLastError())
#else
#   include <sys/time.h>
#endif

#include <string.h>
#include <time.h>

#define MAXPDU 65536

#endif