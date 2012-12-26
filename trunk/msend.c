/* msend.c */
/*   Program to send multicast packets in flexible ways to test multicast
 * networks.
 * See https://community.informatica.com/solutions/1470 for more info.
 *
 * Authors: The fine folks at 29West/Informatica
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.
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


/* Many of the following definitions are intended to make it easier to write
 * portable code between windows and unix. */

/* program name (from argv[0] */
char *prog_name = "xxx";

/* program options (see main() for defaults) */
int o_burst_count;
int o_decimal;
int o_loops;
int o_msg_len;
int o_num_bursts;
int o_pause;
char *o_Payload = NULL;
int o_quiet;  char *o_quiet_equiv_opt;
int o_stat_pause;
int o_Sndbuf_size;
int o_tcp;
int o_unicast_udp;

#define MIN_DEFAULT_SENDBUF_SIZE 65536

/* program positional parameters */
unsigned long groupaddr;
unsigned short groupport;
unsigned char ttlvar;
char *bind_if;

char usage_str[] = "[-1|2|3|4|5] [-b burst_count] [-d] [-h] [-l loops] [-m msg_len] [-n num_bursts] [-P payload] [-p pause] [-q] [-S Sndbuf_size] [-s stat_pause] [-t | -u] group port [ttl] [interface]";
void usage(char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);

	fprintf(stderr, "Usage: %s %s\n\n"
			"(use -h for detailed help)\n",
			prog_name, usage_str);
}  /* usage */


void help(char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);
	fprintf(stderr, "Usage: %s %s\n", prog_name, usage_str);
	fprintf(stderr, "Where:\n"
			"  -1 : pre-load opts for basic connectivity (1 short msg per sec for 10 min)\n"
			"  -2 : pre-load opts for long msg len (1 5k msg each sec for 5 seconds)\n"
			"  -3 : pre-load opts for moderate load (bursts of 100 8K msgs for 5 seconds)\n"
			"  -4 : pre-load opts for heavy load (1 burst of 5000 short msgs)\n"
			"  -5 : pre-load opts for VERY heavy load (1 burst of 50,000 800-byte msgs)\n"
			"  -b burst_count : number of messages per burst [1]\n"
			"  -d : decimal numbers in messages [hex])\n"
			"  -h : help\n"
			"  -l loops : number of times to loop test [1]\n"
			"  -m msg_len : length of each message (0=use length of sequence number) [0]\n"
			"  -n num_bursts : number of bursts to send (0=infinite) [0]\n"
			"  -P payload : hex digits for message content (implicit -m)\n"
			"  -p pause : pause (milliseconds) between bursts [1000]\n"
			"  -q : loop more quietly (can use '-qq' for complete silence)\n"
			"  -S Sndbuf_size : size (bytes) of UDP send buffer (SO_SNDBUF) [65536]\n"
			"                   (use 0 for system default buff size)\n"
			"  -s stat_pause : pause (milliseconds) before sending stat msg (0=no stat) [0]\n"
			"  -t : tcp ('group' becomes destination IP) [multicast]\n"
			"  -u : unicast udp ('group' becomes destination IP) [multicast]\n"
			"\n"
			"  group : multicast group or IP address to send to (required)\n"
			"  port : destination port (required)\n"
			"  ttl : time-to-live (limits transition through routers) [2]\n"
			"  interface : optional IP addr of local interface (for multi-homed hosts)\n"
	);
}  /* help */


