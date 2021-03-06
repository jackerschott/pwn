#ifndef UTIL_H
#define UTIL_H

int hread(int fd, void *buf, size_t size);
int hwrite(int fd, void *buf, size_t size);
int hrecv(int fd, char *buf, size_t size);
int hsend(int fd, char *buf);

#endif /* UTIL_H */
