#ifndef socket_poll_h
#define socket_poll_h

#include <stdbool.h>
#include <unistd.h>

//非常不推荐用windows 版本, 因为会使用select, 性能低下
#if defined(_WIN32) || defined(_WIN64)
#define USE_SELECT (1)
#else

#define closesocket(fd) (close(fd))
//static void closesocket (int fd){ close (fd); }

#endif

#if USE_SELECT
struct select_pool;
typedef struct select_pool *poll_fd;
#else
typedef int poll_fd;
#endif

struct event
{
	void *s;
	bool read;
	bool write;
};

//static bool sp_invalid (poll_fd fd);
//static poll_fd sp_init ();
static poll_fd sp_create ();
static poll_fd sp_release (poll_fd fd);
static int sp_add (poll_fd fd, int sock, void *ud);
static void sp_del (poll_fd fd, int sock);
static void sp_write (poll_fd, int sock, void *ud, bool enable);
static int sp_wait (poll_fd, struct event *e, int max, int timeout);
static void sp_nonblocking (int sock);

#ifdef __linux__
#define sp_invalid(efd) (efd == -1 ? true : false)
#define sp_init() (-1)
#define EPOLL_SFD_MAX (4096)
#include "socket_epoll.h"
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define sp_invalid(kfd) (kfd == -1 ? true : false)
#define sp_init() (-1)
#include "socket_kqueue.h"
#endif

#if defined(_WIN32) || defined(_WIN64)
#define sp_invalid(sp) (sp == NULL ? true : false)
#define sp_init() (NULL)
#include "socket_select.h"
#endif

#endif
