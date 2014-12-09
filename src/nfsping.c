#include "nfsping.h"
#include "util.h"
#include "rpc.h"
#include "string.h"

volatile sig_atomic_t quitting;
int verbose = 0;

/* dispatch table for null function calls, this saves us from a bunch of if statements */
/* array is [protocol number][protocol version] */
/* protocol versions should relate to the corresponding NFS protocol */
/* for example, mount protocol version 1 is used with nfs version 2, so store it at index 2 */

/* waste a bit of memory with a mostly empty array */
/* TODO have another struct to map RPC protocol numbers to lower numbers and get the array size down? */
/* rpc protocol numbers are offset by 100000, ie NFS = 100003 */
static const struct null_procs null_dispatch[][5] = {
    /* mount version 1 was used with nfs v2 */
    [MOUNTPROG - 100000]      [2] = { .proc = mountproc_null_1, .name = "mountproc_null_1", .protocol = "mountv1", .version = 1 },
    [MOUNTPROG - 100000]      [3] = { .proc = mountproc_null_3, .name = "mountproc_null_3", .protocol = "mountv3", .version = 3 },
    /* nfs v4 has mounting built in */
    /* only one version of portmap protocol */
    [PMAPPROG - 100000]       [2] = { .proc = pmapproc_null_2, .name = "pmapproc_null_2", .protocol = "portmap", .version = 2 },
    [PMAPPROG - 100000]       [3] = { .proc = pmapproc_null_2, .name = "pmapproc_null_2", .protocol = "portmap", .version = 2 },
    [PMAPPROG - 100000]       [4] = { .proc = pmapproc_null_2, .name = "pmapproc_null_2", .protocol = "portmap", .version = 2 },
    /* nfs v2 didn't have locks, v4 has it integrated into the nfs protocol */
    /* NLM version 4 is used by NFS version 3 */
    [NLM_PROG - 100000]       [3] = { .proc = nlm4_null_4, .name = "nlm4_null_4", .protocol = "nlmv4", .version = 4 },
    /* nfs */
    [NFS_PROGRAM - 100000]    [2] = { .proc = nfsproc_null_2, .name = "nfsproc_null_2" , .protocol = "nfsv2", .version = 2 },
    [NFS_PROGRAM - 100000]    [3] = { .proc = nfsproc3_null_3, .name = "nfsproc3_null_3", .protocol = "nfsv3", .version = 3 },
    [NFS_PROGRAM - 100000]    [4] = { .proc = nfsproc4_null_4, .name = "nfsproc4_null_4", .protocol = "nfsv4", .version = 4 },
    /* nfs acl */
    [NFS_ACL_PROGRAM - 100000][2] = { .proc = aclproc2_null_2, .name = "aclproc2_null_2", .protocol = "nfs_aclv2", .version = 2 },
    [NFS_ACL_PROGRAM - 100000][3] = { .proc = aclproc3_null_3, .name = "aclproc3_null_3", .protocol = "nfs_aclv3", .version = 3 },
    /* nfs v4 has ACLs built in */
    /* NSM network status monitor, only has one version */
    /* call it "status" to match rpcinfo */
    [SM_PROG - 100000][2] = { .proc = sm_null_1, .name = "sm_null_1", .protocol = "status", .version = 1 },
    [SM_PROG - 100000][3] = { .proc = sm_null_1, .name = "sm_null_1", .protocol = "status", .version = 1 },
};


void int_handler(int sig) {
    quitting = 1;
}


void usage() {
    struct timeval  timeout    = NFS_TIMEOUT;
    struct timespec wait_time  = NFS_WAIT;
    struct timespec sleep_time = NFS_SLEEP;

    printf("Usage: nfsping [options] [targets...]\n\
    -a         check the NFS ACL protocol (default NFS)\n\
    -A         show IP addresses\n\
    -c n       count of pings to send to target\n\
    -C n       same as -c, output parseable format\n\
    -d         reverse DNS lookups for targets\n\
    -D         print timestamp (unix time) before each line\n\
    -g string  prefix for graphite metric names (default \"nfsping\")\n\
    -h         display this help and exit\n\
    -i n       interval between targets (in ms, default %lu)\n\
    -l         loop forever\n\
    -L         check the network lock manager (NLM) protocol (default NFS)\n\
    -m         use multiple target IP addresses if found\n\
    -M         use the portmapper (default: NFS no, mount/NLM/NSM yes)\n\
    -n         check the mount protocol (default NFS)\n\
    -N         check the portmap protocol (default NFS)\n\
    -o F       output format ([G]raphite, [S]tatsd, default human readable)\n\
    -p n       pause between pings to target (in ms, default %lu)\n\
    -P n       specify port (default: NFS %i, portmap %i)\n\
    -q         quiet, only print summary\n\
    -r n       reconnect to server every n pings\n\
    -s         check the network status monitor (NSM) protocol (default NFS)\n\
    -S addr    set source address\n\
    -t n       timeout (in ms, default %lu)\n\
    -T         use TCP (default UDP)\n\
    -v         verbose output\n\
    -V n       specify NFS version (2/3/4, default 3)\n",
    ts2ms(wait_time), ts2ms(sleep_time), NFS_PORT, PMAPPORT, tv2ms(timeout));

    exit(3);
}


