/*
 * Copyright (C) 1997 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 2012 Michael R. Elkins <me@sigpipe.org>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Created:       Thu Dec  4 02:13:11 PST 1997
 * Last Modified: Tue Jul  4 11:23:49 CEST 2000
 */ 

#ifdef USE_SLANG
#include <slcurses.h>
#include <sys/file.h>
#else
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif
#endif /* USE_SLANG */

#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef HAVE_REGEX_H
#include <regex.h>
#else
#include <rx/rxposix.h>
#endif

#include "quote.h"

#define DEFAULT_REGEXP "(((http|https|ftp|gopher)|mailto):(//)?[^ <>\"\t]*|(www|ftp)[0-9]?\\.[-a-z0-9.]+)[^ .,;\t\n\r<\">\\):]?[^, <>\"\t]*[^ .,;\t\n\r<\">\\):]"
#define DEFAULT_COMMAND "/etc/urlview/url_handler.sh %s"
#define SYSTEM_INITFILE "/etc/urlview/system.urlview"

#define OFFSET 2
#define PAGELEN (LINES - 1 - OFFSET)
#define URLSIZE 128

enum
{
  FULL = 1,
  INDEX,
  MOTION
};

extern int mutt_enter_string (unsigned char *buf, size_t buflen, int y, int x,
		int flags);

void search_forward (char *search, int urlcount, char **url, int *redraw, int *current, int *top)
{
  regex_t rx;
  int i, j;

  if (strlen(search) == 0 || *search == '\n')
  {
    move (LINES - 1, 0);
    clrtoeol ();
    *redraw = MOTION;
  } 
  else
  {
    if ( (j = regcomp (&rx, search, REG_EXTENDED | REG_ICASE | REG_NEWLINE)) )
    {
      regerror (j, &rx, search, sizeof (search));
      regfree (&rx);
      puts (search);
    }
    for (i = *current + 1; i < urlcount; i++)
    {
      if (regexec (&rx, url[i], 0, NULL, 0) == 0)
      {
	*current = i;
	if (*current < *top || *current > *top + PAGELEN -1)
	{
	  *top = *current - *current % PAGELEN - 1;
	}
	i = urlcount;
      }
    }
    move (LINES - 1, 0);
    clrtoeol ();
    *redraw = INDEX;
    regfree (&rx);
  }
}

void search_backward (char *search, int urlcount, char **url, int *redraw, int *current, int *top)
{
  regex_t rx;
  int i, j;

  (void)urlcount; /*unused*/
  if (strlen(search) == 0 || *search == '\n')
  {
    move (LINES - 1, 0);
    clrtoeol ();
    *redraw = MOTION;
  } 
  else
  {
    if ((j = regcomp (&rx, search, REG_EXTENDED | REG_ICASE | REG_NEWLINE)))
    {
      regerror (j, &rx, search, sizeof (search));
      regfree (&rx);
      puts (search);
    }
    for (i = *current - 1; i >= 0; i--)
    {
      if (regexec (&rx, url[i], 0, NULL, 0) == 0)
      {
	*current = i;
	if (*current < *top || *current > *top + PAGELEN -1)
	{
	  *top = *current - *current % PAGELEN - 1;
	  if (*top < 0)
	    *top = 0;
	}
	i = 0;
      }
    }
    move (LINES - 1, 0);
    clrtoeol ();
    *redraw = INDEX;
    regfree (&rx);
  }
}
 
