/*  pwn - simple multiplayer chess game
 *
 *  Copyright (C) 2020 Jona Ackerschott
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <sys/socket.h>

#include "handler.h"

int hread(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = read(fd, b, size);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return 2;
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
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}

int hsend(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = send(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hrecv(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = recv(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return 2;
			return -1;
		} else if (n == 0) {
			return 1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
