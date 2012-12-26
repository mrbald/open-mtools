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

/*
 * modified by Aviad Rozenhek [aviadr1@gmail.com] for open-mtools
 */

#include "mtools.h"

typedef struct mdump_options {
    /* program name (from argv[0] */
    char *prog_name;

    /* program options */
    int o_quiet_lvl;
    int o_rcvbuf_size;
    int o_pause_ms;
    int o_pause_num;
    int o_verify;
    int o_stop;
    int o_tcp;
    FILE *o_output;
    char o_output_equiv_opt[1024];

    /* program positional parameters */
    unsigned long int groupaddr;
    unsigned short int groupport;
    char *bind_if;
} mdump_options;


static const char usage_str[] = "[-h] [-o ofile] [-p pause_ms[/loops]] [-Q Quiet_lvl] [-q] [-r rcvbuf_size] [-s] [-t] [-u] [-v] group port [interface]";

void usage(mdump_options* opts, char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);
	fprintf(stderr, "Usage: %s %s\n\n"
			"(use -h for detailed help)\n",
			opts->prog_name, usage_str);
}  /* usage */


void help(mdump_options* opts, char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);
	fprintf(stderr, "Usage: %s %s\n", opts->prog_name, usage_str);
	fprintf(stderr, "Where:\n"
			"  -h : help\n"
			"  -o ofile : print results to file (in addition to stdout)\n"
			"  -p pause_ms[/num] : milliseconds to pause after each receive [0: no pause]\n"
			"                      and number of loops to apply the pause [0: all loops]\n"
			"  -Q Quiet_lvl : set quiet level [0] :\n"
			"                 0 - print full datagram contents\n"
			"                 1 - print datagram summaries\n"
			"                 2 - no print per datagram (same as '-q')\n"
			"  -q : no print per datagram (same as '-Q 2')\n"
			"  -r rcvbuf_size : size (bytes) of UDP receive buffer (SO_RCVBUF) [4194304]\n"
			"                   (use 0 for system default buff size)\n"
			"  -s : stop execution when status msg received\n"
			"  -t : Use TCP (use '0.0.0.0' for group)\n"
			"  -v : verify the sequence numbers\n"
			"\n"
			"  group : multicast address to receive (required, use '0.0.0.0' for unicast)\n"
			"  port : destination port (required)\n"
			"  interface : optional IP addr of local interface (for multi-homed hosts) [INADDR_ANY]\n"
	);
}  /* help */


/* faster routine to replace inet_ntoa() (from tcpdump) */
char *intoa(unsigned int addr)
{
	register char *cp;
	register unsigned int byte;
	register int n;
	static char buf[sizeof(".xxx.xxx.xxx.xxx")];

	addr = ntohl(addr);
	// NTOHL(addr);
	cp = &buf[sizeof buf];
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return cp + 1;
}  /* intoa */


char *format_time(const struct timeval *tv)
{
	static char buff[sizeof(".xx:xx:xx.xxxxxx")];
	int min;

	unsigned int h = localtime((time_t *)&tv->tv_sec)->tm_hour;
	min = (int)(tv->tv_sec % 86400);
	sprintf(buff,"%02d:%02d:%02d.%06d",h,((int)min%3600)/60,(int)min%60,(int)tv->tv_usec);
	return buff;
}  /* format_time */


void dump(FILE *ofile, const char *buffer, int size)
{
	int i,j;
	unsigned char c;
	char textver[20];

	for (i=0;i<(size >> 4);i++) {
		for (j=0;j<16;j++) {
			c = buffer[(i << 4)+j];
			fprintf(ofile, "%02x ",c);
			textver[j] = ((c<0x20)||(c>0x7e))?'.':c;
		}
		textver[j] = 0;
		fprintf(ofile, "\t%s\n",textver);
	}
	for (i=0;i<size%16;i++) {
		c = buffer[size-size%16+i];
		fprintf(ofile, "%02x ",c);
		textver[i] = ((c<0x20)||(c>0x7e))?'.':c;
	}
	for (i=size%16;i<16;i++) {
		fprintf(ofile, "   ");
		textver[i] = ' ';
	}
	textver[i] = 0;
	fprintf(ofile, "\t%s\n",textver); fflush(ofile);
}  /* dump */


