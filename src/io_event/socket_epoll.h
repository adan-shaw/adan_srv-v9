#ifndef poll_socket_epoll_h
#define poll_socket_epoll_h

#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

//#define sp_invalid(efd) (efd == -1 ? true : false)
//static bool sp_invalid (int efd) { return efd == -1; }

//#define sp_init() (-1)
//static int sp_init () { return -1; }

static int sp_create ()
{
	signal (SIGPIPE, SIG_IGN);
	return epoll_create (EPOLL_SFD_MAX);
}

static int sp_release (int efd)
{
	close (efd);
	return -1;
}

static int sp_add (int efd, int sock, void *ud)
{
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = ud;
	if (epoll_ctl (efd, EPOLL_CTL_ADD, sock, &ev) == -1)
	{
		return 1;
	}
	return 0;
}

static void sp_del (int efd, int sock)
{
	epoll_ctl (efd, EPOLL_CTL_DEL, sock, NULL);
}

static void sp_write (int efd, int sock, void *ud, bool enable)
{
	struct epoll_event ev;
	ev.events = EPOLLIN | (enable ? EPOLLOUT : 0);
	ev.data.ptr = ud;
	epoll_ctl (efd, EPOLL_CTL_MOD, sock, &ev);
}

static int sp_wait (int efd, struct event *e, int max, int timeout)
{
	struct epoll_event ev[max];
	unsigned flag;
	int i, n = epoll_wait (efd, ev, max, timeout);
	for (i = 0; i < n; i++)
	{
		e[i].s = ev[i].data.ptr;
		flag = ev[i].events;
		e[i].write = (flag & EPOLLOUT) != 0;
		e[i].read = (flag & EPOLLIN) != 0;
	}

	return n;
}

static void sp_nonblocking (int fd)
{
	int flag = fcntl (fd, F_GETFL, 0);
	if (-1 == flag)
	{
		return;
	}

	fcntl (fd, F_SETFL, flag | O_NONBLOCK);
}

#endif