int main(int argc, char **argv)
{
	int opt;
	int o_Sndbuf_set;
	int num_parms;
	int test_num;
	char equiv_cmd[1024];
	char *buff;
	char cmdbuf[512];
	SOCKET sock;
	struct sockaddr_in sin;
	struct timeval tv = {1,0};
	unsigned int wttl;
	int burst_num;  /* number of bursts so far */
	int msg_num;  /* number of messages so far */
	int send_len;  /* size of datagram to send */
	int sz, default_sndbuf_sz, check_size, i;
	int send_rtn;
#if defined(_WIN32)
	unsigned long int iface_in;
#else
	struct in_addr iface_in;
#endif /* _WIN32 */

	prog_name = argv[0];

	buff = malloc(65536);
	if (buff == NULL) { fprintf(stderr, "malloc failed\n"); exit(1); }
	memset(buff, 0, 65536);

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

	/* Find out what the system default socket buffer size is. */
	if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
		fprintf(stderr, "ERROR: ");  perror("socket");
		exit(1);
	}
	sz = sizeof(default_sndbuf_sz);
	if (getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char *)&default_sndbuf_sz,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_SNDBUF");
		exit(1);
	}
	CLOSESOCKET(sock);

	/* default option values (declared as module globals) */
	o_burst_count = 1;  /* 1 message per "burst" */
	o_decimal = 0;  /* hex numbers in message text */
	o_loops = 1;  /* number of time to loop test */
	o_msg_len = 0;  /* variable */
	o_num_bursts = 0;  /* infinite */
	o_pause = 1000;  /* seconds between bursts */
	o_Payload = NULL;
	o_quiet = 0;  o_quiet_equiv_opt = " ";
	o_stat_pause = 0;  /* no stat message */
	o_Sndbuf_size = MIN_DEFAULT_SENDBUF_SIZE;  o_Sndbuf_set = 0;
	o_tcp = 0;  /* 0 for udp (multicast or unicast) */
	o_unicast_udp = 0;  /* 0 for multicast or tcp */

	/* default values for optional positional parms. */
	ttlvar = 2;
	bind_if = NULL;

	test_num = -1;
	while ((opt = tgetopt(argc, argv, "12345b:dhl:m:n:p:P:qs:S:tu")) != EOF) {
		switch (opt) {
		  case '1':
			test_num = 1;
			o_burst_count = 1;
			o_loops = 1;
			o_msg_len = 20;
			o_num_bursts = 600;  /* 10 min */
			o_pause = 1000;
			o_quiet = 1;  o_quiet_equiv_opt = " -q ";
			o_stat_pause = 2000;
			o_Sndbuf_size = MIN_DEFAULT_SENDBUF_SIZE;  o_Sndbuf_set = 1;
			break;
		  case '2':
			test_num = 2;
			o_burst_count = 1;
			o_loops = 1;
			o_msg_len = 5000;
			o_num_bursts = 5;  /* 5 sec */
			o_pause = 1000;
			o_quiet = 1;  o_quiet_equiv_opt = " -q ";
			o_stat_pause = 2000;
			o_Sndbuf_size = MIN_DEFAULT_SENDBUF_SIZE;  o_Sndbuf_set = 1;
			break;
		  case '3':
			test_num = 3;
			o_burst_count = 100;
			o_loops = 1;
			o_msg_len = 8*1024;
			o_num_bursts = 50;  /* 5 sec */
			o_pause = 100;
			o_quiet = 1;  o_quiet_equiv_opt = " -q ";
			o_stat_pause = 2000;
			o_Sndbuf_size = MIN_DEFAULT_SENDBUF_SIZE;  o_Sndbuf_set = 1;
			break;
		  case '4':
			test_num = 4;
			o_burst_count = 5000;
			o_loops = 1;
			o_msg_len = 20;
			o_num_bursts = 1;
			o_pause = 1000;
			o_quiet = 1;  o_quiet_equiv_opt = " -q ";
			o_stat_pause = 2000;
			o_Sndbuf_size = MIN_DEFAULT_SENDBUF_SIZE;  o_Sndbuf_set = 1;
			break;
		  case '5':
			test_num = 5;
			o_burst_count = 50000;
			o_loops = 1;
			o_msg_len = 800;
			o_num_bursts = 1;
			o_pause = 1000;
			o_quiet = 1;  o_quiet_equiv_opt = " -q ";
			o_stat_pause = 2000;
			o_Sndbuf_size = default_sndbuf_sz;  o_Sndbuf_set = 0;
			break;
		  case 'b':
			o_burst_count = atoi(toptarg);
			break;
		  case 'd':
			o_decimal = 1;
			break;
		  case 'h':
			help(NULL);  exit(0);
			break;
		  case 'l':
			o_loops = atoi(toptarg);
			break;
		  case 'm':
			o_msg_len = atoi(toptarg);
			if (o_msg_len > 65536) {
				o_msg_len = 65536;
				fprintf(stderr, "warning, msg_len lowered to 65536\n");
			}
			break;
		  case 'n':
			o_num_bursts = atoi(toptarg);
			break;
		  case 'p':
			o_pause = atoi(toptarg);
			break;
		  case 'P':
			o_msg_len = strlen(toptarg);
			if (o_msg_len > 65536) {
				fprintf(stderr, "Error, payload too big\n");
				exit(1);
			}
			if (o_msg_len % 2 > 0) {
				fprintf(stderr, "Error, payload must be even number of hex digits\n");
				exit(1);
			}
			o_Payload = strdup(toptarg);
			o_msg_len /= 2;  /* num bytes worth of binary payload */
			for (i = 0; i < o_msg_len; ++i) {
				/* convert 2 hex digits to binary byte */
				char c = o_Payload[i*2];
				if (c >= '0' && c <= '9')
					buff[i] = c - '0';
				else if (c >= 'a' && c <= 'f')
					buff[i] = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					buff[i] = c - 'A' + 10;
				else {
					fprintf(stderr, "Error, invalid hex digit in payload\n");
					exit(1);
				}
				c = o_Payload[i*2 + 1];
				if (c >= '0' && c <= '9')
					buff[i] = 16*buff[i] + c - '0';
				else if (c >= 'a' && c <= 'f')
					buff[i] = 16*buff[i] + c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					buff[i] = 16*buff[i] + c - 'A' + 10;
				else {
					fprintf(stderr, "Error, invalid hex digit in payload\n");
					exit(1);
				}
			}
			break;
		  case 'q':
			++ o_quiet;
			if (o_quiet == 1)
				o_quiet_equiv_opt = " -q ";
			else if (o_quiet == 2)
				o_quiet_equiv_opt = " -qq ";
			else
				o_quiet = 2;  /* never greater than 2 */
			break;
		  case 's':
			o_stat_pause = atoi(toptarg);
			break;
		  case 'S':
			o_Sndbuf_size = atoi(toptarg);  o_Sndbuf_set = 1;
			break;
		  case 't':
			if (o_unicast_udp) {
				fprintf(stderr, "Error, -t and -u are mutually exclusive\n");
				exit(1);
			}
			o_tcp = 1;
			break;
		  case 'u':
			if (o_tcp) {
				fprintf(stderr, "Error, -t and -u are mutually exclusive\n");
				exit(1);
			}
			o_unicast_udp = 1;
			break;
		  default:
			usage("unrecognized option");
			exit(1);
			break;
		}  /* switch */
	}  /* while opt */

	/* prevent careless usage from killing the network */
	if (o_num_bursts == 0 && (o_burst_count > 50 || o_pause < 100)) {
		usage("Danger - heavy traffic chosen with infinite num bursts.\nUse -n to limit execution time");
		exit(1);
	}

	num_parms = argc - toptind;

	strcpy(equiv_cmd, "CODE BUG!!!  'equiv_cmd' not initialized");
	/* handle positional parameters */
	if (num_parms == 2) {
		groupaddr = inet_addr(argv[toptind]);
		groupport = (unsigned short)atoi(argv[toptind+1]);
		if (o_quiet < 2)
			sprintf(equiv_cmd, "msend -b%d%s-m%d -n%d -p%d%s-s%d -S%d%s%s %s",
				o_burst_count, (o_decimal)?" -d ":" ", o_msg_len, o_num_bursts,
				o_pause, o_quiet_equiv_opt, o_stat_pause, o_Sndbuf_size,
				(o_tcp) ? " -t " : ((o_unicast_udp) ? " -u " : " "),
				argv[toptind],argv[toptind+1]);
			printf("Equiv cmd line: %s\n", equiv_cmd);
			fflush(stdout);
	} else if (num_parms == 3) {
		char c;  int i;
		groupaddr = inet_addr(argv[toptind]);
		groupport = (unsigned short)atoi(argv[toptind+1]);
		for (i = 0; (c = *(argv[toptind+2] + i)) != '\0'; ++i) {
			if (c < '0' || c > '9') {
				fprintf(stderr, "ERROR: third positional argument '%s' has non-numeric.  Should be TTL.\n", argv[toptind+2]);
				exit(1);
			}
		}
		ttlvar = (unsigned char)atoi(argv[toptind+2]);
		if (o_quiet < 2)
			sprintf(equiv_cmd, "msend -b%d%s-m%d -n%d -p%d%s-s%d -S%d%s%s %s %s",
				o_burst_count, (o_decimal)?" -d ":" ", o_msg_len, o_num_bursts,
				o_pause, o_quiet_equiv_opt, o_stat_pause, o_Sndbuf_size,
				(o_tcp) ? " -t " : ((o_unicast_udp) ? " -u " : " "),
				argv[toptind],argv[toptind+1],argv[toptind+2]);
			printf("Equiv cmd line: %s\n", equiv_cmd);
			fflush(stdout);
	} else if (num_parms == 4) {
		groupaddr = inet_addr(argv[toptind]);
		groupport = (unsigned short)atoi(argv[toptind+1]);
		ttlvar = (unsigned char)atoi(argv[toptind+2]);
		bind_if = argv[toptind+3];
		if (o_quiet < 2)
			sprintf(equiv_cmd, "msend -b%d%s-m%d -n%d -p%d%s-s%d -S%d%s%s %s %s %s",
				o_burst_count, (o_decimal)?" -d ":" ", o_msg_len, o_num_bursts,
				o_pause, o_quiet_equiv_opt, o_stat_pause, o_Sndbuf_size,
				(o_tcp) ? " -t " : ((o_unicast_udp) ? " -u " : " "),
				argv[toptind],argv[toptind+1],argv[toptind+2],bind_if);
			printf("Equiv cmd line: %s\n", equiv_cmd);
			fflush(stdout);
	} else {
		usage("need 2-4 positional parameters");
		exit(1);
	}

	/* Only warn about small default send buf if no sendbuf option supplied */
	if (default_sndbuf_sz < MIN_DEFAULT_SENDBUF_SIZE && o_Sndbuf_set == 0)
		fprintf(stderr, "NOTE: system default SO_SNDBUF only %d (%d preferred)\n", default_sndbuf_sz, MIN_DEFAULT_SENDBUF_SIZE);

	if (o_tcp) {
		if((sock = socket(PF_INET,SOCK_STREAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
	} else {
		if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
	}

	/* Try to set send buf size and check to see if it took */
	if (setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(const char *)&o_Sndbuf_size,
			sizeof(o_Sndbuf_size)) == SOCKET_ERROR) {
		fprintf(stderr, "WARNING: ");  perror("setsockopt - SO_SNDBUF");
	}
	sz = sizeof(check_size);
	if (getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char *)&check_size,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_SNDBUF");
		exit(1);
	}
	if (check_size < o_Sndbuf_size) {
		fprintf(stderr, "WARNING: tried to set SO_SNDBUF to %d, only got %d\n",
				o_Sndbuf_size, check_size);
	}

	memset((char *)&sin,0,sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = groupaddr;
	sin.sin_port = htons(groupport);

	if (! o_unicast_udp && ! o_tcp) {
#if defined(_WIN32)
		wttl = ttlvar;
		if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_TTL,(char *)&wttl,
					sizeof(wttl)) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("setsockopt - TTL");
			exit(1);
		}
#else
		if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_TTL,(char *)&ttlvar,
					sizeof(ttlvar)) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("setsockopt - TTL");
			exit(1);
		}
