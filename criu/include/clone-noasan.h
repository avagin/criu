#ifndef __CR_CLONE_NOASAN_H__
#define __CR_CLONE_NOASAN_H__

int clone_noasan(int (*fn)(void *), int flags, void *arg, pid_t pid);

#endif /* __CR_CLONE_NOASAN_H__ */
