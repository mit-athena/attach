/*	Created by:	Robert French
 *
 *	$Source: /afs/dev.mit.edu/source/repository/athena/bin/attach/rpc.c,v $
 *	$Author: jfc $
 *
 *	Copyright (c) 1988 by the Massachusetts Institute of Technology.
 */

#ifndef lint
static char rcsid_rpc_c[] = "$Header: /afs/dev.mit.edu/source/repository/athena/bin/attach/rpc.c,v 1.1 1990-04-19 12:12:30 jfc Exp $";
#endif lint

#include "attach.h"
#ifdef NFS
#include <sys/socket.h>
#include <krb.h>
#include <rpcsvc/mount.h>
#include <sys/param.h>

extern bool_t	xdr_void();
extern char	*krb_getrealm();

static struct cache_ent rpc_cache[RPC_MAXCACHE];
static int first_free = 0;

/*
 * XDR for sending a Kerberos ticket - sends the whole KTEXT block,
 * but this is very old lossage, and nothing that can really be fixed
 * now.
 */

bool_t xdr_krbtkt(xdrs, authp)
    XDR *xdrs;
    KTEXT_ST *authp;
{
    return xdr_opaque(xdrs, authp, sizeof(KTEXT_ST));
}

/*
 * Maintain a cache of RPC handles and error status.
 */

struct cache_ent *lookup_cache_ent(addr)
    struct in_addr addr;
{
    int i;

    for (i=0;i<first_free;i++)
	if (!bcmp(&addr, &rpc_cache[i].addr, sizeof(addr)))
	    return (&rpc_cache[i]);

    return (NULL);
}

add_cache_ent(addr, handle, fd, sin)
    struct in_addr addr;
    CLIENT *handle;
    int fd;
    struct sockaddr_in *sin;
{
    int ind;

    /* We lose a tiny bit of efficiency if we overflow the cache */
    if (first_free == RPC_MAXCACHE) {
	ind = 0;
	clnt_destroy(rpc_cache[0].handle);
	close(rpc_cache[0].fd);
    }
    else
	ind = first_free++;
    rpc_cache[ind].addr = addr;
    rpc_cache[ind].handle = handle;
    rpc_cache[ind].fd = fd;
    rpc_cache[ind].sin = *sin;
    rpc_cache[ind].error = 0;
}

errored_out(addr)
    struct in_addr addr;
{
    struct cache_ent *ent;

    if (ent = lookup_cache_ent(addr))
	return (ent->error);
    return (0);
}

mark_errored(addr)
    struct in_addr addr;
{
    struct cache_ent *ent;

    if (ent = lookup_cache_ent(addr))
	ent->error = 1;
}

clear_errored(addr)
    struct in_addr addr;
{
    struct cache_ent *ent;

    if (ent = lookup_cache_ent(addr))
	ent->error = 0;
}

/*
 * Create an RPC handle for the given address.  Attempt to use one
 * already cached if possible.
 */

CLIENT *rpc_create(addr, sinp)
    struct in_addr addr;
    struct sockaddr_in *sinp;
{
    CLIENT *client;
    struct sockaddr_in sin, sin2;
    struct timeval timeout;
    struct cache_ent *ent;
    int s;
    int err;
    u_short port;

    if (debug_flag)
	printf("Getting RPC handle for %s\n", inet_ntoa(addr));

    if (ent = lookup_cache_ent(addr)) {
	if (debug_flag)
	    printf("Using cached entry! FD = %d Error = %d\n", ent->fd,
		   ent->error);
	if (ent->error)
	    /* Error status will already be set */
	    return ((CLIENT *)0);
	else {
	    *sinp = ent->sin;
	    return (ent->handle);
	}
    }
    
    sin.sin_family = AF_INET;
    sin.sin_addr = addr;
    sin.sin_port = 0;
    s = RPC_ANYSOCK;
    timeout.tv_usec = 0;
    timeout.tv_sec = 20;

