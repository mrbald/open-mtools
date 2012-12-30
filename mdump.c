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

#define FF_ARRAY_ELEMS(a)   (sizeof(a) / sizeof((a)[0]))

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
    FILE *O_bin_output;
    char o_output_equiv_opt[1024], O_dumpfile_equiv_opt[1024];

    /* program positional parameters */
    char* groupaddr_name;
    unsigned long int groupaddr;
    unsigned short int groupport;
    char *bind_if;

    /* igmp v3 support */
    char* igmpv3_sources_string;
    int igmpv3_sources_num;
    char* igmpv3_sources[32];
    int igmpv3_include;

    /* state */
    struct sockaddr_storage addr;
    socklen_t addrlen;

    /* tcp state */
    SOCKET tcp_listen_sock;
    struct sockaddr_storage tcp_sock_src_addr;
    int tcp_sock_src_addr_len;

} mdump_options;


static const char usage_str[] = "[-h] [-o ofile] [-O dumpfile][-p pause_ms[/loops]] [-Q Quiet_lvl] [-q] [-r rcvbuf_size] [-s] [-t] [-u] [-v] group port [interface] [igmpv3]";

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
            "  -O dumpfile : dumps packets to a binary file without text formatting\n"
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
            "  igmpv3 : optional list of inclusive or exclusive igmpv3 sources\n"
            "           an example igmpv3 inclusive source list is +192.168.64.32,192.168.64.40\n"
            "           an example igmpv3 exclusive source list is -80.82.20.10\n"
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

    time_t tv_sec = tv->tv_sec;
	unsigned int h = localtime(&tv_sec)->tm_hour;
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
	fprintf(ofile, "\t%s\n",textver); 
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


static int parse_igmpv3_sources(const char* sources, char* sources_arr[], int sources_arr_size, int* is_include)
{
    int num_sources;

    if(!sources)
        return 0;

    if(!is_include)
        return -1;

    while(isspace(sources[0]))
        sources++;

    if(!sources[0])
         return 0;

    if(sources[0] == '+')
        *is_include = 1;
    else if(sources[0] == '-')
        *is_include = 0;
    else 
        return -1;

    num_sources = 0;
    ++sources;

    while (1) {
        char *next = strchr(sources, ',');
        if (next)
            *next = '\0';
        sources_arr[num_sources] = strdup(sources);
        if (!sources_arr[num_sources])
            return -1;
        sources = next + 1;
        num_sources++;
        if (num_sources >= sources_arr_size || !next)
            break;
    }

    return num_sources;
}

static void initialize_basic_socket(mdump_options* opts, SOCKET sock)
{
    int opt;
	int cur_size, sz;

	if(setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(const char *)&opts->o_rcvbuf_size,
			sizeof(opts->o_rcvbuf_size)) == SOCKET_ERROR) {
		mprintf((opts), "WARNING: setsockopt - SO_RCVBUF\n");
		perror((opts), "setsockopt - SO_RCVBUF");
	}
	sz = sizeof(cur_size);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&cur_size,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		mprintf((opts), "ERROR: ");
        perror((opts), "getsockopt - SO_RCVBUF");
		exit(1);
	}
	if (cur_size < opts->o_rcvbuf_size) {
		mprintf((opts), "WARNING: tried to set SO_RCVBUF to %d, only got %d\n", opts->o_rcvbuf_size, cur_size);
	}
    	
	opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
        mprintf((opts), "ERROR: ");
        perror((opts), "setsockopt SO_REUSEADDR");
		exit(1);
	}
}

static SOCKET initliaze_tcp_socket(mdump_options* opts)
{
    SOCKET sock;
    struct sockaddr_in name;

	if((opts->tcp_listen_sock = socket(PF_INET,SOCK_STREAM,0)) == INVALID_SOCKET) {
		mprintf(opts, "ERROR: ");  perror(opts, "socket");
		exit(1);
	}


    memset((char *)&name,0,sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = opts->groupaddr;
    name.sin_port = htons(opts->groupport);
    memcpy(&opts->addr, &name, sizeof(name));
    opts->addrlen = sizeof(name);

	if (bind(opts->tcp_listen_sock, (struct sockaddr*) &opts->addr, opts->addrlen) == SOCKET_ERROR) {
		mprintf((opts), "ERROR: ");  perror(opts, "bind");
		exit(1);
	}
	if(listen(opts->tcp_listen_sock, 1) == SOCKET_ERROR) {
		mprintf((opts), "ERROR: ");  perror(opts, "listen");
		exit(1);
	}
	if((sock = accept(opts->tcp_listen_sock, (struct sockaddr *) &opts->tcp_sock_src_addr, &opts->tcp_sock_src_addr_len)) == INVALID_SOCKET) {
		mprintf((opts), "ERROR: ");  perror(opts, "accept");
		exit(1);
	}

    initialize_basic_socket(opts, sock);
    return sock;
}

static int udp_socket_create(
    const char* grouphost, int groupport, 
    struct sockaddr_storage *addr,
    socklen_t *addr_len)
{
    int udp_fd = -1;
    struct addrinfo *res0 = NULL, *res = NULL;
    int family = AF_UNSPEC;

    res0 = udp_resolve_host(NULL, groupport,
                            SOCK_DGRAM, family, AI_PASSIVE);
    if (res0 == 0)
        goto fail;
    for (res = res0; res; res=res->ai_next) {
        udp_fd = socket(res->ai_family, SOCK_DGRAM, 0);
        if (udp_fd != -1) break;
        //perror("socket");
    }

    if (udp_fd < 0)
        goto fail;

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res0);

    return udp_fd;

 fail:
    if (udp_fd >= 0)
        closesocket(udp_fd);
    if(res0)
        freeaddrinfo(res0);
    return -1;
}

