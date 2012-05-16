#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE 1024*1024

/* convenience string macros */
#define ERRSTR strerror(errno)

#define ARG_CMD      argv[0]
#define ARG_PORT     argv[1]

#define ARG_IP       argv[2]
#define ARG_NUMFLOWS argv[3]
#define ARG_PKTSIZE  argv[4]
#define ARG_MEAN     argv[5]
#define ARG_SHAPE    argv[6]

#define CLIENT_ARGS  (5+1)
#define CLIENT_EXTRA (1)
#define SERVER_ARGS  (1+1)

#define EXIT() exit(__LINE__)

#define IS_PARETO_CLIENT() (argc == CLIENT_ARGS + CLIENT_EXTRA)
#define IS_CONST_CLIENT()  (argc == CLIENT_ARGS)
#define IS_SERVER() (argc == SERVER_ARGS)
#define IS_CLIENT() (argc != SERVER_ARGS)

#define CLIENT_CLOSE(j) do { \
    close(fd[j]); \
    fd[j] = fd[--numfds]; \
    d.c.total[j] = d.c.total[numfds]; \
    d.c.remain[j] = d.c.remain[numfds]; \
    d.c.starttimes[j] = d.c.starttimes[numfds]; \
} while (0)

/* we need 5 extra FDs: /dev/zero, pipe read, pipe write, stdout, stderr */
#define EXTRA_FDS 2
#define FD_STDOUT    (devzero+0)
#define FD_STDERR    (devzero+1)

/* mersenne twister */
#define N 624
static unsigned long x[N];
static unsigned long *p0, *p1, *pm;
static void initrand();
static float randfloat();


/* traffic server, client, and timestamper */
static volatile int done = 0;
void signal_handler(int signal) {
    done = 1;
}

