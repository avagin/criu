# PID Restoration

## ns_last_pid
To restore PIDs, CRIU uses `/proc/sys/kernel/ns_last_pid`, which has been available in the kernel since version 3.3. This feature requires `CONFIG_CHECKPOINT_RESTORE` to be enabled, which is the case for the vast majority of Linux distributions. The `ns_last_pid` file contains the last PID assigned by the kernel. When the kernel needs to assign a new PID, it retrieves the value from `ns_last_pid` and assigns `ns_last_pid + 1`. To restore a specific PID, CRIU locks `ns_last_pid`, writes `PID - 1` to it, and then calls `clone()`.

## Example
The following C program demonstrates how to set a specific PID for a forked child process.

**BEWARE**: This program requires root privileges. The authors take no responsibility for the impact of this code on your system (though it has been tested).

```c
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd, pid;
    char buf[32];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <desired_pid>\n", argv[0]);
        return 1;
    }

    printf("Opening ns_last_pid...\n");
    fd = open("/proc/sys/kernel/ns_last_pid", O_RDWR);
    if (fd < 0) {
        perror("Can't open ns_last_pid");
        return 1;
    }
    printf("Done\n");

    printf("Locking ns_last_pid...\n");
    if (flock(fd, LOCK_EX)) {
        close(fd);
        perror("Can't lock ns_last_pid");
        return 1;
    }
    printf("Done\n");

    pid = atoi(argv[1]);
    snprintf(buf, sizeof(buf), "%d", pid - 1);

    printf("Writing pid-1 (%d) to ns_last_pid...\n", pid - 1);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
        perror("Can't write to ns_last_pid");
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }
    printf("Done\n");

    printf("Forking...\n");
    int new_pid = fork();
    if (new_pid == 0) {
        printf("I'm the child! My PID is %d\n", getpid());
        exit(0);
    } else if (new_pid == pid) {
        printf("I'm the parent. My child received the correct PID (%d)!\n", new_pid);
    } else {
        printf("PID %d does not match expected PID %d\n", new_pid, pid);
    }
    printf("Done\n");

    printf("Cleaning up...\n");
    if (flock(fd, LOCK_UN)) {
        perror("Can't unlock");
    }

    close(fd);
    printf("Done\n");

    return 0;
}
```
