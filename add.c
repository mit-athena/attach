/* Copyright 1998 by the Massachusetts Institute of Technology.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

/* This file contains most of the implementation of the add command
 * for attach.
 */

static char rcsid[] = "$Id: add.c,v 1.1 1998-03-17 03:48:47 cfields Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <athdir.h>
#include "stringlist.h"

/* I wanted to get this by including attach.h, but if I do I get a conflict
 * in the declaration of malloc. I decided not to open that can of worms.
 */
extern char *progname;

/* For debugging output, strings specifying what path is being modified. */
char *debug_strings[5] = {
  NULL,
  "$PATH",
  "$athena_path",
  "$MANPATH",
  "$athena_manpath"
};

void usage()
{
  fprintf(stderr, "Usage: add [-vdfrpwe] [-P $athena_path] [-M $athena_manpath] [-a attachflags]             [lockername] [lockername] ...\n");
  exit(1);
}

/* modify_path
 *
 * Modifies "path," a string_list. "changes" are removed from "path"
 * if "remove," are added to the front of "path" (potentially moved)
 * if "front," or otherwise are added to the end of "path" if not
 * already present. "debuginfo," if not NULL, will be used to tag
 * debugging output. If NULL, no debugging output will occur.
 */
string_list *modify_path(string_list *path, string_list *changes,
			 int front, int remove, char *debuginfo)
{
  char **ptr;
  int contains;

  if (sl_grab_string_array(changes) != NULL)
    {
      for (ptr = sl_grab_string_array(changes); *ptr != NULL; ptr++)
	{
	  contains = sl_contains_string(path, *ptr);

	  /* If the the list contains this path and we want to remove it
	   * or move it to the front, remove it.
	   */
	  if (contains && (front || remove))
	    {
	      sl_remove_string(&path, *ptr);
	      if (debuginfo != NULL)
		fprintf(stderr, "%s removed from %s\n", *ptr, debuginfo);
	    }
	  else
	    /* Otherwise, if we're just supposed to add it and it's already
	     * there, leave it as is and move on.
	     */
	    if (contains)
	      continue;

	  /* If we're here and we're not removing, then we're adding to
	   * the list and the path is not now in the list. So it's time
	   * to add it.
	   */
	  if (!remove)
	    {
	      if (sl_add_string(&path, *ptr, front))
		{
		  fprintf(stderr, "%s: out of memory in modify_path\n",
			  progname);
		  exit(1);
		}

	      if (debuginfo != NULL)
		fprintf(stderr, "%s added to %s of %s\n", *ptr,
			front ? "front" : "end", debuginfo);
	    }
	}
    }

  return path;
}

/* print_readable_path
 *
 * A hack to print out a readable version of the user's path.
 *
 * It's a hack because it's not currently a nice thing to do
 * correctly. So, if any path element is of the form "/mit/*bin" such
 * as "/mit/gnu/arch/sun4x_55/bin," we print "{add gnu}" instead.
 * This is not always correct; it misses things that are not mounted
 * under /mit, and is misleading for lockers that do not mount under
 * /mit/lockername as well as MUL type filesystems. However, these
 * occasions are infrequent.
 *
 * In addition, each path matching /mit/*bin is tested for the substring
 * of the machine's $ATHENA_SYS value. If absent, it is assumed that
 * some form of compatibility system is being used, and a * is added
 * to the shortened path string. So if ATHENA_SYS_COMPAT is set to
 * sun4x_55 while ATHENA_SYS is set to sun4x_56, in the example above
 * "{add gnu*}" would be printed instead of "{add gnu}."
 */