    if (!(client = clntudp_create(&sin, MOUNTPROG, MOUNTVERS,
				  timeout, &s))) {
	if (debug_flag)
	    printf("clntudp_create failed!\n");
	add_cache_ent(addr, 0, 0, sinp);
	mark_errored(addr);
	error_status = ERR_HOST;
	return (client);
    }

    *sinp = sin;
    
    get_myaddress(&sin2);
    sin2.sin_family = AF_INET;
    err = 1;
    for (port=IPPORT_RESERVED-1;err && port >= IPPORT_RESERVED/2;
	 port--) {
	sin2.sin_port = htons(port);
	err = bind(s, &sin2, sizeof(sin2));
	if (err) {
		err = errno;
		if (err == EINVAL) {
			/*
			 * On the NeXT, and possibly other Mach machines,
			 * the socket from clntudp_create is already bound.
			 * So if it is, we'll just go on ahead.
			 */
			err = 0;
			break;
		}
	}
    }

    add_cache_ent(addr, client, s, sinp);
    if (debug_flag)
	printf("Adding cache entry with FD = %d\n", s);

    if (err) {
	if (debug_flag) {
		printf("Couldn't allocate a reserved port!\n");
		errno = err;
		perror("bind");
	}
	clnt_destroy(client);
	client = (CLIENT *)0;
	mark_errored(addr);
	error_status = ERR_NOPORTS;
    } 
	
    return (client);
}

/*
 * spoofunix_create_default -- varient of authunix_create_default, with
 * 	a neat trick....
 * 
 * Written by Mark W. Eichin <eichin@athena.mit.edu>
 */

/* In our case, NGRPS (for RPC) is 8, but NGROUPS (kernel) is 16.  So
 * getgroups fails causing abort() if the user is in >8 groups.  We
 * ask for all groups, but only pass on the first 8. */

