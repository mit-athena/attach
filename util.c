/*	Created by:	Robert French
 *
 *	$Source: /afs/dev.mit.edu/source/repository/athena/bin/attach/util.c,v $
 *	$Author: jfc $
 *
 *	Copyright (c) 1988 by the Massachusetts Institute of Technology.
 */

#ifndef lint
static char rcsid_util_c[] = "$Header: /afs/dev.mit.edu/source/repository/athena/bin/attach/util.c,v 1.1 1990-04-19 12:17:59 jfc Exp $";
#endif lint

#include "attach.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <pwd.h>
#include <signal.h>
#ifdef HESIOD
#include <hesiod.h>
#endif
#include <string.h>

#define TOKSEP " \t\r\n"
	
#ifdef ultrix
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

char exp_hesline[BUFSIZ];	/* Place to store explicit */
char *exp_hesptr[2];		/* ``hesiod'' entry */
char *abort_msg = "Operation aborted\n";

static missarg();

/*
 * These routines provide a way of locking out interrupts during
 * critical sections of code.  After the critical sections of code are
 * executed, if a SIGTERM or SIGINT had arrived before, the program
 * terminates then.
 */

static int	in_critical_code_p = 0;
int	caught_signal = 0;

/*
 * Signal handler for SIGTERM & SIGINT
 */
sig_catch sig_trap()
{
	if (in_critical_code_p) {
		caught_signal++;
		return;
	}
	terminate_program();
}

/*
 * Enter critical section of code
 */
start_critical_code()
{
	in_critical_code_p++;
}

/*
 * Exit critical section of code
 */
end_critical_code()
{
	if (!--in_critical_code_p && caught_signal)
		terminate_program();
}

/*
 * terminate the program
 */
terminate_program()
{
#ifdef notdef
	printf("\nInterrupted.... aborting operation....\n");
#endif
	exit(ERR_INTERRUPT);
}

/*
 * LOCK the mtab - wait for it if it's already locked
 */

/*
 * Unfortunately, mount and umount don't honor lock files, so these
 * locks are only valid for other attach processes.
 */

static int mtab_lock_fd = -1;

lock_mtab()
{
	char	*lockfn;
	
	if (mtab_lock_fd < 0) {
		if (!(lockfn = malloc(strlen(mtab_fn)+6))) {
			fprintf(stderr, "Can't malloc lockfile filename.\n");
			fprintf(stderr, abort_msg);
			exit(ERR_FATAL);
		}
		(void) strcpy(lockfn, mtab_fn);
		(void) strcat(lockfn, ".lock");
		mtab_lock_fd = open(lockfn, O_CREAT, 0644);
		if (mtab_lock_fd < 0) {
			fprintf(stderr,"Can't open %s: %s\n", lockfn,
				sys_errlist[errno]);
			fprintf(stderr, abort_msg);
			exit(ERR_FATAL);
		}
	} 
	flock(mtab_lock_fd, LOCK_EX);
}

/*
 * UNLOCK the mtab
 */
unlock_mtab()
{
	close(mtab_lock_fd);
	mtab_lock_fd = -1;
}

/*
 * Convert a string type to a filesystem type entry
 */
struct _fstypes *get_fs(s)
    char *s;
{
    int i;

    if (s && *s) {
	    for (i=0;fstypes[i].name;i++) {
		    if (!strcasecmp(fstypes[i].name, s))
			    return (&fstypes[i]);
	    }
    }
    return (NULL);
}

/*
 * Build a Hesiod line either from a Hesiod query or internal frobbing
 * if explicit is set.
 */
char **build_hesiod_line(name)
    char *name;
{
    char **realhes;
    struct _fstypes	*fsp;
    
    if (!explicit) {
	    realhes = conf_filsys_resolve(name);
#ifdef HESIOD
	    if (!realhes || !*realhes)
		    realhes = hes_resolve(name, "filsys");
#endif
	    if (!realhes || !*realhes)
		    fprintf(stderr, "%s: Can't resolve name\n", name);
	    return (realhes);
    }

    fsp = get_fs(filsys_type);
    if (!fsp)
	return (NULL);
    if (!fsp->explicit) {
	fprintf(stderr, "Explicit definitions for type %s not allowed\n",
		filsys_type);
	return (NULL);
    }

    return ((fsp->explicit)(name));
}

