// Inspired by https://github.com/iffyio/isolate/tree/master

#include "network.h"
#include <stdio.h>
#include <string.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <vlog.h>

static int addattr_l(
        struct nlmsghdr *n, int maxlen, __u16 type,
        const void *data, __u16 datalen)
{
    __u16 attr_len = RTA_LENGTH(datalen);

    __u32 newlen = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(attr_len);
    if (newlen > maxlen) {
        VLOG_ERROR("containerv", "network: cannot add attribute. size (%d) exceeded maxlen (%d)\n",
            newlen, maxlen);
        return -1;
    }

    struct rtattr *rta;
    rta = NLMSG_TAIL(n);
    rta->rta_type = type;
    rta->rta_len = attr_len;
    if (datalen)
        memcpy(RTA_DATA(rta), data, datalen);

    n->nlmsg_len = newlen;
    return 0;
}

// Note: This function was changed from returning struct rtattr* to using an output
// parameter to maintain consistent error handling throughout the network module.
// All functions now return int (0 for success, -1 for error) rather than using
// die() or returning NULL pointers.
static int addattr_nest(
        struct nlmsghdr *n, int maxlen, __u16 type, struct rtattr **nest_out)
{
    struct rtattr *nest = NLMSG_TAIL(n);

    if (addattr_l(n, maxlen, type, NULL, 0) != 0) {
        return -1;
    }
    *nest_out = nest;
    return 0;
}

static void addattr_nest_end(struct nlmsghdr *n, struct rtattr *nest)
{
    nest->rta_len = (void *)NLMSG_TAIL(n) - (void *)nest;
}

static int read_response(
        int fd, struct msghdr *msg, char **response)
{
    struct iovec *iov = msg->msg_iov;
    iov->iov_base = *response;
    iov->iov_len = MAX_PAYLOAD;

    ssize_t resp_len = recvmsg(fd, msg, 0);

    if (resp_len == 0) {
        VLOG_ERROR("containerv", "network: EOF on netlink\n");
        return -1;
    }

    if (resp_len < 0) {
        VLOG_ERROR("containerv", "network: netlink receive error: %s\n", strerror(errno));
        return -1;
    }

    return resp_len;
}

static int check_response(int sock_fd)
{
    struct iovec iov;
    struct msghdr msg = {
            .msg_name = NULL,
            .msg_namelen = 0,
            .msg_iov = &iov,
            .msg_iovlen = 1
    };
    char *resp = malloc(MAX_PAYLOAD);
    if (!resp) {
        VLOG_ERROR("containerv", "network: failed to allocate response buffer\n");
        return -1;
    }

    ssize_t resp_len = read_response(sock_fd, &msg, &resp);
    if (resp_len < 0) {
        free(resp);
        return -1;
    }

    struct nlmsghdr *hdr = (struct nlmsghdr *) resp;
    int nlmsglen = hdr->nlmsg_len;
    int datalen = nlmsglen - sizeof(*hdr);

    // Did we read all data?
    if (datalen < 0 || nlmsglen > resp_len) {
        if (msg.msg_flags & MSG_TRUNC) {
            VLOG_ERROR("containerv", "network: received truncated message\n");
            free(resp);
            return -1;
        }

        VLOG_ERROR("containerv", "network: malformed message: nlmsg_len=%d\n", nlmsglen);
        free(resp);
        return -1;
    }

    // Was there an error?
    if (hdr->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *) NLMSG_DATA(hdr);

        if (datalen < sizeof(struct nlmsgerr))
            VLOG_ERROR("containerv", "network: ERROR truncated!\n");

        if(err->error) {
            errno = -err->error;
            VLOG_ERROR("containerv", "network: RTNETLINK: %s\n", strerror(errno));
            free(resp);
            return -1;
        }
    }

    free(resp);
    return 0;
}

int create_socket(int domain, int type, int protocol)
{
    int sock_fd = socket(domain, type, protocol);
    if (sock_fd < 0) {
        VLOG_ERROR("containerv", "network: cannot open socket: %s\n", strerror(errno));
        return -1;
    }

    return sock_fd;
}

