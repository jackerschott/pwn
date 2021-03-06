#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

void print_timestamp()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long t = ts.tv_sec * 1000L * 1000L * 1000L + ts.tv_nsec;
	printf("t = %li\n", t);
}

int hread(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = read(fd, b, size);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			assert(errno != EWOULDBLOCK && errno != EAGAIN);
			return -1;
		} else if (n == 0) {
			return 1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hwrite(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = write(fd, b, size);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			assert(errno != EPIPE && errno != EWOULDBLOCK && errno != EAGAIN);
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hrecv(int fd, char *buf, size_t size)
{
	size_t sz = size;
	char *b = buf;
	while (sz > 0) {
		size_t n = recv(fd, b, sz, 0);
		print_timestamp();
		printf("n = %lu\n", n);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return -2;
			return -1;
		} else if (n == 0) {
			return 1;
		}

		if (b[n - 1] == '\n') {
			b[n - 1] = '\0';
			printf("[received] %s\n", b);
			printf("\n");
			return 0;
		}

		b += n;
		sz -= n;

		//if (sz == 0) {
		//	size_t newsz = sz + size;
		//	void *newbuf = realloc(buf, newsz);
		//	if (!newbuf)
		//		return -1;
		//	buf = newbuf;
		//	b = buf + sz;
		//}
	}
	buf[70] = '\0';
	printf("[received] %s\n", buf);
	printf("\n");
	return -3;
}
int hsend(int fd, char *buf)
{
	size_t size = strlen(buf) + 1;
	buf[size - 1] = '\n';

	char *b = buf;
	while (size > 0) {
		size_t n = send(fd, b, size, 0);
		print_timestamp();
		printf("n = %lu\n", n);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			assert(errno != EPIPE && errno != EWOULDBLOCK && errno != EAGAIN);
			return -1;
		}

		b += n;
		size -= n;
	}

	char *c = strchr(buf, '\n');
	*c = '\0';
	printf("[send] %s\n", buf);
	printf("\n");
	return 0;
}