int main(int argc, const char **argv) {
    /* stack variables */
    /* fds and select() */
    int fd[FD_SETSIZE], highfd, numfds, devzero;
    fd_set fdrset, fdwset;
    FILE *fout, *ferr;
    int numevts;
    sigset_t signals;
    /* server/client */
    union serverclientdata {
        struct server {
            unsigned int received;
            int printevent;
            time_t last_bw_print;
            struct timespec start;
        } s;
        struct client {
            struct addrinfo hints, *res;
            struct timespec starttimes[FD_SETSIZE];
            unsigned int remain[FD_SETSIZE];
            unsigned int total[FD_SETSIZE];
            int numflows, tcpdatasz;
            float negInvShape, scale;
        } c;
    } d;
    /* temporaries */
    char buf[BUF_SIZE];
    struct timespec evt;
    int i, count;
    socklen_t len;

    /* calculate /dev/zero, pipe, stdout, and stderr file numbers */
    devzero = sysconf(_SC_OPEN_MAX);
    if (FD_SETSIZE + EXTRA_FDS >= devzero) {
        /* no room on top of FD_SETSIZE for our extra FDs (we can select the
         * same number of FDs we can use). */
        devzero -= EXTRA_FDS;
    } else {
        /* there's room on top of FD_SETSIZE for the FDs, so use it */
        devzero = FD_SETSIZE;
    }

    /* usage */
    if (!IS_PARETO_CLIENT() && !IS_CONST_CLIENT() && !IS_SERVER()) {
        fprintf(stderr,
                "server usage: %s port\n"
                "client usage: %s "
                   "port ip/hostname num_flows pkt_size mean_pkts [shape]\n",
                ARG_CMD, ARG_CMD);
        fprintf(stdout, "max flows per instance: %d\n", devzero);
        EXIT();
    }

    /* move stdout and stderr to high FDs to make room for connections */
    if (!(fout = fdopen(dup2(fileno(stdout), FD_STDOUT), "w"))) {
        fprintf(stderr, "error moving stdout\n");
        EXIT();
    }
    if (!(ferr = fdopen(dup2(fileno(stderr), FD_STDERR), "w"))) {
        fprintf(stderr, "error moving stderr\n");
        EXIT();
    }
    /* close the old FDs, along with stdin */
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    /* signal handler for graceful SIGINT and SIGTERM handling */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    /* ignore SIGPIPE (sudden disconnects) during select calls */
    sigprocmask(0, NULL, &signals);
    sigaddset(&signals, SIGPIPE);

    /* pre-run preparations */
    if (IS_SERVER()) {
        struct sockaddr_in sin;
        /* create socket */
        numfds = 1;
        fd[0] = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        if (fd[0] == -1) {
            fprintf(ferr, "error opening socket: %s\n", ERRSTR);
            EXIT();
        }
        if (fd[0] != 0) {
            fprintf(ferr, "assert failed: fd[0] == %d != 0\n", fd[0]);
            EXIT();
        }
        /* avoid TIME_WAIT issues by allowing rebinding of sockets */
        i = 1;
        if (setsockopt(fd[0], SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i))) {
            fprintf(ferr, "error setting TCP to reuse addresses: %s\n", ERRSTR);
            EXIT();
        }
        /* bind the socket to the port */
        sin.sin_port = htons(atoi(ARG_PORT));
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_family = AF_INET;
        if (bind(fd[0], (struct sockaddr*)&sin, sizeof(sin))) {
            fprintf(ferr, "unable to bind to port %s: %s\n", ARG_PORT, ERRSTR);
            EXIT();
        }
        /* start listening */
        if (listen(fd[0], devzero)) {
            fprintf(ferr, "unable to listen on port %s: %s\n", ARG_PORT, ERRSTR);
            EXIT();
        }
        d.s.received = 0;
        d.s.last_bw_print = 0;
        d.s.printevent = 0;
        /* get time 0 */
        if (clock_gettime(CLOCK_MONOTONIC, &d.s.start)) {
            fprintf(ferr, "error: CLOCK_MONOTONIC not supported\n");
            EXIT();
        }
    } else {
        /* prepare flow size generator */
        initrand();
        i = atoi(ARG_MEAN);
        if (IS_PARETO_CLIENT()) {
            d.c.negInvShape = d.c.scale = atof(ARG_SHAPE);
            d.c.scale = (d.c.scale - 1) / d.c.scale * i;
            d.c.negInvShape = -1.0f / d.c.negInvShape;
        } else {
            d.c.scale = i;
        }
        d.c.tcpdatasz = atoi(ARG_PKTSIZE) - 40;
        /* resolve hostname/ip+port into IPv4 sock_addr */
        memset(&d.c.hints, 0, sizeof(d.c.hints));
        d.c.hints.ai_family = AF_INET;
        d.c.hints.ai_socktype = SOCK_STREAM;
        d.c.hints.ai_protocol = IPPROTO_TCP;
        d.c.hints.ai_flags = AI_NUMERICSERV;
        /* skip resolving for numeric IP addresses */
        if (ARG_IP[0] >= '0' && ARG_IP[0] <= '9')
            d.c.hints.ai_flags |= AI_NUMERICHOST;
        if ((i = getaddrinfo(ARG_IP, ARG_PORT, &d.c.hints, &d.c.res))) {
            fprintf(ferr, "error resolving %s:%s: %s, %s\n",
                    ARG_IP, ARG_PORT, gai_strerror(i), ERRSTR);
            EXIT();
        }
        /* create flows in main loop */
        d.c.numflows = atoi(ARG_NUMFLOWS);
        if (d.c.numflows > devzero) {
            fprintf(ferr, "too many flows requested: %s > %d\n",
                    ARG_NUMFLOWS, devzero);
            EXIT();
        }
        numfds = 0;
    }

    /* semi-infinite main loop */
    while (!done) {

        if (IS_CLIENT()) {
            /* start flows as necessary */
            for (; numfds < d.c.numflows; ++numfds) {
                /* create socket */
                fd[numfds] = socket(AF_INET,
                                    SOCK_STREAM | SOCK_NONBLOCK,
                                    IPPROTO_TCP);
                if (fd[numfds] == -1) {
                    fprintf(ferr, "error opening socket: %s\n", ERRSTR);
                    EXIT();
                }
                /* set socket options */
                if (setsockopt(fd[numfds], SOL_TCP, TCP_MAXSEG,
                               &d.c.tcpdatasz, sizeof(d.c.tcpdatasz))) {
                    fprintf(ferr,
                            "error setting TCP maximum segment size: %s\n",
                            ERRSTR);
                    EXIT();
                }
                i = 1;
                if (setsockopt(fd[numfds], SOL_TCP, TCP_NODELAY,
                               &i, sizeof(i))) {
                    fprintf(ferr, "error disabling TCP nagle: %s\n", ERRSTR);
                    EXIT();
                }
                /* calculate how much data to send */
                if (IS_PARETO_CLIENT()) {
                    d.c.total[numfds] = d.c.scale
                        * powf(1.0f-randfloat(), d.c.negInvShape) + 0.5f;
                    if (!d.c.total[numfds]) d.c.total[numfds] = 1;
                } else {
                    d.c.total[numfds] = d.c.scale;
                }
                d.c.total[numfds] *= d.c.tcpdatasz;
                d.c.remain[numfds] = d.c.total[numfds];
                /* start the timer */
                clock_gettime(CLOCK_MONOTONIC, &d.c.starttimes[numfds]);
                /* connect */
                if (connect(fd[numfds], d.c.res->ai_addr, d.c.res->ai_addrlen)
                        && errno != EINPROGRESS) {
                    fprintf(ferr, "error starting connection: %s\n", ERRSTR);
                    EXIT();
                }
            }
            if (done) break;

            FD_ZERO(&fdwset);
        }

        /* fill in the FD set for selection */
        FD_ZERO(&fdrset);
        highfd = 0;
        if (IS_SERVER()) {
            FD_SET(fd[0], &fdrset);
            for (i = 1; i < numfds; ++i) {
                FD_SET(fd[i], &fdrset);
                if (fd[i] > highfd) highfd = fd[i];
            }
            /* wait for an event on one of the FDs. Ignore SIGPIPE. */
            numevts = pselect(highfd+1, &fdrset, NULL, NULL, NULL, &signals);
        } else {
            for (i = 0; i < numfds; ++i) {
                FD_SET(fd[i], d.c.remain[i] ? &fdwset : &fdrset);
                if (fd[i] > highfd) highfd = fd[i];
            }
            /* wait for an event on one of the FDs. Ignore SIGPIPE. */
            numevts = pselect(highfd+1, &fdrset, &fdwset, NULL, NULL, &signals);
        }
        if (done) break;
        if (numevts == -1) {
            if (errno == EINTR) continue;
            fprintf(ferr, "error selecting: %s\n", ERRSTR);
            EXIT();
        } else if (!numevts) {
            continue;
        }

        /* server/client handling */
        if (IS_SERVER()) {
            /* check for new connection request, but don't handle it yet */
            if (FD_ISSET(fd[0], &fdrset))
                --numevts;
            /* handle data events (not fd[0]) */
            if (numevts) {
                for (i = numfds-1; numevts && i >= 1; --i) {
                    if (!FD_ISSET(fd[i], &fdrset)) continue;
                    /* read and discard */
                    count = recv(fd[i], buf, BUF_SIZE,
                                 MSG_DONTWAIT | MSG_NOSIGNAL);
                    if (count == 0 || (count == -1 && errno == EPIPE)) {
                        /* EOF */
                        close(fd[i]);
                        fd[i] = fd[--numfds];
                        d.s.printevent = 1;
                    } else if (count > 0) {
                        d.s.received += count;
                    }
                }
            }
            /* handle new connection requests */
            if (FD_ISSET(fd[0], &fdrset)) {
                highfd = accept4(fd[0], NULL, NULL, SOCK_NONBLOCK);
                if (highfd == -1) {
                    fprintf(ferr, "error accepting connection: %s\n", ERRSTR);
                } else if (numfds == devzero || highfd >= devzero) {
                    fprintf(ferr, "too many connections\n");
                    close(highfd);
                } else if (highfd != -1) {
                    fd[numfds++] = highfd;
                    d.s.printevent = 1;
                }
            }
            /* print out event info */
            if (d.s.printevent) {
                d.s.printevent = 0;
                /* get the event time */
                clock_gettime(CLOCK_MONOTONIC, &evt);
                /* subtract the time */
                evt.tv_sec -= d.s.start.tv_sec;
                if (evt.tv_nsec < d.s.start.tv_nsec) {
                    evt.tv_sec -= 1;
                    evt.tv_nsec -= d.s.start.tv_nsec - 1000000000;
                } else {
                    evt.tv_nsec -= d.s.start.tv_nsec;
                }
                /* print out event */
                if (evt.tv_sec > d.s.last_bw_print) {
                    /* print out received bytes and reset it */
                    d.s.last_bw_print = evt.tv_sec;
                    fprintf(fout, "%ld.%09ld s: %u connections, %u bytes\n",
                            evt.tv_sec, evt.tv_nsec, numfds - 1, d.s.received);
                    d.s.received = 0;
                } else {
                    fprintf(fout, "%ld.%09ld s: %u connections\n",
                            evt.tv_sec, evt.tv_nsec, numfds - 1);
                }
            }
        } else {
            /* client */
            for (i = numfds-1; numevts && i >= 0; --i) {
                if (FD_ISSET(fd[i], &fdwset)) {
                    --numevts;
                    if (d.c.remain[i] == d.c.total[i]) {
                        /* connect() has completed. check for success */
                        len = sizeof(count);
                        if (getsockopt(fd[i], SOL_SOCKET, SO_ERROR,
                                       &count, &len) || count) {
                            /* connection failed. try again later. */
                            fprintf(ferr, "error connecting: %s\n",
                                    strerror(count));
                            CLIENT_CLOSE(i);
                            continue;
                        }
                    }
                    /* write junk */
                    count = d.c.remain[i];
                    if (count > BUF_SIZE) count = BUF_SIZE;
                    count = send(fd[i], buf, count, MSG_DONTWAIT | MSG_NOSIGNAL);
                    if (count == -1) {
                        if (errno == EAGAIN || errno == EPIPE) continue;
                        fprintf(ferr, "error writing to socket: %s\n", ERRSTR);
                        CLIENT_CLOSE(i);
                        continue;
                    }
                    d.c.remain[i] -= count;
                    if (!d.c.remain[i]) {
                        /* finish the stream */
                        if (shutdown(fd[i], SHUT_WR)) {
                            fprintf(ferr, "error shutting down socket: %s\n",
                                    ERRSTR);
                            CLIENT_CLOSE(i);
                            continue;
                        }
                    }
                } else if (FD_ISSET(fd[i], &fdrset)) {
                    --numevts;
                    /* EOF */
                    /* end the timer */
                    clock_gettime(CLOCK_MONOTONIC, &evt);
                    /* subtract the time */
                    evt.tv_sec -= d.c.starttimes[i].tv_sec;
                    if (evt.tv_nsec < d.c.starttimes[i].tv_nsec) {
                        evt.tv_sec -= 1;
                        evt.tv_nsec -= d.c.starttimes[i].tv_nsec - 1000000000;
                    } else {
                        evt.tv_nsec -= d.c.starttimes[i].tv_nsec;
                    }
                    /* print stats */
                    fprintf(fout, "%u packets: %ld.%09ld seconds\n",
                            d.c.total[i] / d.c.tcpdatasz,
                            evt.tv_sec, evt.tv_nsec);
                    CLIENT_CLOSE(i);
                }
            }
        }
    }
    /* free resources */
    if (IS_CLIENT())
        freeaddrinfo(d.c.res);
    return 0;
}