#endif /* _WIN32 */
	}

	if (bind_if != NULL) {
#if !defined(_WIN32)
		memset((char *)&iface_in,0,sizeof(iface_in));
		iface_in.s_addr = inet_addr(bind_if);
#else
		iface_in = inet_addr(bind_if);
#endif /* !_WIN32 */
		if(setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface_in,
				sizeof(iface_in)) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("setsockopt - IP_MULTICAST_IF");
			exit(1);
		}
	}

	if (o_tcp) {
		if((connect(sock,(struct sockaddr *)&sin,sizeof(sin))) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("connect");
			exit(1);
		}
	}


/* Loop the test "o_loops" times (-l option) */
MAIN_LOOP:

	if (o_num_bursts != 0) {
		if (o_msg_len == 0) {
			if (o_quiet < 2) {
				printf("Sending %d bursts of %d variable-length messages\n",
					o_num_bursts, o_burst_count);
				fflush(stdout);
			}
		}
		else {
			if (o_quiet < 2) {
				printf("Sending %d bursts of %d %d-byte messages\n",
					o_num_bursts, o_burst_count, o_msg_len);
				fflush(stdout);
			}
		}
	}

	/* 1st msg: give network hardware time to establish mcast flow */
	if (test_num >= 0)
		sprintf(cmdbuf, "echo test %d, sender equiv cmd %s", test_num, equiv_cmd);
	else
		sprintf(cmdbuf, "echo sender equiv cmd: %s", equiv_cmd);
	if (o_tcp) {
		send_rtn = send(sock,cmdbuf,strlen(cmdbuf)+1,0);
	} else {
		send_rtn = sendto(sock,cmdbuf,strlen(cmdbuf)+1,0,(struct sockaddr *)&sin,sizeof(sin));
	}
	if (send_rtn == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("send");
		exit(1);
	}
	SLEEP_SEC(1);

	burst_num = 0;
	msg_num = 0;
	while (o_num_bursts == 0 || burst_num < o_num_bursts) {
		if (o_pause > 0 && msg_num > 0)
			SLEEP_MSEC(o_pause);

		/* send burst */
		for (i = 0; i < o_burst_count; ++i) {
			send_len = o_msg_len;
			if (! o_Payload) {
				if (o_decimal)
					sprintf(buff,"Message %d",msg_num);
				else
					sprintf(buff,"Message %x",msg_num);
				if (o_msg_len == 0)
					send_len = strlen(buff);
			}

			if (i == 0) {  /* first msg in batch */
				if (o_quiet == 0) {  /* not quiet */
					if (o_burst_count == 1)
						printf("Sending %d bytes\n", send_len);
					else
						printf("Sending burst of %d msgs\n",
								o_burst_count);
				}
				else if (o_quiet == 1) {  /* pretty quiet */
					printf(".");
					fflush(stdout);
				}
				/* else o_quiet > 1; very quiet */
			}

			send_rtn = sendto(sock,buff,send_len,0,(struct sockaddr *)&sin,sizeof(sin));
			if (send_rtn == SOCKET_ERROR) {
				fprintf(stderr, "ERROR: ");  perror("send");
				exit(1);
			}
			else if (send_rtn != send_len) {
				fprintf(stderr, "ERROR: sendto returned %d, expected %d\n",
						send_rtn, send_len);
				exit(1);
			}

			++msg_num;
		}  /* for i */

		++ burst_num;
	}  /* while */

	if (o_stat_pause > 0) {
		/* send 'stat' message */
		if (o_quiet < 2)
			printf("Pausing before sending 'stat'\n");
		SLEEP_MSEC(o_stat_pause);
		if (o_quiet < 2)
			printf("Sending stat\n");
		sprintf(cmdbuf, "stat %d", msg_num);
		send_len = strlen(cmdbuf);
		send_rtn = sendto(sock,cmdbuf,send_len,0,(struct sockaddr *)&sin,sizeof(sin));
		if (send_rtn == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("send");
			exit(1);
		}
		else if (send_rtn != send_len) {
			fprintf(stderr, "ERROR: sendto returned %d, expected %d\n",
					send_rtn, send_len);
			exit(1);
		}

		if (o_quiet < 2)
			printf("%d messages sent (not including 'stat')\n", msg_num);
	}
	else {
		if (o_quiet < 2)
			printf("%d messages sent\n", msg_num);
	}

	/* Loop the test "o_loops" times (-l option) */
	-- o_loops;
	if (o_loops > 0) goto MAIN_LOOP;


	CLOSESOCKET(sock);

	return(0);
}  /* main */
