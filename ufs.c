/*	Created by:  Theodore Ts'o
 *
 *	$Source: /afs/dev.mit.edu/source/repository/athena/bin/attach/ufs.c,v $
 *	$Author: epeisach $
 *
 *	Copyright (c) 1988 by the Massachusetts Institute of Technology.
 */

#ifndef lint
static char rcsid_ufs_c[] = "$Header: /afs/dev.mit.edu/source/repository/athena/bin/attach/ufs.c,v 1.2 1991-03-04 12:57:38 epeisach Exp $";
#endif lint

#include "attach.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>

#ifdef UFS
/*
 * Attach an Unix filesystem
 */

ufs_attach(at, mopt, errorout)
	struct _attachtab *at;
	struct mntopts	*mopt;
	int errorout;
{
    if (at->mode == 'w' && !skip_fsck) {
	    if (perform_fsck(at->hostdir, at->hesiodname, 1) == FAILURE)
		    return(FAILURE);
    }
    
    /* XXX This is kind of bogus, because if a filesystem has a number
     * of hesiod entries, and the mount point is busy, each one will
     * be tried until the last one fails, then an error printed.
     * C'est la vie.
     */

    if (mountfs(at, at->hostdir, mopt, errorout) == FAILURE) {
	return (FAILURE);
    }

    return (SUCCESS);
}

/*
 * Detach a Unix filesystem
 */
ufs_detach(at)
    struct _attachtab *at;
{
    if (at->flags & FLAG_PERMANENT) {
	if (debug_flag)
	    printf("Permanent flag on, skipping umount.\n");
	return(SUCCESS);
    }
	
    if (unmount_42(at->hesiodname, at->mntpt, at->hostdir) == FAILURE)
	return (FAILURE);

    return (SUCCESS);
}
#endif

/*
 * Attach an "error" filesystem.
 * (Print an error message and go away; always fails)
 * Not strictly UFS, but it's a good place to sneak it in...
 */
err_attach(at, mopt, errorout)
	struct _attachtab *at;
	struct mntopts	*mopt;
	int errorout;		/* ignored */
{
	fprintf(stderr, "%s: error: %s\n", at->hesiodname, at->hostdir);
	return(FAILURE);
}

#if defined(UFS) || defined(RVD)
/*
 * Subroutine to check a filesystem with fsck
 */
perform_fsck(device, errorname, useraw)
	char	*device, *errorname;
	int	useraw;
{
	static char	rdevice[512];
	static char	*fsck_av[4];
	int	error_ret, save_stderr;
	union wait	waitb;

	strncpy(rdevice, device, sizeof(rdevice));
	
	/* Try to generate the raw device, since it's almost always faster */
	if (useraw) {
		char	*cpp, *cpp2;
		struct stat	buf;
		
		if (!(cpp = rindex(rdevice, '/')))
			cpp = rdevice;
		else
			cpp++;
		*cpp++ = 'r';
		if (!(cpp2 = rindex(device, '/')))
			cpp2 = device;
		else
			cpp2++;
		(void) strcpy(cpp, cpp2);
	
		/*
		 * Try to stat the constructed rdevice.  If it isn't a
		 * device file or if it doesn't exist, give up and use
		 * the original file. 
		 */
		if (stat(rdevice,&buf) || !(buf.st_mode & (S_IFCHR|S_IFBLK)))
			strcpy(rdevice,device);
	}

	if (debug_flag)
		printf("performing an fsck on %s\n", rdevice);
	
	fsck_av[0] = FSCK_SHORTNAME;
	fsck_av[1] = "-p";		/* Preen option */
	fsck_av[2] = rdevice;
	fsck_av[3] = NULL;
	switch(vfork()) {
	case -1:
		perror("vfork: to fsck");
		error_status = ERR_ATTACHFSCK;
		return(FAILURE);
	case 0:
		if (!debug_flag) {
			save_stderr = dup(2);
			close(0);
			close(1);
			close(2);
			open("/dev/null", O_RDWR);
			dup(0);
			dup(0);
		}

		execv(FSCK_FULLNAME, fsck_av);
		if (!debug_flag)
			dup2(save_stderr, 2);
		perror(FSCK_FULLNAME);
		exit(1);
		/*NOTREACHED*/
	default:
		if (wait(&waitb) < 0) {
			perror("wait: for fsck");
			error_status = ERR_ATTACHFSCK;
			return(FAILURE);
		}
	}
	
	if (error_ret = waitb.w_retcode) {
		fprintf(stderr,
			"%s: fsck returned a bad exit status (%d)\n",
			errorname, error_ret);
		error_status = ERR_ATTACHFSCK;
		return (FAILURE);
	}
	return(SUCCESS);
}

/*
 * Parsing of explicit UFS file types
 */
char **ufs_explicit(name)
    char *name;
{
    extern char *exp_hesptr[2];
	
    sprintf(exp_hesline, "UFS %s %c %s", name, override_mode ?
	    override_mode : 'w', mntpt ? mntpt : "/mnt");
    exp_hesptr[0] = exp_hesline;
    exp_hesptr[1] = 0;
    return(exp_hesptr);
}

#endif
