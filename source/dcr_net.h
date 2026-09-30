/* dcr_net.h -- real sockets, where a port has them (PvZ's pvz_net.c).
 *
 * This port plays offline: bionic_net.c's sockets never connect, and no
 * descriptor is a real socket. bionic_io.c asks here all the same, so the
 * file stays the one the other ports use. MIT. */
#ifndef DCR_NET_H
#define DCR_NET_H

static inline int dcr_net_owns(int fd) { (void)fd; return 0; }
static inline int dcr_net_close(int fd) { (void)fd; return -1; }
static inline int dcr_net_fcntl(int fd, int cmd, long arg) { (void)fd; (void)cmd; (void)arg; return -1; }
static inline int dcr_net_ioctl(int fd, unsigned long req, void *arg) { (void)fd; (void)req; (void)arg; return -1; }
static inline short dcr_net_ready(int fd, short events) { (void)fd; (void)events; return 0; }

#endif