/*
 * Parse a Hesiod record
 *
 * Returns 0 on success, -1 on failure
 */
int parse_hes(hes, at, errorname)
    char *hes, *errorname;
    struct _attachtab *at;
{
    char	*cp;
    struct hostent *hent;
    int		type;

    bzero(at, sizeof(struct _attachtab));
    
    if (!*hes)
	    goto bad_hes_line;


    at->fs = get_fs(strtok(hes, TOKSEP));
    if (!at->fs)
	    goto bad_hes_line;
    type = at->fs->type;

    if (type & TYPE_ERR) {
	    strncpy(at->hostdir, strtok(NULL, ""), sizeof(at->hostdir));
	    return(0);
    }

    if (!(cp = strtok(NULL, TOKSEP)))
	    goto bad_hes_line;
    strcpy(at->hostdir, cp);

    if (type & ~(TYPE_UFS | TYPE_AFS)) {
	    if (!(cp = strtok(NULL, TOKSEP)))
		    goto bad_hes_line;
	    strcpy(at->host, cp);
    } else
	    strcpy(at->host, "localhost");

    if (!(cp = strtok(NULL, TOKSEP)))
	    goto bad_hes_line;
    at->mode = *cp;
    if (at->mode == 'x')
	    at->mode = 'w';  /* Backwards compatibility */
    if (at->fs->good_flags) {
	    if (!index(at->fs->good_flags, at->mode))
		    goto bad_hes_line;	/* Bad attach mode */
    }

    if (!(cp = strtok(NULL, TOKSEP)))
	    goto bad_hes_line;
    strcpy(at->mntpt, cp);

    if (type & ~(TYPE_UFS | TYPE_AFS)) {
	    hent = gethostbyname(at->host);
	    if (!hent) {
		    fprintf(stderr,"%s: Can't resolve host %s\n",errorname,
			    at->host);
		    fprintf(stderr, abort_msg);
		    return(-1);
	    }
	    bcopy(hent->h_addr_list[0], &at->hostaddr.s_addr, 4);
	    strcpy(at->host, hent->h_name);
    } else
	    at->hostaddr.s_addr = (long) ntohl(0x7f000001); /* 127.0.0.1 */
    return(0);
    
bad_hes_line:
    fprintf(stderr,"Badly formatted filesystem definition\n");
    fprintf(stderr, abort_msg);
    return(-1);
}

/*
 * Make the directories necessary for a mount point - set the number
 * of directories created in the attachtab structure
 */
make_mntpt(at)
    struct _attachtab *at;
{
    char bfr[BUFSIZ],*ptr;
    int oldmask;
    struct stat statbuf;

    strcpy(bfr, at->mntpt);
    if (at->fs->flags & FS_PARENTMNTPT) {
	    ptr = rindex(bfr, '/');
	    if (ptr)
		    *ptr = 0;
	    else
		    return(SUCCESS);
    }
    
    if (!stat(bfr, &statbuf)) {
	if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
	    return (SUCCESS);
	fprintf(stderr, "%s: %s is not a directory\n", at->hesiodname,
		at->mntpt);
	return (FAILURE);
    }
    
    oldmask = umask(0);

    ptr = bfr+1;		/* Pass initial / */

    at->rmdir = 0;
	
    while (ptr && *ptr) {
	strcpy(bfr, at->mntpt);
	ptr = index(ptr, '/');
	if (ptr)
	    *ptr++ = '\0';
	if (debug_flag)
		printf("Making directory %s (%s)\n", bfr, ptr ? ptr : "");
	if (mkdir(bfr, 0777)) {
	    if (errno == EEXIST)
		continue;
	    fprintf(stderr, "%s: Can't create directory %s: %s\n",
		    at->hesiodname, bfr, sys_errlist[errno]);
	    umask(oldmask);
	    return (FAILURE);
	}
	else
	    at->rmdir++;
    }
    umask(oldmask);
    return (SUCCESS);
}

/*
 * Delete a previously main mountpoint
 */
rm_mntpt(at)
    struct _attachtab *at;
{
    char bfr[BUFSIZ], *ptr;

    strcpy(bfr, at->mntpt);
    ptr = bfr;

