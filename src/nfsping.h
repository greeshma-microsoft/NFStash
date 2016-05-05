#ifndef NFSPING_H
#define NFSPING_H

#define _GNU_SOURCE /* for asprintf */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/rpc.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <errno.h>

/* local copies */
/* TODO do these need to be included in all utilities? */
#include "rpcsrc/nfs_prot.h"
#include "rpcsrc/mount.h"
#include "rpcsrc/pmap_prot.h"
#include "rpcsrc/nlm_prot.h"
#include "rpcsrc/nfsv4_prot.h"
#include "rpcsrc/nfs_acl.h"
#include "rpcsrc/sm_inter.h"
#include "rpcsrc/rquota.h"
#include "rpcsrc/klm_prot.h"

/* BSD timespec functions */
#include "src/timespec.h"

/* Parson JSON */
#include "parson/parson.h"

#define fatal(x...) do { fflush(stdout); fprintf(stderr,x); fflush(stderr); usage(); } while (0)
#define fatalx(x, y...) do { fflush(stdout); fprintf(stderr,y); fflush(stderr); exit(x); } while (0)
#define debug(x...) do { if (verbose) { fflush(stdout); fprintf(stderr,x); fflush(stderr); } } while (0)

/* struct timeval */
/* timeout for RPC requests, keep it the same (or lower) than the sleep time below */
#define NFS_TIMEOUT { 1, 0 }
/* struct timespec */
/* time to wait between pings */
#define NFS_WAIT { 0, 25000000 }
/* struct timespec */
/* polling frequency */
#define NFS_HERTZ 1

/* maximum number of digits that can fit in a 64 bit time_t seconds (long long int) for use with strftime() */
/* 9223372036854775807 is LLONG_MAX, add one for a '-' (just in case!) and another for a terminating NUL */
#define TIME_T_MAX_DIGITS 21

typedef struct targets {
    char *name;
    char *ndqf; /* reversed name */
    char *ip_address; /* the IP address as a string */
    struct sockaddr_in *client_sock; /* used to store the port number and connect to the RPC client */
    CLIENT *client; /* RPC client */
    /* for fping output when we need to store the individual results for the summary */
    unsigned long *results;
    unsigned int sent, received;
    unsigned long min, max;
    float avg;
    /* list of filesystem exports */
    struct mount_exports *exports;

    struct targets *next;
} targets_t;

/* MOUNT protocol filesystem exports */
struct mount_exports {
    char path[MNTPATHLEN];
    /* for fping output when we need to store the individual results for the summary */
    unsigned long *results;
    unsigned int sent, received;
    unsigned long min, max;
    float avg;
    JSON_Value *json_root; /* the JSON object for output */

    struct mount_exports *next;
};

/* a singly linked list of nfs filehandles */
typedef struct nfs_fh_list {
    char *host;
    struct sockaddr_in *client_sock;
    char *path;
    nfs_fh3 nfs_fh; /* generic name so we can include v2/v4 later */
    struct nfs_fh_list *next;
} nfs_fh_list;

/* TODO capitalise? */
enum outputs {
    unset, /* use as a default for getopt checks */
    ping, /* classic ping */
    fping,
    unixtime,
    showmount,
    graphite,
    statsd,
    json
};

/* for NULL procedure function pointers */
typedef void *(*proc_null_t)(void *, CLIENT *);

struct null_procs {
    /* function pointer */
    proc_null_t proc;
    /* store the name as a string for error messages */
    char *name;
    /* protocol name for output functions */
    char *protocol;
    /* protocol version */
    u_long version;
};

#endif /* NFSPING_H */
