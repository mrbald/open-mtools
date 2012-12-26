
/* tgetopt.c - (renamed from BSD getopt) - this source was adapted from BSD
 *
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>
#include <stdio.h>

#ifdef _BSD
extern char *__progname;
#else
#define __progname "tgetopt"
#endif

int	topterr = 1,		/* if error message should be printed */
	toptind = 1,		/* index into parent argv vector */
	toptopt,			/* character checked for validity */
	toptreset;		/* reset getopt */
char	*toptarg;		/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

/*
 * tgetopt --
 *	Parse argc/argv argument vector.
 */
int
tgetopt(nargc, nargv, ostr)
	int nargc;
	char * const *nargv;
	const char *ostr;
{
	static char *place = EMSG;		/* option letter processing */
	char *oli;				/* option letter list index */

	/* really reset */
	if (toptreset) {
		topterr = 1;
		toptind = 1;
		toptopt = 0;
		toptreset = 0;
		toptarg = NULL;
		place = EMSG;
	}
	if (!*place) {		/* update scanning pointer */
		if (toptind >= nargc || *(place = nargv[toptind]) != '-') {
			place = EMSG;
			return (-1);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++toptind;
			place = EMSG;
			return (-1);
		}
	}					/* option letter okay? */
	if ((toptopt = (int)*place++) == (int)':' ||
	    !(oli = strchr(ostr, toptopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means -1.
		 */
		if (toptopt == (int)'-')
			return (-1);
		if (!*place)
			++toptind;
		if (topterr && *ostr != ':')
			(void)fprintf(stderr,
			    "%s: illegal option -- %c\n", __progname, toptopt);
		return (BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		toptarg = NULL;
		if (!*place)
			++toptind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			toptarg = place;
		else if (nargc <= ++toptind) {	/* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (topterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    __progname, toptopt);
			return (BADCH);
		}
	 	else				/* white space */
			toptarg = nargv[toptind];
		place = EMSG;
		++toptind;
	}
	return (toptopt);			/* dump back option letter */
}  /* tgetopt */