    if (at->fs->flags & FS_PARENTMNTPT) {
	    ptr = rindex(bfr, '/');
	    if (ptr)
		    *ptr = 0;
	    else
		    return(SUCCESS);
    }
    while (at->rmdir--) {
	if (debug_flag)
		printf("Deleting directory %s (%s)\n", bfr, ptr ? ptr : "");
	if (rmdir(bfr)) {
	    if (errno != ENOENT) {
		fprintf(stderr,
			"%s: Can't remove directory %s: %s\n",
			at->hesiodname, ptr,
			sys_errlist[errno]);
		return (FAILURE);
	    }
	}
	ptr = rindex(bfr, '/');
	if (ptr)
	    *ptr = '\0';
	else
	    return (SUCCESS);
    }
    return (SUCCESS);
}

/*
 * Internal spiffed up getopt
 */
internal_getopt(arg, cl)
    char *arg;
    struct command_list *cl;
{
    int i;

    for (i=0;cl[i].small;i++) {
	if (cl[i].large && !strcmp(cl[i].large, arg))
	    return (cl[i].small[1]);
	if (!strcmp(cl[i].small, arg))
	    return (cl[i].small[1]);
    }
    return ('?');
}

/*
 * Format a hesiod name into a name in /tmp...including replacing /
 * with @, etc.
 */
make_temp_name(filename, name)
    char *filename;
    char *name;
{
    strcpy(filename, "/tmp/attach_");
    filename = filename+strlen(filename);
    
    while (*name) {
	if (*name == '/')
	    *filename++ = '@';
	else
	    *filename++ = *name;
	name++;
    }
    *filename = '\0';
}

/*
 * Check to see if a filesystem is really being frobbed with by
 * another attach process.
 */

