#include "nfsping.h"

volatile sig_atomic_t quitting;

struct timeval start, end;
float min, max, avg;
unsigned int sent = 0;
unsigned int received = 0;
double loss;
char *target;

/* convert a timeval to microseconds */
unsigned long tv2us(struct timeval tv) {
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

/* convert a timeval to milliseconds */
unsigned long tv2ms(struct timeval tv) {
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* convert milliseconds to a timeval */
void ms2tv(struct timeval *tv, unsigned long ms) {
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
}

/* convert milliseconds to a timespec */
void ms2ts(struct timespec *ts, unsigned long ms) {
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000000) * 1000000;
}

void int_handler(int sig) {
    quitting = 1;
}

void print_summary() {
    printf("%s : xmt/rcv/%%loss = %u/%u/%.0f%%, min/avg/max = %.2f/%.2f/%.2f\n",
        target, sent, received, loss, min / 1000.0, avg / 1000.0, max / 1000.0);
}

void print_verbose_summary(results_t results) {
    results_t *current = &results;
    printf("%s :", target);
    while (current) {
        if (current->us)
            printf(" %.2f", current->us / 1000.0);
        else
            printf(" -");
        current = current->next;
    }
    printf("\n");
}

int main(int argc, char **argv) {
    CLIENT *client;
    enum clnt_stat status;
    char *error;
    /* default 2.5 seconds */
    struct timeval timeout = { 2, 500000 };
    struct timeval call_start, call_end;
    /* default 1 second */
    struct timespec sleep_time = { 1, 0 };
    struct sockaddr_in *client_sock;
    int sock = RPC_ANYSOCK;
    struct addrinfo hints, *addr;
    int getaddr;
    unsigned long us;
    results_t *results;
    results_t *current;
    int ch;
    unsigned long count = 0;
    int verbose, loop, ip;

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, int_handler);

    while ((ch = getopt(argc, argv, "AC:c:lp:t:")) != -1) {
        switch(ch) {
            case 'A':
                ip = 1;
                break;
            case 'C':
                verbose = 1;
                results = calloc(1, sizeof(results_t));
                current = results;
                /* fall through to regular count */
            case 'c':
                count = strtoul(optarg, NULL, 10);
                break;
            case 'l':
                loop = 1;
                break;
            case 'p':
                ms2ts(&sleep_time, strtoul(optarg, NULL, 10));
                break;
            case 't':
                /* TODO check for zero */
                ms2tv(&timeout, strtoul(optarg, NULL, 10));
                break;
        }
    }

    argc -= optind;
    argv += optind;

    target = *argv;

    addr = calloc(1, sizeof(struct addrinfo));
    addr->ai_addr = calloc(1, sizeof(struct sockaddr_in));
    client_sock = (struct sockaddr_in *) addr->ai_addr;
    client_sock->sin_family = AF_INET;
    client_sock->sin_port = htons(NFS_PORT);

    /* first try treating the hostname as an IP address */
    if (!inet_pton(AF_INET, target, &client_sock->sin_addr)) {
        /* if that fails, do a DNS lookup */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM; /* change to SOCK_STREAM for TCP */
        getaddr = getaddrinfo(target, "nfs", &hints, &addr);
        if (getaddr == 0) {
            client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            if (ip) {
                target = calloc(1, INET_ADDRSTRLEN);
                inet_ntop(AF_INET, &client_sock->sin_addr, target, INET_ADDRSTRLEN);
            }
        } else {
            printf("%s: %s\n", target, gai_strerror(getaddr));
            exit(EXIT_FAILURE);
        }
    }

    client = clntudp_create(client_sock, NFS_PROGRAM, 3, timeout, &sock);

    if (client) {
        client->cl_auth = authnone_create();

        gettimeofday(&start, NULL);

        while(1) {
            if (quitting) {
                break;
            }
            gettimeofday(&call_start, NULL);
            status = clnt_call(client, NFSPROC_NULL, (xdrproc_t) xdr_void, NULL, (xdrproc_t) xdr_void, error, timeout);
            gettimeofday(&call_end, NULL);
            sent++;

            if (status == RPC_SUCCESS) {
                /* check if we're not looping */
                if (!count && !loop) {
                    printf("%s is alive\n", target);
                    exit(EXIT_SUCCESS);
                }
                received++;
                loss = (sent - received) / (double)sent * 100;

                us = tv2us(call_end) - tv2us(call_start);

                /* first result is a special case */
                if (received == 1) {
                    min = max = avg = us;
                } else {
                    if (verbose) {
                        current->next = calloc(1, sizeof(results_t));
                        current = current->next;
                    }
                    if (us < min) min = us;
                    if (us > max) max = us;
                    /* calculate the average time */
                    avg = (avg * (received - 1) + us) / received;
                }

                if (verbose)
                    current->us = us;

                printf("%s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n", target, sent - 1, us / 1000.0, avg / 1000.0, loss);
            } else {
                clnt_perror(client, target);
                if (!count && !loop) {
                    printf("%s is dead\n", target);
                    exit(EXIT_FAILURE);
                }
                if (verbose && sent > 1) {
                    current->next = calloc(1, sizeof(results_t));
                    current = current->next;
                }
            }
            if (count && sent >= count) {
                break;
            }
            nanosleep(&sleep_time, NULL);
        }
        gettimeofday(&end, NULL);
        printf("\n");
        if (verbose)
            print_verbose_summary(*results);
        else
            print_summary();
        exit(EXIT_SUCCESS);
    } else {
        clnt_pcreateerror(argv[0]);
        exit(EXIT_FAILURE);
    }
}