void print_readable_path(string_list *path)
{
  string_list *readable_path = NULL;
  char **ptr, *name, *name_end;
  char name_buf[MAXPATHLEN+10];

  if (sl_grab_string_array(path) != NULL)
    {
      for (ptr = sl_grab_string_array(path); *ptr != NULL; ptr++)
	{
	  if (!strncmp(*ptr, "/mit/", 5))
	    {
	      name = *ptr + 5;
	      name_end = strchr(name, '/');
	      if (name_end != NULL && !strcmp(*ptr + strlen(*ptr) - 3, "bin"))
		{
		  if (athdir_native(name, NULL))
		    sprintf(name_buf, "{add %.*s}", name_end - name, name);
		  else
		    sprintf(name_buf, "{add %.*s*}", name_end - name, name);
		  if (sl_add_string(&readable_path, name_buf, 0))
		    {
		      fprintf(stderr, "%s: out of memory in "
			      "print_readable_path\n", progname);
		      exit(1);
		    }
		  continue;
		}
	    }
	  if (sl_add_string(&readable_path, *ptr, 0))
	    {
	      fprintf(stderr, "%s: out of memory in print_readable_path\n",
		      progname);
	      exit(1);
	    }
	}
      fprintf(stdout, "%s\n", sl_grab_string(readable_path, ' '));
      sl_free(&readable_path);
    }
}