/* for original implementation of the mersenne twister, see
 * http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/VERSIONS/C-LANG/mt19937ar-nrl.c */
static void initrand() {
    int i, j, k, key[3];
    struct timespec time;

    /* quickly-varying seed */
    clock_gettime(CLOCK_MONOTONIC, &time);
    key[0] = time.tv_sec;
    key[1] = time.tv_nsec;
    key[2] = getpid();

    /* basic seed */
    x[0] = 19650218UL;
    for (i = 1; i < N; ++i)
        x[i] = (1812433253UL * (x[i - 1] ^ (x[i - 1] >> 30)) + i)
               & 0xffffffffUL;
    p0 = x;
    p1 = x + 1;
    pm = x + 397;

    /* use extra key data to seed further */
    i = 1;
    j = 0;
    for (k = N; k; --k) {
        /* non linear */
        x[i] = ((x[i] ^ ((x[i - 1] ^ (x[i - 1] >> 30)) * 1664525UL))
                + key[j] + j) & 0xffffffffUL;
        if (++i >= N) {
            x[0] = x[N - 1];
            i = 1;
        }
        if (++j >= 3)
            j = 0;
    }
    for (k = N - 1; k; --k) {
        /* non linear */
        x[i] = ((x[i] ^ ((x[i - 1] ^ (x[i - 1] >> 30)) * 1566083941UL)) - i)
               & 0xffffffffUL;
        if (++i >= N) {
            x[0] = x[N - 1];
            i = 1;
        }
    }
    x[0] = 0x80000000UL;  /* MSB is 1; assuring non-zero initial array */
}

static float randfloat() {
    unsigned long y;

    /* Twisted feedback */
    y = *p0 = *pm++ ^ (((*p0 & 0x80000000UL) | (*p1 & 0x7fffffffUL)) >> 1)
              ^ (-(*p1 & 1) & 0x9908b0dfUL);
    p0 = p1++;
    if (pm == x + N) {
        pm = x;
    }
    if (p1 == x + N) {
        p1 = x;
    }
    /* Temper */
    y ^= y >> 11;
    y ^= y << 7 & 0x9d2c5680UL;
    y ^= y << 15 & 0xefc60000UL;
    y ^= y >> 18;
    return y * (1.0 / 4294967296.0);
}