void print_summary(targets_t targets) {
    targets_t *target = &targets;
    double loss;

    while (target) {
        loss = (target->sent - target->received) / (double)target->sent * 100;

        /* check if this is still set to the default value */
        /* that means we never saw any responses */
        if (target->min == ULONG_MAX) {
            target->min = 0;
        }

        fprintf(stderr, "%s : xmt/rcv/%%loss = %u/%u/%.0f%%",
            target->name, target->sent, target->received, loss);
        /* only print times if we got any responses */
        if (target->received) {
            fprintf(stderr, ", min/avg/max = %.2f/%.2f/%.2f",
                target->min / 1000.0, target->avg / 1000.0, target->max / 1000.0);
        }
        fprintf(stderr, "\n");

        target = target->next;
    }
}


/* TODO target output spacing */
/* print a parseable summary string when finished in fping-compatible format */
void print_fping_summary(targets_t targets) {
    targets_t *target = &targets;
    unsigned long i;

    while (target) {
        fprintf(stderr, "%s :", target->name);
        for (i = 0; i < target->sent; i++) {
            if (target->results[i])
                fprintf(stderr, " %.2f", target->results[i] / 1000.0);
            else
                fprintf(stderr, " -");
        }
        fprintf(stderr, "\n");
        target = target->next;
    }
}


/* print formatted output after each ping */
void print_output(enum outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, struct timeval now, unsigned long us) {
    double loss;

    if (format == unixtime) {
        /* FIXME these casts to long aren't great */
        printf("[%ld.%06ld] ", (long)now.tv_sec, (long)now.tv_usec);
    }

    if (format == human || format == fping || format == unixtime) {
        loss = (target->sent - target->received) / (double)target->sent * 100;
        printf("%s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n", target->name, target->sent - 1, us / 1000.0, target->avg / 1000.0, loss);
    } else if (format == graphite || format == statsd) {
        printf("%s.%s.", prefix, target->ndqf);
        printf("%s", null_dispatch[prognum_offset][version].protocol);
        if (format == graphite) {
            printf(".usec %lu %li\n", us, now.tv_sec);
        } else if (format == statsd) {
        /* statsd only takes milliseconds */
            printf(":%03.2f|ms\n", us / 1000.0 );
        }
    }
    fflush(stdout);
}


/* print missing packets for formatted output */
void print_lost(enum outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, struct timeval now) {
    /* send to stdout even though it could be considered an error, presumably these are being piped somewhere */
    /* stderr prints the errors themselves which can be discarded */
    if (format == graphite || format == statsd) {
        printf("%s.%s.", prefix, target->ndqf);
        printf("%s", null_dispatch[prognum_offset][version].protocol);
        if (format == graphite) {
            printf(".lost 1 %li\n", now.tv_sec);
        } else if (format == statsd) {
            /* send it as a counter */
            printf(".lost:1|c\n");
        }
    }
    fflush(stdout);
}


/* make a new target */
targets_t *make_target(char *name, uint16_t port) {
    targets_t *target;

    target = calloc(1, sizeof(targets_t));
    target->next = NULL;
    target->name = name;
    /* always set this even if we might not need it, it should be quick */
    target->ndqf = reverse_fqdn(target->name);

    target->client_sock = calloc(1, sizeof(struct sockaddr_in));
    target->client_sock->sin_family = AF_INET;
    target->client_sock->sin_port = port;

    /* set this so that the first comparison will always be smaller */
    target->min = ULONG_MAX;

    return target;
}