AUTH *
spoofunix_create_default(spoofname)
	char *spoofname;
{
	register int len;
	char machname[MAX_MACHINE_NAME + 1];
	register int uid;
	register int gid;
#ifdef notdef
	int gids[NGRPS];
#else
	int gids[NGROUPS];
#endif

	if (spoofname)
		(void) strncpy(machname, spoofname, MAX_MACHINE_NAME);
	else
		if (gethostname(machname, MAX_MACHINE_NAME) == -1) {
			perror("gethostname");
			abort();
		}
	machname[MAX_MACHINE_NAME] = 0;
	uid = geteuid();
	gid = getegid();
#ifdef notdef
	if ((len = getgroups(NGRPS, gids)) < 0) {
#else
	if ((len = getgroups(NGROUPS,gids)) < 0) {
#endif
		fprintf(stderr,"Fatal error: User in too many groups!\n");
		exit(1);
	} 
#ifdef notdef
	return (authunix_create(machname, uid, gid, len, gids));
#else
	return (authunix_create(machname, uid, gid,
				(len > NGRPS) ? NGRPS : len, gids));
#endif
}
	
/*
 * Perform a mapping operation of some kind on the indicated host.  If
 * errorout is zero, don't print error messages.  errname is the name
 * of the filesystem/host/whatever to prefix error message with.
 */

nfsid(host, addr, op, errorout, errname, inattach)
    char *host;
    struct in_addr addr;
    int op, errorout;
    char *errname;
    int inattach;
{
    CLIENT *client;
    KTEXT_ST authent;
    int status;
    struct timeval timeout;
    struct sockaddr_in sin;
    enum clnt_stat rpc_stat;
    char instance[INST_SZ+REALM_SZ+1];
    char *realm;
    char *ptr;

    if (debug_flag)
	printf("nfsid operation on %s, op = %d\n", inet_ntoa(addr), op);
    
    /*
     * Check for previous error condition on this host
     */
    if (errored_out(addr)) {
	if (debug_flag)
	    printf("Host previously errored - ignoring\n");
	if (errorout)
	    fprintf(stderr, "%s: Ignoring %s due to previous host errors\n",
		    errname, host);
	/*
	 * Don't bother setting error status...we already will have
	 * before
	 */
	return (FAILURE);
    }

#ifdef KERBEROS
    /*
     * Mapping and user purging are the only authenticated functions...
     */
    if (op == MOUNTPROC_KUIDMAP || op == MOUNTPROC_KUIDUPURGE) {
	if (debug_flag)
	    printf("nfsid operation requiring authentication...op = %d\n",
		   op);
#ifdef OLD_KERBEROS
	realm = (char *) krb_getrealm(host);
#else
	realm = (char *) krb_realmofhost(host);
#endif
	strcpy(instance, host);
	ptr = index(instance, '.');
	if (ptr)
	    *ptr = '\0';
	for (ptr=instance;*ptr;ptr++)
	    if (isupper(*ptr))
		*ptr = tolower(*ptr);

	if (debug_flag)
	    printf("krb_mk_req for instance %s, realm %s\n", instance, realm);

	status = krb_mk_req(&authent, KERB_NFSID_INST, instance,
			   realm, 0);
	if (status != KSUCCESS) {
	    if (debug_flag)
		printf("krb_mk_req failed! status = %d\n", status);
	    if (status == KDC_PR_UNKNOWN) {
		    fprintf(stderr, "%s: (warning) Host %s isn't registered\n",
			    errname, host);
		    fprintf(stderr, "%s: with kerberos; mapping failed.\n",
			    errname);
		    return(SUCCESS);
	    }
	    if (errorout)
		fprintf(stderr, "%s: Could not get Kerberos ticket for realm %s instance %s:\n%s\n",
			errname, realm, instance,
			krb_err_txt[status]);
	    error_status = ERR_KERBEROS;
	    return (FAILURE);
	}
    }
#endif

    /*
     * Get an RPC handle
     */
    if ((client = rpc_create(addr, &sin)) == NULL) {
	if (errorout)
	    fprintf(stderr, "%s: Server %s not responding\n", errname, host);
	/*
	 * Error status set by rpc_create
	 */
	return (FAILURE);
    }

    setreuid(geteuid(), getuid());
    client->cl_auth = spoofunix_create_default(spoofhost);
    setreuid(geteuid(), getuid());
	
    timeout.tv_usec = 0;
    timeout.tv_sec = 20;

    if (op == MOUNTPROC_KUIDMAP || op == MOUNTPROC_KUIDUPURGE)
	rpc_stat = clnt_call(client, op, xdr_krbtkt, &authent,
			     xdr_void, 0, timeout);
    else
	rpc_stat = clnt_call(client, op, xdr_void, 0, xdr_void,
			     0, timeout);
    if (rpc_stat != RPC_SUCCESS) {
	if (errorout) {
	    switch (rpc_stat) {
	    case RPC_TIMEDOUT:
		fprintf(stderr, "%s: Timeout while contacting mount daemon on %s\n",
			errname, host);
		if (inattach)
		    fprintf(stderr, "%s:   while mapping - try attach -n\n",
			    errname);
		error_status = ERR_HOST;
		break;
	    case RPC_AUTHERROR:
		fprintf(stderr, "%s: Authentication failed\n",
			errname, host);
		error_status = ERR_AUTHFAIL;
		break;
	    case RPC_PMAPFAILURE:
		fprintf(stderr, "%s: Can't find mount daemon on %s\n",
			errname, host);
		error_status = ERR_HOST;
		break;
	    case RPC_PROGUNAVAIL:
	    case RPC_PROGNOTREGISTERED:
		fprintf(stderr, "%s: Mount daemon not available on %s\n",
			errname, host);
		error_status = ERR_HOST;
		break;
	    case RPC_PROCUNAVAIL:
		fprintf(stderr, "%s: (warning) Mount daemon on %s doesn't understand UID maps\n",
		       errname, host);
		return(SUCCESS);
	    default:
		fprintf(stderr, "%s: System error contacting server %s\n",
			errname, host);
		error_status = ERR_HOST;
		break;
	    }
	} 
	mark_errored(addr);
	if (debug_flag)
	    clnt_perror(client, "RPC return status");
	return (FAILURE);
    }
    return (SUCCESS);
}
#endif