really_in_use(name)
    char *name;
{
    int fd, ret;
    char filename[BUFSIZ];

    make_temp_name(filename, name);

    if (debug_flag)
	printf("Checking lock on %s\n", filename);
    
    fd = open(filename, O_RDWR, 0644);
    if (!fd)
	return (0);

    ret = (flock(fd, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);

    close(fd);
    return (ret);
}

/*
 * Mark a filesystem as being frobbed with
 */

mark_in_use(name)
    char *name;
{
    static int fd;
    static char filename[BUFSIZ];

    if (!name) {
	if (debug_flag)
	    printf("Removing lock on %s\n", filename);
	close(fd);
	unlink(filename);
	return;
    }

    make_temp_name(filename, name);

    if (debug_flag)
	printf("Setting lock on %s: ", filename);

    /*
     * Unlink the old file in case someone else already has a lock on
     * it...we'll override them with our new file.
     */
    unlink(filename);
    fd = open(filename, O_CREAT, 0644);
    if (!fd) {
	    fprintf(stderr,"Can't open %s: %s\n", ATTACHTAB,
		    sys_errlist[errno]);
	    fprintf(stderr, abort_msg);
	    exit(ERR_FATAL);
    }

    if (debug_flag)
	    printf("%d\n", fd);

    flock(fd, LOCK_EX);
}

#ifdef DEBUG
/*
 * Dump used file descriptors...useful for debugging
 */

fd_dump()
{
    int i;
    char b;

    printf("FD's in use: ");
    for (i=0;i<64;i++)
	if (read(i,&b,0) >= 0)
	    printf("%d ",i);
    printf("\n");
}
#endif

/*
 * String comparison for filesystem names - case insensitive for hesiod,
 * sensitive for explicit.
 */

hescmp(at, s)
    struct _attachtab *at;
    char *s;
{
    if (at->explicit)
	return (strcmp(at->hesiodname, s));
    return (strcasecmp(at->hesiodname, s));
}

/*
 * Duplicate a string in malloc'ed memory
 */
char *strdup(s)
	char	*s;
{
	register char	*cp;
	
	if (!(cp = malloc(strlen(s)+1))) {
		printf("Out of memory!!!\n");
		abort();
	}
	return(strcpy(cp,s));
}

/*
 * Check to see if we have root priv's; give an error if we don't.
 */
check_root_privs(progname)
	char	*progname;
{
	if (!getuid() || !geteuid() || (debug_flag))
		return;
	fprintf(stderr,
		"%s must be setuid to root for this operation to succeed.\n",
		progname);
	exit(ERR_FATAL);
}

add_options(mopt, string)
	struct mntopts	*mopt;
	char		*string;
{
	char	*next, *arg, *str, *orig_str;

	orig_str = str = strdup(string);
	while (str && *str) {
		next = index(str, ',');
		if (next) {
			*next++ = '\0';
		}
		arg = index(str, '=');
		if (arg)
			*arg++ = '\0';
		if (!strcmp(str, "ro")) {
			mopt->flags |= M_RDONLY;
#if defined(ultrix) && defined(NFS)
			if (mopt->type == MOUNT_NFS) {
			    mopt->tsa.nfs.gfs_flags |= M_RONLY;
			    mopt->tsa.nfs.flags |= NFSMNT_RONLY;
			}
#endif /* ultrix && NFS */
		} else if (!strcmp(str, "rw")) {
#ifdef ultrix
		    mopt->flags &= ~M_RDONLY;
		    mopt->tsa.nfs.gfs_flags &= ~M_RONLY;
		    mopt->tsa.nfs.flags &= ~NFSMNT_RONLY;
#else /* !ultrix */
		    mopt->flags &= ~M_RDONLY;
#endif /* ultrix */
		}
#ifdef ultrix
		/* process other ultrix options */
		else if (!strcmp(str, "force")) {
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_FORCE;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_FORCE;
		} else if (!strcmp(str, "sync")) {
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_SYNC;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_SYNC;
		} else if (!strcmp(str, "pgthresh")) {
		    int pgthresh;

		    if (!arg) {
			missarg("pgthresh");
			continue;
		    }
		    pgthresh = atoi(arg);
		    pgthresh = max (pgthresh, MINPGTHRESH/PGUNITS);
#ifdef NFS		    
		    if (mopt->type == MOUNT_NFS) {
			mopt->tsa.nfs.pg_thresh = pgthresh;
			mopt->tsa.nfs.flags |= NFSMNT_PGTHRESH;
		    } else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_pgthresh = pgthresh;
		} else if (!strcmp(str, "quota")) {
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_QUOTA;
		} else if (!strcmp(str, "noexec")) {
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_NOEXEC;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_NOEXEC;
		} else if (!strcmp(str, "nocache")) {
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_NOCACHE;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_NOCACHE;
		} else if (!strcmp(str, "nodev")) {
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_NODEV;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_NODEV;
		}
#endif /* ultrix */

		else if (!strcmp(str, "nosuid")) {
#ifdef ultrix
#ifdef NFS
		    if (mopt->type == MOUNT_NFS)
			mopt->tsa.nfs.gfs_flags |= M_NOSUID;
		    else
#endif /* NFS */
		    if (mopt->type == MOUNT_UFS)
			mopt->tsa.ufs.ufs_flags |= M_NOSUID;
#else /* !ultrix */
		    mopt->flags |= M_NOSUID;
#endif /* ultrix */
		}
#ifdef NFS
		else if (mopt->type == MOUNT_NFS) {
			if (!strcmp(str, "soft"))
				mopt->tsa.nfs.flags |= NFSMNT_SOFT;
			else if (!strcmp(str, "hard"))
				mopt->tsa.nfs.flags &= ~NFSMNT_SOFT;
			else if (!strcmp(str, "rsize")) {
				if (!arg) {
					missarg("rsize");
				}
				mopt->tsa.nfs.rsize = atoi(arg);
				mopt->tsa.nfs.flags |= NFSMNT_RSIZE;
			} else if (!strcmp(str, "wsize")) {
				if (!arg) {
					missarg("wsize");
					continue;
				}
				mopt->tsa.nfs.wsize = atoi(arg);
				mopt->tsa.nfs.flags |= NFSMNT_WSIZE;
			} else if (!strcmp(str, "timeo")) {
				if (!arg) {
					missarg("timeo");
					continue;
				}
				mopt->tsa.nfs.timeo = atoi(arg);
				mopt->tsa.nfs.flags |= NFSMNT_TIMEO;
			} else if (!strcmp(str, "retrans")) {
				if (!arg) {
					missarg("retrans");
					continue;
				}
				mopt->tsa.nfs.retrans = atoi(arg);
				mopt->tsa.nfs.flags |= NFSMNT_RETRANS;
			} else if (!strcmp(str, "port")) {
				if (!arg) {
					missarg("port");
					continue;
				}
				mopt->nfs_port = atoi(arg);
			}
#ifdef ultrix
			else if (!strcmp(str, "intr")) {
			    mopt->tsa.nfs.flags |= NFSMNT_INT;
			}
#endif /* ultrix */
		    }
#endif
		str = next;
	}
	free(orig_str);
}

static missarg(arg)
	char	*arg;
{
	fprintf(stderr,
		"%s: missing argument to mount option; ignoring.\n",
		arg);
}

/*
 * Return a string describing the options in the mount options structure
 */
char *stropt(mopt)
	struct mntopts	mopt;
{
	static char	buff[BUFSIZ];
	char	tmp[80];

	buff[0] = '\0';
	
	if (mopt.flags & M_RDONLY)
		(void) strcat(buff, ",ro");
	else
		(void) strcat(buff, ",rw");
#ifdef M_NOSUID
#ifdef ultrix 
#ifdef NFS
	if (mopt.type == MOUNT_NFS && (mopt.tsa.nfs.gfs_flags & M_NOSUID))
	    (void) strcat(buff, ",nosuid");
	else
#endif /* NFS */
	if (mopt.type == MOUNT_UFS && (mopt.tsa.ufs.ufs_flags & M_NOSUID))
	    (void) strcat(buff, ",nosuid");
#else /* !ultrix */
	if (mopt.flags & M_NOSUID)
		(void) strcat(buff, ",nosuid");
#endif /* ultrix */
#endif /* M_NOSUID */
#ifdef NFS
	if (mopt.type == MOUNT_NFS) {
#ifdef ultrix
		if (mopt.tsa.nfs.gfs_flags & M_FORCE)
			(void) strcat(buff, ",force");
		if (mopt.tsa.nfs.gfs_flags & M_SYNC)
			(void) strcat(buff, ",force");
		if (mopt.tsa.nfs.flags & NFSMNT_PGTHRESH) {
			(void) strcat(buff, ",pgthresh=");
			(void) sprintf(tmp, "%d", mopt.tsa.nfs.pg_thresh);
			(void) strcat(buff, tmp);
		}
		if (mopt.tsa.nfs.gfs_flags & M_NOEXEC)
			(void) strcat(buff, ",noexec");
		if (mopt.tsa.nfs.gfs_flags & M_NOCACHE)
			(void) strcat(buff, ",nocache");
		if (mopt.tsa.nfs.gfs_flags & M_NODEV)
			(void) strcat(buff, ",nodev");
		if (mopt.tsa.nfs.flags & NFSMNT_INT)
			(void) strcat(buff, ",intr");
#endif /* ultrix */
		if (mopt.tsa.nfs.flags & NFSMNT_SOFT)
			(void) strcat(buff, ",soft");
		if (mopt.tsa.nfs.flags & NFSMNT_RSIZE) {
			(void) strcat(buff, ",rsize=");
			(void) sprintf(tmp, "%d", mopt.tsa.nfs.rsize);
			(void) strcat(buff, tmp);
		}
		if (mopt.tsa.nfs.flags & NFSMNT_WSIZE) {
			(void) strcat(buff, ",wsize=");
			(void) sprintf(tmp, "%d", mopt.tsa.nfs.wsize);
			(void) strcat(buff, tmp);
		}
		if (mopt.tsa.nfs.flags & NFSMNT_TIMEO) {
			(void) strcat(buff, ",timeo=");
			(void) sprintf(tmp, "%d", mopt.tsa.nfs.timeo);
			(void) strcat(buff, tmp);
		}
		if (mopt.tsa.nfs.flags & NFSMNT_RETRANS) {
			(void) strcat(buff, ",retrans=");
			(void) sprintf(tmp, "%d", mopt.tsa.nfs.retrans);
			(void) strcat(buff, tmp);
		}
		if (mopt.nfs_port) {
			(void) strcat(buff, ",port=");
			(void) sprintf(tmp, "%d", mopt.nfs_port);
			(void) strcat(buff, tmp);
		}
	}
#endif /* NFS */
#ifdef ultrix
	else if (mopt.type == MOUNT_UFS) {
		if (mopt.tsa.ufs.ufs_flags & M_QUOTA)
			(void) strcat(buff, ",quota");
		if (mopt.tsa.ufs.ufs_flags & M_FORCE)
			(void) strcat(buff, ",force");
		if (mopt.tsa.ufs.ufs_flags & M_SYNC)
			(void) strcat(buff, ",force");
		if (mopt.tsa.ufs.ufs_pgthresh != DEFPGTHRESH) {
			(void) strcat(buff, ",pgthresh=");
			(void) sprintf(tmp, "%d", mopt.tsa.ufs.ufs_pgthresh);
			(void) strcat(buff, tmp);
		}
		if (mopt.tsa.ufs.ufs_flags & M_NOEXEC)
			(void) strcat(buff, ",noexec");
		if (mopt.tsa.ufs.ufs_flags & M_NOCACHE)
			(void) strcat(buff, ",nocache");
		if (mopt.tsa.ufs.ufs_flags & M_NODEV)
			(void) strcat(buff, ",nodev");
	    }
#endif /* ultrix */

	return(buff+1);
}

/*
 * Display the userid, given a uid (if possible)
 */
char *struid(uid)
	int	uid;
{
	struct	passwd	*pw;
	static char	buff[256];

	pw = getpwuid(uid);
	if (pw)
		strcpy(buff, pw->pw_name);
	else
		sprintf(buff, "#%d", uid);
	return(buff);
}

/*
 * Compare if two hosts are the same
 */
int host_compare(host1, host2)
	char	*host1;
	char	*host2;
{
	char	bfr[BUFSIZ];
	static	char	last_host[BUFSIZ] = "********";
	static	struct in_addr	sin;
	struct hostent	*host;

	/*
	 * Cache the last host1, for efficiency's sake.
	 */
	if (strcmp(host1, last_host)) {
		strcpy(last_host, host1);
		if (host = gethostbyname(host1)) 
			bcopy(host->h_addr, &sin, (sizeof sin));
		else {
			if (debug_flag) {
				sprintf(bfr, "%s: gethostbyname", host1);
				perror(bfr);
			}
			return(!strcmp(host1, host2));
		}
	}
	if (host = gethostbyname(host2))
		return(!bcmp(&sin, host->h_addr, (sizeof sin)));
	else {
		if (debug_flag) {
			sprintf(bfr, "%s: gethostbyname", host2);
			perror(bfr);
		}
		return(!strcmp(host1, host2));
	}
}

/*
 * Owner list code, originally written by jfc
 */
int clean_attachtab(atp)
	struct _attachtab *atp;
{
	struct passwd *pw;
	int i;
	for(i=0;i<atp->nowners;i++)
		if(NULL == (pw = getpwuid(atp->owners[i]))) {
			int j;
			for(j=i+1;j<atp->nowners;j++)
				atp->owners[j-1] = atp->owners[j];
			atp->nowners--;
			i--;
		}
	return atp->nowners;
}

void add_an_owner(atp,uid)
        uid_t uid;
        struct _attachtab *atp;
{
	register int i;
	for(i=0;i<atp->nowners;i++)
		if(atp->owners[i] == uid)
			return;
	if(atp->nowners < MAXOWNERS)
		atp->owners[atp->nowners++] = uid;
	else
	  /* fail silently */;
}

int is_an_owner(at,uid)
        uid_t uid;
        struct _attachtab *at;
{
	register int i;
	if (!at->nowners)
		return(1);	/* If noone claims it, anyone can have it */
	for(i=0;i<at->nowners;i++)
		if(at->owners[i] == uid)
			return 1;
	return 0;
}

int del_an_owner(at,uid)
        uid_t uid;
        struct _attachtab *at;
{
	register int i;
	for(i=0;i<at->nowners;i++)
		if(at->owners[i] == uid) {
			--at->nowners;
			for(;i<at->nowners;i++)
				at->owners[i] = at->owners[i+1];
			return at->nowners;
		}
	if(at->nowners == 0)
		at->owners[0] = -1;
	return at->nowners;
}

char *ownerlist(atp)
        struct _attachtab *atp;
{
	static char ret[256];
	int i,len=1;
	if(atp->nowners == 0)
		return("{}");
	else if(atp->nowners == 1)
		return struid(atp->owners[0]);

	ret[0] = '{';
	for(i=0;i<atp->nowners;i++) {
		char *u = struid(atp->owners[i]);
		int tmp = strlen(u);
		if(i)
			ret[len++] = ',';
		if(len+tmp >= 255) {
			ret[len++] = '}';
			ret[len] = '\0';
			return ret;
		}
		strcpy(ret+len,u);
		len += tmp;
	}
	ret[len++] = '}';
	ret[len] = '\0';
	return ret;
}