void currenttv(struct timeval *tv)
{
#if defined(_WIN32)
	struct __timeb32 tb;
	_ftime32(&tb);
	tv->tv_sec = tb.time;
	tv->tv_usec = 1000*tb.millitm;
#else
	gettimeofday(tv,NULL);
#endif /* _WIN32 */
}  /* currenttv */


int main(int argc, char **argv)
{
	int opt;
	int num_parms;
	char equiv_cmd[1024];
	char *buff;
	SOCKET listensock;
	SOCKET sock;
	socklen_t fromlen = sizeof(struct sockaddr_in);
	int default_rcvbuf_sz, cur_size, sz;
	int num_rcvd;
	struct sockaddr_in name;
	struct sockaddr_in src;
	struct ip_mreq imr;
	struct timeval tv;
	int num_sent;
	float perc_loss;
	int cur_seq;
	char *pause_slash;

    mdump_options opts;
    memset(&opts, sizeof(opts), 0);
	opts.prog_name = argv[0];

	buff = malloc(65536 + 1);  /* one extra for trailing null (if needed) */
	if (buff == NULL) { fprintf(stderr, "malloc failed\n"); exit(1); }

#if defined(_WIN32)
	{
		WSADATA wsadata;  int wsstatus;
		if ((wsstatus = WSAStartup(MAKEWORD(2,2), &wsadata)) != 0) {
			fprintf(stderr,"%s: WSA startup error - %d\n", argv[0], wsstatus);
			exit(1);
		}
	}
#else
	signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	/* get system default value for socket buffer size */
	if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
		fprintf(stderr, "ERROR: ");  perror("socket");
		exit(1);
	}
	sz = sizeof(default_rcvbuf_sz);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&default_rcvbuf_sz,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_RCVBUF");
		exit(1);
	}
	CLOSESOCKET(sock);

	/* default values for options */
	opts.o_quiet_lvl = 0;
	opts.o_rcvbuf_size = 0x400000;  /* 4MB */
	opts.o_pause_ms = 0;
	opts.o_pause_num = 0;
	opts.o_verify = 0;
	opts.o_stop = 0;
	opts.o_tcp = 0;
	opts.o_output = NULL;
	opts.o_output_equiv_opt[0] = '\0';

	/* default values for optional positional params */
	opts.bind_if = NULL;

	while ((opt = tgetopt(argc, argv, "hqQ:p:r:o:vst")) != EOF) {
		switch (opt) {
		  case 'h':
			help(&opts, NULL);  exit(0);
			break;
		  case 'q':
			opts.o_quiet_lvl = 2;
			break;
		  case 'Q':
			opts.o_quiet_lvl = atoi(toptarg);
			break;
		  case 'p':
			pause_slash = strchr(toptarg, '/');
			if (pause_slash)
				opts.o_pause_num = atoi(pause_slash+1);
			opts.o_pause_ms = atoi(toptarg);
			break;
		  case 'r':
			opts.o_rcvbuf_size = atoi(toptarg);
			if (opts.o_rcvbuf_size == 0)
				opts.o_rcvbuf_size = default_rcvbuf_sz;
			break;
		  case 'v':
			opts.o_verify = 1;
			break;
		  case 's':
			opts.o_stop = 1;
			break;
		  case 't':
			opts.o_tcp = 1;
			break;
		  case 'o':
			if (strlen(toptarg) > 1000) {
				fprintf(stderr, "ERROR: file name too long (%s)\n", toptarg);
				exit(1);
			}
			opts.o_output = fopen(toptarg, "w");
			if (opts.o_output == NULL) {
				fprintf(stderr, "ERROR: ");  perror("fopen");
				exit(1);
			}
			sprintf(opts.o_output_equiv_opt, "-o %s ", toptarg);
			break;
		  default:
			usage(&opts, "unrecognized option");
			exit(1);
			break;
		}  /* switch */
	}  /* while opt */

	num_parms = argc - toptind;

	/* handle positional parameters */
	if(num_parms < 2 > num_parms > 3) {
    	usage(&opts, "need 2-3 positional parameters");
		exit(1);
	}

	opts.groupaddr = inet_addr(argv[toptind]);
	opts.groupport = (unsigned short)atoi(argv[toptind+1]);
    if(num_parms >= 3)
        opts.bind_if  = argv[toptind+2];

	sprintf(equiv_cmd, "mdump %s-p%d -Q%d -r%d %s%s%s%s %s %s",
			opts.o_output_equiv_opt, opts.o_pause_ms, opts.o_quiet_lvl, opts.o_rcvbuf_size,
			opts.o_stop ? "-s " : "",
			opts.o_tcp ? "-t " : "",
			opts.o_verify ? "-v " : "",
			argv[toptind],argv[toptind+1],argv[toptind+2]);
	printf("Equiv cmd line: %s\n", equiv_cmd); fflush(stdout);
	if (opts.o_output) { fprintf(opts.o_output, "Equiv cmd line: %s\n", equiv_cmd); fflush(opts.o_output); }

	if (opts.o_tcp)
        if(opts.groupaddr != inet_addr("0.0.0.0")) {
		usage(&opts, "-t incompatible with non-zero multicast group");
	}

	if (opts.o_tcp) {
		if((listensock = socket(PF_INET,SOCK_STREAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
		memset((char *)&name,0,sizeof(name));
		name.sin_family = AF_INET;
		name.sin_addr.s_addr = opts.groupaddr;
		name.sin_port = htons(opts.groupport);
		if (bind(listensock,(struct sockaddr *)&name,sizeof(name)) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("bind");
			exit(1);
		}
		if(listen(listensock, 1) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("listen");
			exit(1);
		}
		if((sock = accept(listensock,(struct sockaddr *)&src,&fromlen)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("accept");
			exit(1);
		}
	} else {
		if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
	}

	if(setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(const char *)&opts.o_rcvbuf_size,
			sizeof(opts.o_rcvbuf_size)) == SOCKET_ERROR) {
		printf("WARNING: setsockopt - SO_RCVBUF\n"); fflush(stdout);
		if (opts.o_output) { fprintf(opts.o_output, "WARNING: "); perror("setsockopt - SO_RCVBUF"); fflush(opts.o_output); }
	}
	sz = sizeof(cur_size);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&cur_size,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_RCVBUF");
		exit(1);
	}
	if (cur_size < opts.o_rcvbuf_size) {
		printf("WARNING: tried to set SO_RCVBUF to %d, only got %d\n", opts.o_rcvbuf_size, cur_size); fflush(stdout);
		if (opts.o_output) { fprintf(opts.o_output, "WARNING: tried to set SO_RCVBUF to %d, only got %d\n", opts.o_rcvbuf_size, cur_size); fflush(opts.o_output); }
	}

	if (opts.groupaddr != inet_addr("0.0.0.0")) {
		memset((char *)&imr,0,sizeof(imr));
		imr.imr_multiaddr.s_addr = opts.groupaddr;
		if (opts.bind_if != NULL) {
			imr.imr_interface.s_addr = inet_addr(opts.bind_if);
		} else {
			imr.imr_interface.s_addr = htonl(INADDR_ANY);
		}
	}

	opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("setsockopt SO_REUSEADDR");
		exit(1);
	}

	if (! opts.o_tcp) {
		memset((char *)&name,0,sizeof(name));
		name.sin_family = AF_INET;
		name.sin_addr.s_addr = opts.groupaddr;
		name.sin_port = htons(opts.groupport);
		if (bind(sock,(struct sockaddr *)&name,sizeof(name)) == SOCKET_ERROR) {
			/* So OSes don't want you to bind to the m/c group. */
			name.sin_addr.s_addr = htonl(INADDR_ANY);
			if (bind(sock,(struct sockaddr *)&name, sizeof(name)) == SOCKET_ERROR) {
				fprintf(stderr, "ERROR: ");  perror("bind");
				exit(1);
			}
		}

		if (opts.groupaddr != inet_addr("0.0.0.0")) {
			if (setsockopt(sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,
						(char *)&imr,sizeof(struct ip_mreq)) == SOCKET_ERROR ) {
				fprintf(stderr, "ERROR: ");  perror("setsockopt - IP_ADD_MEMBERSHIP");
				exit(1);
			}
		}
	}

	cur_seq = 0;
	num_rcvd = 0;
	for (;;) {
		if (opts.o_tcp) {
			cur_size = recv(sock,buff,65536,0);
			if (cur_size == 0) {
				printf("EOF\n");
				if (opts.o_output) { fprintf(opts.o_output, "EOF\n"); }
				break;
			}
		} else {
			cur_size = recvfrom(sock,buff,65536,0,
					(struct sockaddr *)&src,&fromlen);
		}
		if (cur_size == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("recv");
			exit(1);
		}

		if (opts.o_quiet_lvl == 0) {  /* non-quiet: print full dump */
			currenttv(&tv);
			printf("%s %s.%d %d bytes:\n",
					format_time(&tv), inet_ntoa(src.sin_addr),
					ntohs(src.sin_port), cur_size);
			dump(stdout, buff,cur_size);
			if (opts.o_output) {
				fprintf(opts.o_output, "%s %s.%d %d bytes:\n",
						format_time(&tv), inet_ntoa(src.sin_addr),
						ntohs(src.sin_port), cur_size);
				dump(opts.o_output, buff,cur_size);
			}
		}
		if (opts.o_quiet_lvl == 1) {  /* semi-quiet: print datagram summary */
			currenttv(&tv);
			printf("%s %s.%d %d bytes\n",  /* no colon */
					format_time(&tv), inet_ntoa(src.sin_addr),
					ntohs(src.sin_port), cur_size);
			fflush(stdout);
			if (opts.o_output) {
				fprintf(opts.o_output, "%s %s.%d %d bytes\n",  /* no colon */
						format_time(&tv), inet_ntoa(src.sin_addr),
						ntohs(src.sin_port), cur_size);
				fflush(opts.o_output);
			}
		}

		if (cur_size > 5 && memcmp(buff, "echo ", 5) == 0) {
			/* echo command */
			buff[cur_size] = '\0';  /* guarantee trailing null */
			if (buff[cur_size - 1] == '\n')
				buff[cur_size - 1] = '\0';  /* strip trailing nl */
			printf("%s\n", buff); fflush(stdout);
			if (opts.o_output) { fprintf(opts.o_output, "%s\n", buff); fflush(opts.o_output); }

			/* reset stats */
			num_rcvd = 0;
			cur_seq = 0;
		}
		else if (cur_size > 5 && memcmp(buff, "stat ", 5) == 0) {
			/* when sender tells us to, calc and print stats */
			buff[cur_size] = '\0';  /* guarantee trailing null */
			/* 'stat' message contains num msgs sent */
			num_sent = atoi(&buff[5]);
			perc_loss = (float)(num_sent - num_rcvd) * 100.0f / (float)num_sent;
			printf("%d msgs sent, %d received (not including 'stat')\n", num_sent, num_rcvd);
			printf("%f%% loss\n", perc_loss);
			fflush(stdout);
			if (opts.o_output) {
				fprintf(opts.o_output, "%d msgs sent, %d received (not including 'stat')\n", num_sent, num_rcvd);
				fprintf(opts.o_output, "%f%% loss\n", perc_loss);
				fflush(opts.o_output);
			}

			if (opts.o_stop)
				exit(0);

			/* reset stats */
			num_rcvd = 0;
			cur_seq = 0;
		}
		else {  /* not a cmd */
			if (opts.o_pause_ms > 0 && ( (opts.o_pause_num > 0 && num_rcvd < opts.o_pause_num)
									|| (opts.o_pause_num == 0) )) {
				SLEEP_MSEC(opts.o_pause_ms);
			}

			if (opts.o_verify) {
				buff[cur_size] = '\0';  /* guarantee trailing null */
				if (cur_seq != strtol(&buff[8], NULL, 16)) {
					printf("Expected seq %x (hex), got %s\n", cur_seq, &buff[8]);
					fflush(stdout);
					/* resyncronize sequence numbers in case there is loss */
					cur_seq = strtol(&buff[8], NULL, 16);
				}
			}

			++num_rcvd;
			++cur_seq;
		}
	}  /* for ;; */

	CLOSESOCKET(sock);
	if (opts.o_tcp)
		CLOSESOCKET(listensock);

	exit(0);
}  /* main */