static SOCKET initliaze_udp_socket(mdump_options* opts)
{
    SOCKET sock;
    struct sockaddr_in name;

	if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
	    mprintf((opts), "ERROR: ");  perror((opts), "socket");
		exit(1);
	}

    initialize_basic_socket(opts, sock);

    memset((char *)&name,0,sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = opts->groupaddr;
    name.sin_port = htons(opts->groupport);
    memcpy(&opts->addr, &name, sizeof(name));
    opts->addrlen = sizeof(name);

	if (bind(sock,(struct sockaddr *)&name, sizeof(name)) == SOCKET_ERROR) {
		/* So OSes don't want you to bind to the m/c group. */
		name.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(sock,(struct sockaddr *)&name, sizeof(name)) == SOCKET_ERROR) {
			mprintf((opts), "ERROR: ");  
            perror((opts), "bind");
			exit(1);
		}
	}

    if (opts->igmpv3_sources_num == 0 || !opts->igmpv3_include) {
        if (udp_join_multicast_group(sock, (struct sockaddr *) &opts->addr /*, (opts->bind_if? inet_addr(opts->bind_if) : INADDR_ANY)*/) < 0) {
            perror((opts), "udp_join_multicast_group");
            exit(1);
        }

        if (opts->igmpv3_sources_num) {
            if (udp_set_multicast_sources(sock, (struct sockaddr *) &opts->addr, opts->addrlen, opts->igmpv3_sources, opts->igmpv3_sources_num, 0) < 0) {
                perror((opts), "udp_set_multicast_sources");
                exit(1);
            }
        }
    } else if (opts->igmpv3_include && opts->igmpv3_sources_num) {
        if (udp_set_multicast_sources(sock, (struct sockaddr *) &opts->addr, opts->addrlen, opts->igmpv3_sources, opts->igmpv3_sources_num, 1) < 0) {
            perror((opts), "udp_set_multicast_sources");
            exit(1);
        }
    } else {
        mprintf((opts), "invalid udp settings: inclusive multicast but no sources given");
        exit(1);
    }

    return sock;
}


static SOCKET initialize_socket(mdump_options* opts)
{
    SOCKET sock;

	if (opts->o_tcp) {
        sock = initliaze_tcp_socket(opts);
	} else {
        sock = initliaze_udp_socket(opts);
	}

    return sock;
}

