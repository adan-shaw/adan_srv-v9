#ifndef poll_socket_kqueue_h
#define poll_socket_kqueue_h

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool 
sp_invalid(int kfd) {
	return kfd == -1;
}

static int
sp_init() {
	return -1;
}

static int
sp_create() {
	signal(SIGPIPE, SIG_IGN);
	return kqueue();
}

static int
sp_release(int kfd) {
	close(kfd);
	return -1;
}

static int 
sp_add(int kfd, int sock, void *ud) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ, EV_ADD, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
		return 1;
	}
	return 0;
}

static void 
sp_del(int kfd, int sock) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ | EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
}

static void 
sp_write(int kfd, int sock, void *ud, bool enable) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_WRITE, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
}

static int 
sp_wait(int kfd, struct event *e, int max, int timeout) {
	struct timespec timeoutspec;
	timeoutspec.tv_sec = timeout / 1000;
	timeoutspec.tv_nsec = (timeout % 1000) * 1000000;
	struct kevent ev[max];
	int n = kevent(kfd, NULL, 0, ev, max, &timeoutspec);

	int i;
	for (i=0;i<n;i++) {
		e[i].s = ev[i].udata;
		unsigned flag = ev[i].filter;
		e[i].write = (flag & EVFILT_WRITE) != 0;
		e[i].read = (flag & EVFILT_READ) != 0;
	}

	return n;
}

static void
sp_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if ( -1 == flag ) {
		return;
	}

	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#endif
