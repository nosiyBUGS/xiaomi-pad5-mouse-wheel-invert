/*
 * uu_wheel_flip - grab mouse evdev devices, invert REL_WHEEL/REL_HWHEEL,
 * and re-emit through uinput.
 *
 * Build:
 *   zig cc -target aarch64-linux-musl -static -O2 -o uu_wheel_flip uu_wheel_flip.c
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include <fnmatch.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/input.h>
#include <linux/uinput.h>

#ifndef UINPUT_MAX_NAME_SIZE
#define UINPUT_MAX_NAME_SIZE 80
#endif

#ifndef UI_DEV_SETUP
struct uinput_setup {
    struct input_id id;
    char name[UINPUT_MAX_NAME_SIZE];
    __u32 ff_effects_max;
};
#define UI_DEV_SETUP _IOW(UINPUT_IOCTL_BASE, 3, struct uinput_setup)
#endif

#define MAX_DEV 8
#define LOCK_PATH "/data/local/tmp/uu_wheel_flip.lock"
#define CONTROL_PATH "/data/local/tmp/uu_wheel_flip.invert"
#define TARGETS_PATH "/data/local/tmp/uu_wheel_flip.targets"
#define SCAN_INTERVAL 2

struct dev {
    int fd;
    int ufd;
    char path[64];
    char name[256];
    struct input_id id;
};

static volatile sig_atomic_t running = 1;
static int g_argc;
static char **g_argv;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static int invert_enabled(void) {
    int fd = open(CONTROL_PATH, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    char buf[8];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    return (buf[0] == '1');
}

static int test_bit(const unsigned long *bits, int nr) {
    return (int)((bits[nr / (8 * sizeof(unsigned long))] >>
                  (nr % (8 * sizeof(unsigned long)))) & 1UL);
}

static int match_target(const char *pattern, const char *name) {
    if (pattern[0] == '\0') {
        return 0;
    }
    if (strpbrk(pattern, "*?[") != NULL) {
        return fnmatch(pattern, name, 0) == 0;
    }
    return strstr(name, pattern) != NULL;
}

static int builtin_target(const char *name) {
    return strstr(name, "BT5.0 Mouse") != NULL ||
           strstr(name, "Xiaomi Mouse") != NULL;
}

static int is_target(const char *name) {
    if (strstr(name, "UU Wheel Flip") != NULL) {
        return 0;
    }

    /* Optional CLI arguments override the config file. */
    if (g_argc > 0) {
        for (int i = 0; i < g_argc; i++) {
            if (match_target(g_argv[i], name)) {
                return 1;
            }
        }
        return 0;
    }

    FILE *fp = fopen(TARGETS_PATH, "r");
    if (fp == NULL) {
        return builtin_target(name);
    }

    char line[256];
    int count = 0;
    int matched = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        char *end = p + strlen(p);
        while (end > p && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        if (*p == '\0' || *p == '#') {
            continue;
        }
        count++;
        if (match_target(p, name)) {
            matched = 1;
            break;
        }
    }
    fclose(fp);

    if (count == 0) {
        return builtin_target(name);
    }
    return matched;
}