int main (int argc, char **argv)
{
  struct passwd *pw;
  struct stat stat_buf;
#ifndef USE_SLANG
  SCREEN *scr;
#endif
  FILE *fp;
  regex_t rx;
  regmatch_t match;
  char buf[1024];
  char command[1024];
  char regexp[1024];
  char search[1024];
  char scratch[1024];
  char wrapchoice[1024];
  char **url;
  int urlsize = URLSIZE;
  char *pc;
  char *wc;
  size_t len;
  size_t offset;
  int i;
  int top = 0;
  int startline = 0;
  int oldcurrent = 0;
  int current = 0;
  int done = 0;
  int quitonlaunch = 0;
  int redraw = FULL;
  int urlcount = 0;
  int urlcheck = 0;
  int reopen_tty = 0;
  int is_filter = 0;

  int expert = 0;

  int skip_browser = 0;

  int menu_wrapping = 0;
  
  if (argc == 1)
    is_filter = 1;

  strncpy (regexp, DEFAULT_REGEXP, sizeof (regexp) - 1);
  strncpy (command, DEFAULT_COMMAND, sizeof (command) - 1);
  
  /*** read the initialization file ***/

  pw = getpwuid (getuid ());
  snprintf (buf, sizeof (buf), "%s/.urlview", pw->pw_dir);

  /*** Check for users rc-file ***/
  if (stat (buf,&stat_buf) == -1)
      snprintf (buf, sizeof(buf), "%s", SYSTEM_INITFILE);

  if ((fp = fopen (buf, "r")))
  {
    while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      if (buf[0] == '#' || buf[0] == '\n')
	continue;
      if (strncmp ("REGEXP", buf, 6) == 0 && isspace (buf[6]))
      {
	pc = buf + 6;
	while (isspace (*pc))
	  pc++;
	wc = regexp;
	while (*pc && *pc != '\n')
	{
	  if (*pc == '\\')
	  {
	    pc++;
	    switch (*pc)
	    {
	      case 'n':
		*wc++ = '\n';
		break;
	      case 'r':
		*wc++ = '\r';
		break;
	      case 't':
		*wc++ = '\t';
		break;
	      case 'f':
		*wc++ = '\f';
		break;
	      default:
		*wc++ = '\\';
		*wc++ = *pc;
		break;
	    }
	  }
	  else
	    *wc++ = *pc;
	  pc++;
	}
	*wc = 0;
      }
      else if (strncmp ("COMMAND", buf, 7) == 0 && isspace (buf[7]))
      {
	pc = buf + 7;
	while (isspace (*pc))
	  pc++;
	pc[ strlen (pc) - 1 ] = 0; /* kill the trailing newline */
	strncpy (command, pc, sizeof (command) - 1);
	command[sizeof (command) - 1] = 0;
	skip_browser = 1;
      }
      else if (strncmp ("WRAP", buf, 4) == 0 && isspace (buf[4]))
      {
	pc = buf + 4;
	while (isspace (*pc))
	  pc++;
	pc[ strlen (pc) - 1 ] = 0; /* kill the trailing newline */
	strncpy (wrapchoice, pc, sizeof (wrapchoice) - 1);
	wrapchoice[sizeof (wrapchoice) - 1] = 0;
	/* checking the value, case insensitive */
	if (strcasecmp("YES", wrapchoice) == 0)
		menu_wrapping = 1;
	else if (!strcasecmp("NO", wrapchoice) == 0)
	{
		printf ("Unknown value for WRAP: %s. Valid values are: YES, NO\n", wrapchoice);
		exit (1);
	}	
      }
      else if (strncmp ("BROWSER", buf, 7) == 0 && isspace (buf[7]))
      {
	skip_browser = 0;
      }
      else if (strcmp ("EXPERT\n", buf) == 0)
      {
	expert = 1;
      }
	  else if (strcmp ("QUITONLAUNCH\n", buf) == 0)
	  {
	quitonlaunch = 1;
	  }
      else
      {
	printf ("Unknown command: %s", buf);
	exit (1);
      }
    }
    fclose (fp);
  }
 
  /* Only use the $BROWSER environment variable if 
   * (a) no COMMAND in rc file or
   * (b) BROWSER in rc file.
   * If both COMMAND and BROWSER are in the rc file, then the option used
   * last counts.
   */
  if (!skip_browser) {
    pc = getenv("BROWSER");
    if (pc)
    {
        if (strlen(pc) > 0) {
          strncpy (command, pc, sizeof (command) - 1);
        } else {
          printf("Your $BROWSER is zero-length.  Falling back to COMMAND.");
        }
    }
  }

  if (!expert && strchr (command, '\'')) 
  {
    puts ("\n\
ERROR: Your $BROWSER contains a single\n\
quote (') character. This is most likely\n\
in error; please read the manual page\n\
for details. If you really want to use\n\
this command, please put the word EXPERT\n\
into a line of its own in your \n\
~/.urlview file.\n\
");
    exit (1);
  }
  
  /*** compile the regexp ***/

  if ((i = regcomp (&rx, regexp, REG_EXTENDED | REG_ICASE | REG_NEWLINE)))
  {
    regerror (i, &rx, buf, sizeof (buf));
    regfree (&rx);
    puts (buf);
    exit (1);
  }

  /*** find matching patterns ***/
  
  if ((url = (char **) malloc (urlsize * sizeof (char *))) == NULL)
  {
    printf ("Couldn't allocate memory for url list\n");
    exit (1);
  }

  for (; is_filter || argv[optind]; optind++)
  {
    if (is_filter || strcmp ("-", argv[optind]) == 0)
    {
      fp = stdin;
      reopen_tty = 1;
    }
	else if (!is_filter && argv[optind][0] == '-') {
	  startline = atoi(argv[optind]+1);
	  current = -1;
	  continue;
	}
    else if (!(fp = fopen (argv[optind], "r")))
    {
      perror (argv[optind]);
      continue;
    }

    while (fgets (buf, sizeof (buf) - 1, fp) != NULL)
    {
	  --startline;
      offset = 0;
      while (regexec (&rx, buf + offset, 1, &match, offset ? REG_NOTBOL : 0) == 0)
      {
	len = match.rm_eo - match.rm_so;
        if (urlcount >= urlsize)
	{
	  void *urltmp;
	  urltmp = realloc ((void *) url, (urlsize + URLSIZE) * sizeof (char *));
	  if (urltmp == NULL)
	  {
	    printf ("Couldn't allocate memory for additional urls, "
		    "only first %d displayed\n", urlsize);
	    goto got_urls;
	  }
	  else
	  {
	    urlsize += URLSIZE;
	    url = (char **) urltmp;
	  }
	}
	url[urlcount] = malloc (len + 1);
	memcpy (url[urlcount], buf + match.rm_so + offset, len);
	url[urlcount][len] = 0;
	for (urlcheck=0; urlcheck < urlcount; urlcheck++)
	{
	  if(strcasecmp(url[urlcount],url[urlcheck])==0)
	  {
	    urlcount--;
	    break;
	  }
	}
	if (current < 0 && startline <= 0)
	  current = urlcount;
	urlcount++;
	offset += match.rm_eo;
      }
    }
  got_urls:
    fclose (fp);
    if (is_filter)
      break;
  }

  regfree (&rx);

  if (!urlcount)
  {
    puts ("No URLs found.");
    exit (1);
  }

  /*** present the URLs to the user ***/
  for (i = 0; i < urlcount; i++)
  {
      printf("%s\n", url[i]);
  }

  exit (0);
}