static int send_nlmsg(int sock_fd, struct nlmsghdr *n)
{
    struct iovec iov = {
            .iov_base = n,
            .iov_len = n->nlmsg_len
    };

    struct msghdr msg = {
            .msg_name = NULL,
            .msg_namelen = 0,
            .msg_iov = &iov,
            .msg_iovlen = 1
    };

    n->nlmsg_seq++;

    ssize_t status = sendmsg(sock_fd, &msg, 0);
    if (status < 0) {
        VLOG_ERROR("containerv", "network: cannot talk to rtnetlink: %s\n", strerror(errno));
        return -1;
    }

    return check_response(sock_fd);
}

int get_netns_fd(int pid)
{
    char path[256];
    sprintf(path, "/proc/%d/ns/net", pid);

    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        VLOG_ERROR("containerv", "network: cannot read netns file %s: %s\n", path, strerror(errno));
        return -1;
    }

    return fd;
}

int if_up(
        char *ifname, char *ip, char *netmask)
{
    int sock_fd = create_socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_fd < 0) {
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ifname, strlen(ifname));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(struct sockaddr_in));
    saddr.sin_family = AF_INET;
    saddr.sin_port = 0;

    char *p = (char *) &saddr;

    saddr.sin_addr.s_addr = inet_addr(ip);
    memcpy(((char *)&(ifr.ifr_addr)), p, sizeof(struct sockaddr));
    if (ioctl(sock_fd, SIOCSIFADDR, &ifr)) {
        VLOG_ERROR("containerv", "network: cannot set ip addr %s, %s: %s\n", ifname, ip, strerror(errno));
        close(sock_fd);
        return -1;
    }

    saddr.sin_addr.s_addr = inet_addr(netmask);
    memcpy(((char *)&(ifr.ifr_addr)), p, sizeof(struct sockaddr));
    if (ioctl(sock_fd, SIOCSIFNETMASK, &ifr)) {
        VLOG_ERROR("containerv", "network: cannot set netmask for addr %s, %s: %s\n", ifname, netmask, strerror(errno));
        close(sock_fd);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP | IFF_BROADCAST |
                     IFF_RUNNING | IFF_MULTICAST;
    if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr)) {
        VLOG_ERROR("containerv", "network: cannot set flags for addr %s, %s: %s\n", ifname, ip, strerror(errno));
        close(sock_fd);
        return -1;
    }

    close(sock_fd);
    return 0;
}

int create_veth(int sock_fd, char *ifname, char *peername)
{
    // ip link add veth0 type veth peer name veth1
    __u16 flags =
                  NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    struct nl_req req = {
            .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
            .n.nlmsg_flags = flags,
            .n.nlmsg_type = RTM_NEWLINK,
            .i.ifi_family = PF_NETLINK,
    };
    struct nlmsghdr *n = &req.n;
    int maxlen = sizeof(req);
    struct rtattr *linfo = NULL;
    struct rtattr *linfodata = NULL;
    struct rtattr *peerinfo = NULL;

    if (addattr_l(n, maxlen, IFLA_IFNAME, ifname, strlen(ifname) + 1) != 0) {
        return -1;
    }

    if (addattr_nest(n, maxlen, IFLA_LINKINFO, &linfo) != 0) {
        return -1;
    }
    if (addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, "veth", 5) != 0) {
        return -1;
    }

    if (addattr_nest(n, maxlen, IFLA_INFO_DATA, &linfodata) != 0) {
        return -1;
    }

    if (addattr_nest(n, maxlen, VETH_INFO_PEER, &peerinfo) != 0) {
        return -1;
    }
    n->nlmsg_len += sizeof(struct ifinfomsg);
    if (addattr_l(n, maxlen, IFLA_IFNAME, peername, strlen(peername) + 1) != 0) {
        return -1;
    }
    addattr_nest_end(n, peerinfo);

    addattr_nest_end(n, linfodata);
    addattr_nest_end(n, linfo);

    return send_nlmsg(sock_fd, n);
}

int move_if_to_pid_netns(int sock_fd, char *ifname, int netns)
{
    // ip link set veth1 netns coke
    struct nl_req req = {
            .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
            .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
            .n.nlmsg_type = RTM_NEWLINK,
            .i.ifi_family = PF_NETLINK,
    };

    if (addattr_l(&req.n, sizeof(req), IFLA_NET_NS_FD, &netns, 4) != 0) {
        return -1;
    }
    if (addattr_l(&req.n, sizeof(req), IFLA_IFNAME,
              ifname, strlen(ifname) + 1) != 0) {
        return -1;
    }
    return send_nlmsg(sock_fd, &req.n);
}
