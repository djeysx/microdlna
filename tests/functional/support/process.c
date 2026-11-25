#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "process.h"

#define MAX_TRACKED 64
#define MAX_ARGV 32

static pid_t tracked[MAX_TRACKED];
static int tracked_count = 0;
static int atexit_registered = 0;

static void kill_tracked(void)
{
    for (int i = 0; i < tracked_count; i++)
        if (tracked[i] > 0)
            kill(tracked[i], SIGKILL);
}

static void track(pid_t pid)
{
    if (!atexit_registered)
    {
        atexit(kill_tracked);
        atexit_registered = 1;
    }
    if (tracked_count < MAX_TRACKED)
        tracked[tracked_count++] = pid;
}

static void untrack(pid_t pid)
{
    for (int i = 0; i < tracked_count; i++)
        if (tracked[i] == pid)
            tracked[i] = 0;
}

static const char *binary_path(void)
{
    const char *bin = getenv("MICRODLNAD_BIN");
    return (bin && *bin) ? bin : "build/microdlnad";
}

static pid_t spawn(const char *bin, const char *media_dir, int port,
                    const char *const *extra_args)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    const char *argv[MAX_ARGV];
    int n = 0;
    argv[n++] = bin;
    argv[n++] = "-g";
    argv[n++] = "-p";
    argv[n++] = port_str;
    argv[n++] = "-D";
    argv[n++] = media_dir;
    if (extra_args)
    {
        for (int i = 0; extra_args[i] != NULL; i++)
        {
            if (n >= MAX_ARGV - 1)
                abort();
            argv[n++] = extra_args[i];
        }
    }
    argv[n] = NULL;

    pid_t pid = fork();
    if (pid < 0)
        abort();

    if (pid == 0)
    {
        execv(bin, (char *const *)argv);
        _exit(127);
    }

    track(pid);
    return pid;
}

static int connect_probe(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);

    int rc = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    close(s);
    return rc;
}

static int deadline_passed(const struct timespec *deadline)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > deadline->tv_sec
        || (now.tv_sec == deadline->tv_sec && now.tv_nsec > deadline->tv_nsec);
}

static void nap(void)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 20 * 1000 * 1000 }; /* 20ms */
    nanosleep(&ts, NULL);
}

void mdlna_start(struct mdlna_proc *p, const char *media_dir, int port,
                  const char *const *extra_args)
{
    const char *bin = binary_path();
    pid_t pid = spawn(bin, media_dir, port, extra_args);

    p->pid = pid;
    p->port = port;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 5;

    for (;;)
    {
        if (connect_probe(port) == 0)
            return;

        int status;
        if (waitpid(pid, &status, WNOHANG) == pid)
        {
            fprintf(stderr,
                    "mdlna_start: %s exited before accepting connections on port %d "
                    "(wait status %d)\n", bin, port, status);
            untrack(pid);
            abort();
        }

        if (deadline_passed(&deadline))
        {
            fprintf(stderr, "mdlna_start: timed out waiting for %s to listen on port %d\n",
                    bin, port);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            untrack(pid);
            abort();
        }

        nap();
    }
}

int mdlna_expect_fast_exit(const char *media_dir, int port,
                            const char *const *extra_args)
{
    const char *bin = binary_path();
    pid_t pid = spawn(bin, media_dir, port, extra_args);

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 5;

    for (;;)
    {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
            untrack(pid);
            return status;
        }

        if (connect_probe(port) == 0)
        {
            fprintf(stderr,
                    "mdlna_expect_fast_exit: %s unexpectedly started listening on port %d\n",
                    bin, port);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            untrack(pid);
            abort();
        }

        if (deadline_passed(&deadline))
        {
            fprintf(stderr,
                    "mdlna_expect_fast_exit: %s neither exited nor listened on port %d\n",
                    bin, port);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            untrack(pid);
            abort();
        }

        nap();
    }
}

int mdlna_stop(struct mdlna_proc *p)
{
    if (p->pid <= 0)
        return 0;

    kill(p->pid, SIGTERM);

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;

    int status = -1;
    int exited_on_its_own = 0;

    for (;;)
    {
        pid_t r = waitpid(p->pid, &status, WNOHANG);
        if (r == p->pid)
        {
            exited_on_its_own = 1;
            break;
        }

        if (deadline_passed(&deadline))
        {
            kill(p->pid, SIGKILL);
            waitpid(p->pid, &status, 0);
            break;
        }

        nap();
    }

    untrack(p->pid);
    p->pid = -1;

    return exited_on_its_own && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