int main(int argc, char **argv)
{
	int opt;
	int num_parms;
	char equiv_cmd[1024];
	char *buff;
	SOCKET sock;
	int default_rcvbuf_sz, cur_size, sz;
	int num_rcvd;
	struct timeval tv;
	int num_sent;
	float perc_loss;
	int cur_seq;
	char *pause_slash;
    struct sockaddr_storage src;
    mdump_options opts;

    memset(&opts, 0, sizeof(opts));
	opts.prog_name = argv[0];

	buff = malloc(65536 + 1);  /* one extra for trailing null (if needed) */
	if (buff == NULL) { mprintf((&opts), "malloc failed\n"); exit(1); }

#if defined(_WIN32)
	{
		WSADATA wsadata;  int wsstatus;
		if ((wsstatus = WSAStartup(MAKEWORD(2,2), &wsadata)) != 0) {
			mprintf((&opts),"%s: WSA startup error - %d\n", argv[0], wsstatus);
			exit(1);
		}
	}
#else
	signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	/* get system default value for socket buffer size */
	if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
		mprintf((&opts), "ERROR: ");  perror((&opts), "socket");
		exit(1);
	}
	sz = sizeof(default_rcvbuf_sz);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&default_rcvbuf_sz,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		mprintf((&opts), "ERROR: ");  perror((&opts), "getsockopt - SO_RCVBUF");
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

	while ((opt = tgetopt(argc, argv, "hqQ:p:r:o:O:vst")) != EOF) {
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
				mprintf((&opts), "ERROR: file name too long (%s)\n", toptarg);
				exit(1);
			}
			opts.o_output = fopen(toptarg, "w");
			if (opts.o_output == NULL) {
				mprintf((&opts), "ERROR: ");  perror((&opts), "fopen");
				exit(1);
			}
			sprintf(opts.o_output_equiv_opt, "-o %s ", toptarg);
			break;
          case 'O':
			if (strlen(toptarg) > 1000) {
				mprintf((&opts), "ERROR: file name too long (%s)\n", toptarg);
				exit(1);
			}
			opts.O_bin_output = fopen(toptarg, "wb");
			if (opts.O_bin_output == NULL) {
				mprintf((&opts), "ERROR: ");  perror((&opts), "fopen");
				exit(1);
			}
			sprintf(opts.O_dumpfile_equiv_opt, "-O %s ", toptarg);
			break;

		  default:
			usage(&opts, "unrecognized option");
			exit(1);
			break;
		}  /* switch */
	}  /* while opt */

	num_parms = argc - toptind;

	/* handle positional parameters */
	if(num_parms < 2 || num_parms > 4) {
    	usage(&opts, "need 2-4 positional parameters");
		exit(1);
	}

    opts.groupaddr_name = argv[toptind];
	opts.groupaddr = inet_addr(opts.groupaddr_name);
	opts.groupport = (unsigned short)atoi(argv[toptind+1]);
    if(num_parms >= 3)
        opts.bind_if  = argv[toptind+2];
    
    if(num_parms >= 4) {
        opts.igmpv3_sources_string = argv[toptind+3];
        opts.igmpv3_sources_num = parse_igmpv3_sources(opts.igmpv3_sources_string, opts.igmpv3_sources, FF_ARRAY_ELEMS(opts.igmpv3_sources), &opts.igmpv3_include);
        if(opts.igmpv3_sources_num < 0) {
            mprintf((&opts), "bad igmpv3 sources string");
            exit(1);
        }
    }

	sprintf(equiv_cmd, "mdump %s%s-p%d -Q%d -r%d %s%s%s%s %s %s %s",
			opts.o_output_equiv_opt, opts.O_dumpfile_equiv_opt, opts.o_pause_ms, opts.o_quiet_lvl, opts.o_rcvbuf_size,
			opts.o_stop ? "-s " : "",
			opts.o_tcp ? "-t " : "",
			opts.o_verify ? "-v " : "",
			argv[toptind],
            argv[toptind+1],
            opts.bind_if,
            opts.igmpv3_sources_string
            );
    mprintf((&opts), "Equiv cmd line: %s\n", equiv_cmd);

	if (opts.o_tcp)
        if(opts.groupaddr != inet_addr("0.0.0.0") || opts.igmpv3_sources != NULL) {
		usage(&opts, "-t incompatible with non-zero multicast group");
	}

    sock = initialize_socket(&opts);

	cur_seq = 0;
	num_rcvd = 0;
	for (;;) {
		if (opts.o_tcp) {
			cur_size = recv(sock,buff,65536,0);
			if (cur_size == 0) {
				mprintf((&opts), "EOF\n");				break;
			}
		} else {
            int fromlen = sizeof(src);
			cur_size = recvfrom(sock,buff,65536,0, (struct sockaddr*) &src, &fromlen);
		}
		if (cur_size == SOCKET_ERROR) {
			mprintf((&opts), "ERROR: ");  
            perror((&opts), "recv");
			exit(1);
		}

		if (opts.o_quiet_lvl == 0) {  /* non-quiet: print full dump */
			currenttv(&tv);
			mprintf((&opts),"%s %s.%d %d bytes:\n",
					format_time(&tv), 
                    inet_ntoa(((struct sockaddr_in*)&opts.addr)->sin_addr),
					ntohs(((struct sockaddr_in*)&opts.addr)->sin_port), 
                    cur_size
                    );
			dump(stdout, buff,cur_size);
			if (opts.o_output) {
				dump(opts.o_output, buff,cur_size);
			}
		}
		if (opts.o_quiet_lvl == 1) {  /* semi-quiet: print datagram summary */
			currenttv(&tv);
			mprintf((&opts),"%s %s.%d %d bytes\n",  /* no colon */
					format_time(&tv), inet_ntoa(((struct sockaddr_in*)&opts.addr)->sin_addr),
					ntohs(((struct sockaddr_in*)&opts.addr)->sin_port), cur_size);
		}

        if(opts.O_bin_output) { /* binary dump of packets, useful for MPEG-TS */
			fwrite(buff, cur_size, 1, opts.O_bin_output);			
		}
		if (cur_size > 5 && memcmp(buff, "echo ", 5) == 0) {
			/* echo command */
			buff[cur_size] = '\0';  /* guarantee trailing null */
			if (buff[cur_size - 1] == '\n')
				buff[cur_size - 1] = '\0';  /* strip trailing nl */
			mprintf((&opts),"%s\n", buff);

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
			mprintf((&opts),"%d msgs sent, %d received (not including 'stat')\n", num_sent, num_rcvd);
			mprintf((&opts),"%f%% loss\n", perc_loss);

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
					mprintf((&opts),"Expected seq %x (hex), got %s\n", cur_seq, &buff[8]);
					/* resyncronize sequence numbers in case there is loss */
					cur_seq = strtol(&buff[8], NULL, 16);
				}
			}

			++num_rcvd;
			++cur_seq;
		}
	}  /* for ;; */

	CLOSESOCKET(sock);
	if (opts.o_tcp) {
		CLOSESOCKET(opts.tcp_listen_sock);
    }

	exit(0);
}  /* main */