static int open_uinput(struct dev *d) {
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        logmsg("open /dev/uinput: %s", strerror(errno));
        return -1;
    }

    unsigned long evbits[(EV_MAX / (8 * sizeof(unsigned long))) + 1];
    unsigned long keybits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1];
    unsigned long relbits[(REL_MAX / (8 * sizeof(unsigned long))) + 1];
    unsigned long absbits[(ABS_MAX / (8 * sizeof(unsigned long))) + 1];
    unsigned long mscbits[(MSC_MAX / (8 * sizeof(unsigned long))) + 1];
    unsigned long propbits[(INPUT_PROP_MAX / (8 * sizeof(unsigned long))) + 1];

    memset(evbits, 0, sizeof(evbits));
    memset(keybits, 0, sizeof(keybits));
    memset(relbits, 0, sizeof(relbits));
    memset(absbits, 0, sizeof(absbits));
    memset(mscbits, 0, sizeof(mscbits));
    memset(propbits, 0, sizeof(propbits));

    ioctl(d->fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    ioctl(d->fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    ioctl(d->fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits);
    ioctl(d->fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
    ioctl(d->fd, EVIOCGBIT(EV_MSC, sizeof(mscbits)), mscbits);
    ioctl(d->fd, EVIOCGPROP(sizeof(propbits)), propbits);

    for (int i = 0; i <= EV_MAX; i++) {
        if (test_bit(evbits, i)) {
            ioctl(ufd, UI_SET_EVBIT, i);
        }
    }
    for (int i = 0; i <= KEY_MAX; i++) {
        if (test_bit(keybits, i)) {
            ioctl(ufd, UI_SET_KEYBIT, i);
        }
    }
    for (int i = 0; i <= REL_MAX; i++) {
        if (test_bit(relbits, i)) {
            ioctl(ufd, UI_SET_RELBIT, i);
        }
    }
    for (int i = 0; i <= ABS_MAX; i++) {
        if (test_bit(absbits, i)) {
            ioctl(ufd, UI_SET_ABSBIT, i);
        }
    }
    for (int i = 0; i <= MSC_MAX; i++) {
        if (test_bit(mscbits, i)) {
            ioctl(ufd, UI_SET_MSCBIT, i);
        }
    }
    for (int i = 0; i <= INPUT_PROP_MAX; i++) {
        if (test_bit(propbits, i)) {
            ioctl(ufd, UI_SET_PROPBIT, i);
        }
    }

    /* Make sure Android treats the virtual device as a pointer/cursor. */
    ioctl(ufd, UI_SET_PROPBIT, INPUT_PROP_POINTER);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id = d->id;
    snprintf(usetup.name, sizeof(usetup.name), "UU Wheel Flip %s", d->name);

    if (ioctl(ufd, UI_DEV_SETUP, &usetup) < 0) {
        logmsg("UI_DEV_SETUP: %s", strerror(errno));
        close(ufd);
        return -1;
    }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        logmsg("UI_DEV_CREATE: %s", strerror(errno));
        close(ufd);
        return -1;
    }
    return ufd;
}

static int attach_dev(struct dev *d, const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    char name[256];
    memset(name, 0, sizeof(name));
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        close(fd);
        return -1;
    }
    if (!is_target(name)) {
        close(fd);
        return -1;
    }

    struct input_id id;
    memset(&id, 0, sizeof(id));
    ioctl(fd, EVIOCGID, &id);

    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        logmsg("EVIOCGRAB %s (%s): %s", path, name, strerror(errno));
        close(fd);
        return -1;
    }

    int reuse = (d->ufd >= 0 && strcmp(d->name, name) == 0);
    d->fd = fd;
    d->id = id;
    snprintf(d->path, sizeof(d->path), "%s", path);
    snprintf(d->name, sizeof(d->name), "%s", name);

    /* Keep the virtual device alive across physical mouse reconnects.
     * UU Remote binds to the virtual device; destroying/recreating it
     * leaves the app on a stale input device and wheel direction flips. */
    if (!reuse) {
        d->ufd = open_uinput(d);
        if (d->ufd < 0) {
            ioctl(fd, EVIOCGRAB, 0);
            close(fd);
            d->fd = -1;
            return -1;
        }
    }

    logmsg("attached %s (%s)%s", path, name, reuse ? " [virtual kept]" : "");
    return 0;
}

static void disconnect_dev(struct dev *d) {
    if (d->fd >= 0) {
        ioctl(d->fd, EVIOCGRAB, 0);
        close(d->fd);
        d->fd = -1;
    }
    if (d->path[0] != '\0') {
        logmsg("disconnected %s (%s), virtual kept", d->path, d->name);
    }
}

static void destroy_dev(struct dev *d) {
    disconnect_dev(d);
    if (d->ufd >= 0) {
        ioctl(d->ufd, UI_DEV_DESTROY);
        close(d->ufd);
        d->ufd = -1;
    }
}

static void process_dev(struct dev *d) {
    struct input_event ev[64];
    ssize_t n = read(d->fd, ev, sizeof(ev));
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return;
        }
        logmsg("read %s: %s", d->path, strerror(errno));
        disconnect_dev(d);
        return;
    }
    if (n == 0) {
        disconnect_dev(d);
        return;
    }

    int count = (int)(n / (ssize_t)sizeof(struct input_event));
    for (int i = 0; i < count; i++) {
        if (ev[i].type == EV_REL &&
            (ev[i].code == REL_WHEEL || ev[i].code == REL_HWHEEL)) {
            int orig = ev[i].value;
            int inv = invert_enabled();
            if (inv) {
                ev[i].value = -ev[i].value;
            }
            logmsg("wheel %s code=%d %d -> %d (invert=%d)", d->name, ev[i].code, orig, ev[i].value, inv);
        }
        ssize_t w = write(d->ufd, &ev[i], sizeof(ev[i]));
        if (w != (ssize_t)sizeof(ev[i])) {
            logmsg("write uinput %s: %s", d->path, strerror(errno));
            destroy_dev(d);
            return;
        }
    }
}