int main(int argc, char **argv) {
    void *status;
    char *error;
    struct timeval timeout = NFS_TIMEOUT;
    struct timeval call_start, call_end;
    struct timespec sleep_time = NFS_SLEEP;
    struct timespec wait_time = NFS_WAIT;
    uint16_t port = htons(NFS_PORT);
    unsigned long prognum = NFS_PROGRAM;
    unsigned long prognum_offset = NFS_PROGRAM - 100000;
    struct addrinfo hints = {0}, *addr;
    struct rpc_err clnt_err;
    int getaddr;
    unsigned long us;
    enum outputs format = human;
    char prefix[255] = "nfsping";
    targets_t *targets;
    targets_t *target;
    targets_t target_dummy;
    int ch;
    unsigned long count = 0;
    unsigned long reconnect = 0;
    /* command-line options */
    int dns = 0, loop = 0, ip = 0, quiet = 0, multiple = 0;
    /* default to NFS v3 */
    u_long version = 3;
    int first, index;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = INADDR_ANY
    };

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, int_handler);

    /* don't quit on (TCP) broken pipes */
    signal(SIGPIPE, SIG_IGN);

    hints.ai_family = AF_INET;
    /* default to UDP */
    hints.ai_socktype = SOCK_DGRAM;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "aAc:C:dDg:hi:lLmMnNo:p:P:qr:sS:t:TvV:")) != -1) {
        switch(ch) {
            /* NFS ACL protocol */
            case 'a':
                if (prognum == NFS_PROGRAM) {
                    prognum = NFS_ACL_PROGRAM;
                    /* this usually runs on 2049 alongside NFS so leave port as default */
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* show IP addresses */
            case 'A':
                ip = 1;
                break;
            /* number of pings per target, parseable summary */
            case 'C':
                if (format == human) {
                    format = fping;
                } else {
                    fatal("Can't specify both -C and -o!\n");
                }
                /* fall through to regular count */
            /* number of pings per target */
            case 'c':
                count = strtoul(optarg, NULL, 10);
                if (count == 0 || count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* do reverse dns lookups for IP addresses */
            case 'd':
                dns = 1;
                break;
            case 'D':
                if (format == human) {
                    format = unixtime;
                /* TODO this should probably work, maybe a format=fpingunix? */
                } else if (format == fping) {
                    fatal("Can't specify both -C and -D!\n");
                } else {
                    fatal("Can't specify both -D and -o!\n");
                }
                break;
            /* prefix to use for graphite metrics */
            case 'g':
                strncpy(prefix, optarg, sizeof(prefix));
                break;
            /* interval between targets */
            case 'i':
                ms2ts(&wait_time, strtoul(optarg, NULL, 10));
                break;
            /* loop forever */
            case 'l':
                loop = 1;
                break;
            /* check network lock manager protocol */
            case 'L':
                if (prognum == NFS_PROGRAM) {
                    prognum = NLM_PROG;
                    /* default to the portmapper */
                    port = 0;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* use multiple IP addresses if found */
            /* TODO in this case do we also want to default to showing IP addresses instead of names? */
            case 'm':
                multiple = 1;
                break;
            /* use the portmapper */
            case 'M':
                /* check if it's been changed from the default by the -P option */
                if (port == htons(NFS_PORT)) {
                    port = 0;
                } else {
                    fatal("Can't specify both port and portmapper!\n");
                }
                break;
            /* check mount protocol */
            case 'n':
                if (prognum == NFS_PROGRAM) {
                    prognum = MOUNTPROG;
                    /* if we're checking mount instead of nfs, default to using the portmapper */
                    port = 0;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* check portmap protocol */
            case 'N':
                if (prognum == NFS_PROGRAM) {
                    prognum = PMAPPROG;
                    port = htons(PMAPPORT); /* 111 */
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* output format */
            case 'o':
                if (format == fping) {
                    fatal("Can't specify both -C and -o!\n");
                } else if (format == unixtime) {
                    fatal("Can't specify both -D and -o!\n");
                }

                if (strcmp(optarg, "G") == 0) {
                    format = graphite;
                } else if (strcmp(optarg, "S") == 0) {
                    format = statsd;
                } else if (strcmp(optarg, "T") == 0) {
                    format = opentsdb;
                } else {
                    fatal("Unknown output format \"%s\"!\n", optarg);
                }
                break;
            /* time between pings to target */
            case 'p':
                ms2ts(&sleep_time, strtoul(optarg, NULL, 10));
                break;
            /* specify NFS port */
            case 'P':
                /* check for the portmapper option */
                if (port) {
                    port = htons(strtoul(optarg, NULL, 10));
                } else {
                    fatal("Can't specify both portmapper and port!\n");
                }
                break;
            /* quiet, only print summary */
            /* TODO error if output also specified? */
            case 'q':
                quiet = 1;
                break;
            case 'r':
                reconnect = strtoul(optarg, NULL, 10);
                if (reconnect == 0 || reconnect == ULONG_MAX) {
                    fatal("Invalid reconnect count!\n");
                }
                break;
            /* check NSM */
            case 's':
                if (prognum == NFS_PROGRAM) {
                    prognum = SM_PROG;
                    /* default to using the portmapper */
                    port = 0;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* timeout */
            case 't':
                ms2tv(&timeout, strtoul(optarg, NULL, 10));
                if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
                    fatal("Zero timeout!\n");
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* verbose */
            case 'v':
                verbose = 1;
                break;
            /* specify NFS version */
            case 'V':
                version = strtoul(optarg, NULL, 10);
                if (version == 0 || version == ULONG_MAX) {
                    fatal("Illegal version %lu\n", version);
                }
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* calculate this once */
    prognum_offset = prognum - 100000;

    /* check null_dispatch table for supported versions for all protocols */
    if (null_dispatch[prognum_offset][version].proc == 0) {
        fatal("Illegal version %lu\n", version);
    }

    /* output formatting doesn't make sense for the simple check */
    if (count == 0 && loop == 0 && format != human) {
        fatal("Can't specify output format without ping count!\n");
    }

    /* check that the reconnect argument makes sense */
    if (count && (reconnect >= count)) {
        fatal("Ping count must be higher than reconnect!\n");
    }

    /* mark the first non-option argument */
    first = optind;

    /* check if we don't have any targets */
    if (first == argc) {
        usage();
    }

    /* pointer to head of list */
    target = &target_dummy;
    targets = target;

    /* process the targets from the command line */
    for (index = optind; index < argc; index++) {
        target->next = make_target(argv[index], port);
        target = target->next;

        /* first try treating the hostname as an IP address */
        if (inet_pton(AF_INET, target->name, &((struct sockaddr_in *)target->client_sock)->sin_addr)) {
            /* if we have reverse lookups enabled */
            if (dns) {
                target->name = calloc(1, NI_MAXHOST);
                getaddr = getnameinfo((struct sockaddr *)target->client_sock, sizeof(struct sockaddr_in), target->name, NI_MAXHOST, NULL, 0, 0);
                if (getaddr > 0) { /* failure! */
                    fprintf(stderr, "%s: %s\n", target->name, gai_strerror(getaddr));
                    exit(2); /* ping and fping return 2 for name resolution failures */
                }
                target->ndqf = reverse_fqdn(target->name);
            } else {
                /* don't reverse an IP address */
                target->ndqf = target->name;
            }
        } else {
            /* if that fails, do a DNS lookup */
            /* we don't call freeaddrinfo because we keep a pointer to the sin_addr in the target */
            getaddr = getaddrinfo(target->name, "nfs", &hints, &addr);
            if (getaddr == 0) { /* success! */
                /* loop through possibly multiple DNS responses */
                while (addr) {
                    target->client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;

                    if (ip) {
                        target->name = calloc(1, INET_ADDRSTRLEN);
                        inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, target->name, INET_ADDRSTRLEN);
                    }
                    target->ndqf = reverse_fqdn(target->name);

                    /* multiple results */
                    if (addr->ai_next) {
                        if (multiple) {
                            /* create the next target */
                            target->next = make_target(argv[index], port);
                            target = target->next;
                        } else {
                            /* we have to look up the IP address if we haven't already for the warning */
                            if (!ip) {
                                target->name = calloc(1, INET_ADDRSTRLEN);
                                inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, target->name, INET_ADDRSTRLEN);
                            }
                            fprintf(stderr, "Multiple addresses found for %s, using %s\n", argv[index], target->name);
                            /* if we're not using the IP address again we can free it */
                            if (!ip) {
                                free(target->name);
                                target->name = argv[index];
                            }
                            target->ndqf = reverse_fqdn(target->name);
                            break;
                        }
                    }
                    addr = addr->ai_next;
                }
            } else {
                fprintf(stderr, "getaddrinfo error (%s): %s\n", target->name, gai_strerror(getaddr));
                exit(2); /* ping and fping return 2 for name resolution failures */
            }
        }
    }

    /* skip the first dummy entry */
    targets = targets->next;

    /* allocate space for printing out a summary of all ping times at the end */
    if (format == fping) {
        target = targets;
        while (target) {
            target->results = calloc(count, sizeof(unsigned long));
            if (target->results == NULL) {
                fprintf(stderr, "nfsping: couldn't allocate memory for results!\n");
                exit(3);
            }
            target = target->next;
        }
    }

    target = targets;

    /* the main loop */
    while (target) {
        /* reset */
        status == NULL;

        /* check if we were disconnected (TCP) or if this is the first iteration */
        if (target->client == NULL) {
            /* try and (re)connect */
            target->client = create_rpc_client(target->client_sock, &hints, prognum, null_dispatch[prognum_offset][version].version, timeout, src_ip);
        }

        /* now see if we're connected */
        if (target->client) {
            /* first time marker */
            gettimeofday(&call_start, NULL);

            /* the actual ping */
            /* use a dispatch table instead of switch */
            /* doublecheck that the procedure exists, should have been checked above */
            if (null_dispatch[prognum_offset][version].proc) {
                status = null_dispatch[prognum_offset][version].proc(NULL, target->client);
            } else {
                fatal("Illegal version: %lu\n", version);
            }

            /* second time marker */
            gettimeofday(&call_end, NULL);
            target->sent++;
        } /* else not connected */

        /* check for success */
        if (status) {
            target->received++;

            /* check if we're looping */
            if (count || loop) {
                us = tv2us(call_end) - tv2us(call_start);

                /* TODO discard first ping in case of ARP delay? Only for TCP for handshake? */
                if (us < target->min) target->min = us;
                if (us > target->max) target->max = us;
                /* calculate the average time */
                target->avg = (target->avg * (target->received - 1) + us) / target->received;

                if (format == fping)
                    target->results[target->sent - 1] = us;

                if (!quiet) {
                    /* TODO estimate the time by getting the midpoint of call_start and call_end? */
                    print_output(format, prefix, target, prognum_offset, version, call_end, us);
                }
            } else {
                printf("%s is alive\n", target->name);
            }
        /* something went wrong */
        } else {
            print_lost(format, prefix, target, prognum_offset, version, call_end);

            if (target->client) {
                fprintf(stderr, "%s : ", target->name);
                clnt_geterr(target->client, &clnt_err);
                clnt_perror(target->client, null_dispatch[prognum_offset][version].name);
                fprintf(stderr, "\n");
                fflush(stderr);

                /* check for broken pipes or reset connections and try and reconnect next time */
                if (clnt_err.re_errno == EPIPE || ECONNRESET) {
                    target->client = destroy_rpc_client(target->client);
                }
            } /* TODO else? */

            if (!count && !loop) {
                printf("%s is dead\n", target->name);
            }
        }

        /* see if we should disconnect and reconnect */
        if (reconnect && targets->sent % reconnect == 0) {
            target->client = destroy_rpc_client(target->client);
        }

        target = target->next;

        if (target) {
            nanosleep(&wait_time, NULL);
        /* at the end of the targets list, see if we need to loop */
        } else {
            /* check the first target */
            if ((count && targets->sent < count) || loop) {
                /* reset back to start of list */
                /* do this at the end of the loop not the start so we can check if we're done or need to sleep */
                /* see if we've been signalled */
                if (!quitting) {
                    target = targets;
                    /* sleep between rounds */
                    nanosleep(&sleep_time, NULL);
                }
            }
        }
    } /* while(target) */

    fflush(stdout);

    /* only print summary if looping */
    if (count || loop) {
        /* these print to stderr */
        if (!quiet && (format == human || format == fping || format == unixtime))
            fprintf(stderr, "\n");
        /* don't print summary for formatted output */
        if (format == fping)
            print_fping_summary(*targets);
        else if (format == human || format == unixtime)
            print_summary(*targets);
    }

    /* loop through the targets and find any that didn't get a response
     * exit with a failure if there were any missing responses */
    target = targets;
    while (target) {
        if (target->received < target->sent)
            exit(EXIT_FAILURE);
        else
            target = target->next;
    }
    /* otherwise exit successfully */
    exit(EXIT_SUCCESS);
}
