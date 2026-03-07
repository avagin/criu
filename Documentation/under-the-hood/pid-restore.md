# Pid restore

## ns_last_pid
In order to restore PID, CRIU uses /proc/sys/kernel/ns_last_pid, which is available in kernel since v3.3 (according to [Upstream_kernel_commits](upstream_kernel_commits.md)). It requires CONFIG_CHECKPOINT_RESTORE to be set and it is enabled in the vast majority of distros. ns_last_pid contains the last pid that was assigned by the kernel. So, when kernel needs to assign a new one, it looks into ns_last_pid, gets last_pid and assigns last_pid+1. To restore PID, criu locks ns_last_pid, writes PID-1 and calls clone().

## Example
Here is a simple program that shows how to set PID for a forked child.

-*BEWARE! This program requires root. I don't take any responsibility for what this code might do to your system.**( tested though =) )

```c

1.include <sys/stat.h>
1.include <fcntl.h>
1.include <stdio.h>
1.include <string.h>
1.include <stdlib.h>

int main(int argc, char *argv[])
{
    int fd, pid;
    char buf[32];

    if (argc != 2)
     return 1;

    printf("Opening ns_last_pid...\n");
    fd = open("/proc/sys/kernel/ns_last_pid", O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("Can't open ns_last_pid");
        return 1;
    }
    printf("Done\n");

    printf("Locking ns_last_pid...\n");
    if (flock(fd, LOCK_EX)) {
        close(fd);
        printf("Can't lock ns_last_pid\n");
        return 1;
    }
    printf("Done\n");

    pid = atoi(argv[1]);
    snprintf(buf, sizeof(buf), "%d", pid - 1);

    printf("Writing pid-1 to ns_last_pid...\n");
    if (write(fd, buf, strlen(buf)) != strlen(buf)) {
        printf("Can't write to buf\n");
        return 1;
    }
    printf("Done\n");

    printf("Forking...\n");
    int new_pid;
    new_pid = fork();
    if (new_pid == 0) {
        printf("I'm child!\n");
        exit(0);
    } else if (new_pid == pid) {
        printf("I'm parent. My child got right pid!\n");
    } else {
        printf("pid does not match expected one\n");
    }
    printf("Done\n");

    printf("Cleaning up...");
    if (flock(fd, LOCK_UN)) {
        printf("Can't unlock");
    }

    close(fd);

    printf("Done\n");

    return 0;
}

```

