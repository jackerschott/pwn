#include <unistd.h>
#include <sys/socket.h>

#include "handler.h"

int readbuf(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n;
		do {
			n = read(fd, b, size);
		} while ((n == -1) && (errno == EINTR));
		if (n == -1)
			return -1;
		b += n;
		size -= n;
	}
	return 0;
}

int writebuf(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n;
		do {
			n = write(fd, b, size);
		} while ((n == -1) && (errno == EINTR));
		if (n == -1)
			return -1;
		b += n;
		size -= n;
	}
	return 0;
}

int sendbuf(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n;
		do {
			n = send(fd, b, size, 0);
		} while ((n == -1) && (errno == EINTR));
		if (n == -1)
			return -1;
		b += n;
		size -= n;
	}
	return 0;
}

int recvbuf(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n;
		do {
			n = recv(fd, b, size, 0);
		} while ((n == -1) && (errno == EINTR));
		if (n == -1)
			return -1;
		b += n;
		size -= n;
	}
	return 0;
}