int addcmd(int argc, char **argv)
{
  int c;
  int verbose = 0, debug = 0, add_to_front = 0,
    remove_from_path = 0, print_path = 0, give_warnings = 0,
    use_athena_paths = 0, bourne_output = 0, end_args = 0;
  string_list *path = NULL, *manpath = NULL;
  string_list *athena_path = NULL, *athena_manpath = NULL;
  string_list *delta_path = NULL, *delta_manpath = NULL;
  string_list *mountpoint_list = NULL;
  string_list *attach_args = NULL;
  char **found, **mountpoint, **ptr;
  FILE *shell;

  /* Redirect stdout to stderr, so attach output won't be interpreted
   * by the shell. Preserve stdout so we can still talk to the shell.
   */
  shell = fdopen(dup(STDOUT_FILENO), "w");
  fflush(stdout);
  dup2(STDERR_FILENO, STDOUT_FILENO);

  /* Parse the command line... */
  while (!end_args && (c = getopt(argc, argv, "vdnfrpwebP:M:a")) != EOF)
    switch(c)
      {
      case 'v':
	verbose = 1;
	break;

      case 'd':
	debug = 1;
	break;

      case 'n':
	fprintf(stderr, "%s: warning: -n no longer required\n", progname);
	break;

      case 'f':
	add_to_front = 1;
	break;

      case 'r':
	remove_from_path = 1;
	break;

      case 'p':
	print_path = 1;
	break;

      case 'w':
	give_warnings = 1;
	break;

      case 'e':
	use_athena_paths = 1;
	break;

      case 'b':
	bourne_output = 1;
	break;

      case 'P':
	/* Store the elements of athena_path, if passed, into the
	 * athena_path string list. athena_path is a space-separated
	 * sequence of arguments, so we need to loop over them to
	 * add each to the string list individually.
	 */
	if (sl_add_string(&athena_path, optarg, 0))
	  {
	    fprintf(stderr, "%s: out of memory (P)\n", progname);
	    exit(1);
	  }
	while (optind < argc && *argv[optind] != '-')
	  {
	    if (sl_add_string(&athena_path, argv[optind++], 0))
	      {
		fprintf(stderr, "%s: out of memory (P)\n", progname);
		exit(1);
	      }
	  }
	break;

      case 'M':
	/* Store the elements of athena_manpath, if passed, into the
	 * athena_manpath string list. athena_manpath is a coloon-separated
	 * path list, so there is only one argument we need to worry about.
	 * We use sl_parse_string to separate the list into components
	 * and store it into the string list.
	 */
	if (sl_parse_string(&athena_manpath, optarg, ':'))
	  {
	    fprintf(stderr, "%s: out of memory (M)\n", progname);
	    exit(1);
	  }
	break;

      case 'a':
	/* -a means the remaining arguments go to attach, even if there are
	 * flags in them.
	 */
	end_args = 1;
	break;

      case '?':
	usage();
	break;
      }

  /* Set up path/manpath to point to the paths we wish to manipulate.
   * If use_athena_paths, -M and -P should have been specified, and
   * the search paths come from there. Otherwise, we want the search
   * paths specified in the environment.
   */
  if (use_athena_paths)
    {
      path = athena_path;
      manpath = athena_manpath;
    }
  else
    {
      if (sl_parse_string(&path, getenv("PATH"), ':') ||
	  sl_parse_string(&manpath, getenv("MANPATH"), ':'))
	{
	  fprintf(stderr, "%s: out of memory (setup)\n", progname);
	  exit(1);
	}
    }

  /* If no arguments have been directed to attach, or -p was specified,
   * we output the path in an easier-to-read format and we're done.
   */
  if (argc == optind || print_path)
    {
      print_readable_path(path);
      sl_free(&path);
      sl_free(&manpath);
      exit(0);
    }

  /* Get information from attach... */

  /* Create argv for attach... */
  sl_add_string(&attach_args, "add", 0);
  if (verbose == 0)
    sl_add_string(&attach_args, "-q", 0);

  /* The remaining arguments go to attach. */
  while (optind < argc)
    {
      if (sl_add_string(&attach_args, argv[optind++], 0))
	{
	  fprintf(stderr, "%s: out of memory (optind)\n", progname);
	  exit(1);
	}
    }

  attachcmd(sl_grab_length(attach_args),  sl_grab_string_array(attach_args),
	    &mountpoint_list);
  sl_free(&attach_args);

  /* Iterate over the mountpoints attach returned and find the
   * subdirectories we want. In the end, delta_path and delta_manpath
   * will contain lists of the paths to be added or removed from the
   * path and manpath.
   */
  for (mountpoint = sl_grab_string_array(mountpoint_list);
       mountpoint != NULL && *mountpoint != NULL; mountpoint++)
    {
      /* Find the binary directories we want to add to/remove from the path. */
      found = athdir_get_paths(*mountpoint, "bin", NULL, NULL, NULL, NULL, 0);
      if (found != NULL)
	{
	  for (ptr = found; *ptr != NULL; ptr++)
	    {
	      if (!remove_from_path && !athdir_native(*ptr, NULL))
		fprintf(stderr, "%s: warning: using compatibility for %s\n",
			progname, *mountpoint);
		
	      if (sl_add_string(&delta_path, *ptr, 0))
		{
		  fprintf(stderr, "%s: out of memory (bin)\n", progname);
		  exit(1);
		}
	    }
	  athdir_free_paths(found);
	}
      else
	if (give_warnings)
	  fprintf(stderr, "%s: warning: %s has no binary directory\n",
		  progname, *mountpoint);

      /* Find the man directories we want to add to/remove from the manpath. */
      found = athdir_get_paths(*mountpoint, "man", NULL, NULL, NULL, NULL, 0);
      if (found != NULL)
	{
	  for (ptr = found; *ptr != NULL; ptr++)
	    {
	      if (sl_add_string(&delta_manpath, *ptr, 0))
		{
		  fprintf(stderr, "%s: out of memory (man)\n", progname);
		  exit(1);
		}
	    }
	  athdir_free_paths(found);
	}
    }
  sl_free(&mountpoint_list);

  /* Make the changes to the path lists. */
  path = modify_path(path, delta_path, add_to_front, remove_from_path,
		     debug_strings[debug * (use_athena_paths + 1)]);
  sl_free(&delta_path);

  manpath = modify_path(manpath, delta_manpath,	add_to_front, remove_from_path,
			debug_strings[debug * (use_athena_paths + 3)]);
  sl_free(&delta_manpath);

  /* Output the shell commands needed to modify the user's path. */
  if (use_athena_paths)
    {
      fprintf(shell, "set athena_path=(%s);", sl_grab_string(path, ' '));
      fprintf(shell, "set athena_manpath=%s\n", sl_grab_string(manpath, ':'));
    }
  else
    {
      if (bourne_output)
	{
	  fprintf(shell, "PATH=%s;export PATH;", sl_grab_string(path, ':'));
	  fprintf(shell, "MANPATH=%s;export MANPATH\n",
		  sl_grab_string(manpath, ':'));
	}
      else
	{
	  fprintf(shell, "setenv PATH %s;", sl_grab_string(path, ':'));
	  fprintf(shell, "setenv MANPATH %s\n", sl_grab_string(manpath, ':'));
	}
    }

  sl_free(&path);
  sl_free(&manpath);
  exit(0);
}