static int read_dev_name(const char *path, char *name, size_t size) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    memset(name, 0, size);
    int ret = ioctl(fd, EVIOCGNAME(size - 1), name);
    close(fd);
    return ret < 0 ? -1 : 0;
}

static void scan_devs(struct dev *devs, int *ndev) {
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        return;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0) {
            continue;
        }

        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

        int already = 0;
        for (int i = 0; i < *ndev; i++) {
            if (devs[i].fd >= 0 && strcmp(devs[i].path, path) == 0) {
                already = 1;
                break;
            }
        }
        if (already) {
            continue;
        }

        char name[256];
        if (read_dev_name(path, name, sizeof(name)) < 0 || !is_target(name)) {
            continue;
        }

        int idx = -1;
        /* A physically disconnected target keeps its virtual device.
         * Reuse that slot by name so the virtual device stays stable. */
        for (int i = 0; i < *ndev; i++) {
            if (devs[i].fd < 0 && strcmp(devs[i].name, name) == 0) {
                idx = i;
                break;
            }
        }

        int is_new = 0;
        if (idx < 0) {
            for (int i = 0; i < *ndev; i++) {
                if (devs[i].fd < 0 && devs[i].ufd < 0) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0) {
            if (*ndev >= MAX_DEV) {
                continue;
            }
            idx = (*ndev)++;
            is_new = 1;
            memset(&devs[idx], 0, sizeof(devs[idx]));
            devs[idx].fd = -1;
            devs[idx].ufd = -1;
        }

        if (attach_dev(&devs[idx], path) < 0 && is_new) {
            memset(&devs[idx], 0, sizeof(devs[idx]));
            devs[idx].fd = -1;
            devs[idx].ufd = -1;
            (*ndev)--;
        }
    }
    closedir(dir);
}

int main(int argc, char **argv) {
    g_argc = argc > 1 ? argc - 1 : 0;
    g_argv = argc > 1 ? argv + 1 : NULL;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    int lock_fd = open(LOCK_PATH, O_CREAT | O_RDWR, 0600);
    if (lock_fd >= 0 && flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        logmsg("another instance is running");
        close(lock_fd);
        return 0;
    }

    logmsg("uu_wheel_flip start");

    struct dev devs[MAX_DEV];
    memset(devs, 0, sizeof(devs));
    for (int i = 0; i < MAX_DEV; i++) {
        devs[i].fd = -1;
        devs[i].ufd = -1;
    }

    int ndev = 0;
    scan_devs(devs, &ndev);
    time_t last_scan = time(NULL);

    while (running) {
        struct pollfd pfds[MAX_DEV];
        int map[MAX_DEV];
        int n = 0;

        for (int i = 0; i < ndev; i++) {
            if (devs[i].fd >= 0) {
                pfds[n].fd = devs[i].fd;
                pfds[n].events = POLLIN;
                pfds[n].revents = 0;
                map[n] = i;
                n++;
            }
        }

        int ret = poll(pfds, (nfds_t)n, 1000);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            logmsg("poll: %s", strerror(errno));
            break;
        }

        if (ret > 0) {
            for (int k = 0; k < n; k++) {
                if (pfds[k].revents & (POLLIN | POLLERR | POLLHUP)) {
                    process_dev(&devs[map[k]]);
                }
            }
        }

        int w = 0;
        for (int i = 0; i < ndev; i++) {
            if (devs[i].fd >= 0 || devs[i].ufd >= 0) {
                if (w != i) {
                    devs[w] = devs[i];
                }
                w++;
            }
        }
        ndev = w;

        time_t now = time(NULL);
        if (now - last_scan >= SCAN_INTERVAL) {
            scan_devs(devs, &ndev);
            last_scan = now;
        }
    }

    for (int i = 0; i < ndev; i++) {
        destroy_dev(&devs[i]);
    }
    logmsg("uu_wheel_flip stop");

    if (lock_fd >= 0) {
        close(lock_fd);
    }
    return 0;
}
