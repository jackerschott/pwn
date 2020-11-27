#include <unistd.h>
#include <sys/socket.h>

#include "handler.h"

int hread(int fd, void *buf, size_t size)
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
int hwrite(int fd, void *buf, size_t size)
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
