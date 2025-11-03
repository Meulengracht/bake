// Inspired by https://github.com/iffyio/isolate/tree/master

#ifndef ISOLATE_NETNS_H
#define ISOLATE_NETNS_H

#include <stdio.h>
#include <string.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#define MAX_PAYLOAD 1024

struct nl_req {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[MAX_PAYLOAD];
};

#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

/**
 * @brief Create a socket with the specified parameters
 * @return socket file descriptor on success, -1 on failure
 */
int create_socket(int domain, int type, int protocol);

/**
 * @brief Bring up a network interface and configure its IP address
 * @return 0 on success, -1 on failure
 */
int if_up(char *ifname, char *ip, char *netmask);

/**
 * @brief Create a virtual ethernet (veth) pair
 * @return 0 on success, -1 on failure
 */
int create_veth(int sock_fd, char *ifname, char *peername);

/**
 * @brief Move a network interface to a different network namespace
 * @return 0 on success, -1 on failure
 */
int move_if_to_pid_netns(int sock_fd, char *ifname, int netns);

/**
 * @brief Get the file descriptor for a process's network namespace
 * @return file descriptor on success, -1 on failure
 */
int get_netns_fd(int pid);

#endif //ISOLATE_NETNS_H
