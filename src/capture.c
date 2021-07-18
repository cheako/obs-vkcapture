/*
OBS Linux Vulkan/OpenGL game capture
Copyright (C) 2021 David Rosca <nowrep@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "capture.h"
#include "utils.h"

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/un.h>
#include <sys/socket.h>

struct {
    int connfd;
    bool accepted;
    bool capturing;
} data;

static bool capture_try_connect()
{
    const char *sockname = "/tmp/obs-vkcapture.sock";

    if (access(sockname, R_OK) != 0) {
        return false;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sockname);
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    int ret = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        close(sock);
        return false;
    }

    os_socket_block(sock, false);
    data.connfd = sock;
    return true;
}

void capture_init()
{
    data.connfd = -1;
    data.accepted = false;
    data.capturing = false;
}

void capture_update_socket()
{
    static int limiter = 0;
    if (++limiter < 60) {
        return;
    }
    limiter = 0;

    if (data.connfd < 0 && !capture_try_connect()) {
        return;
    }

    char buf[1];
    ssize_t n = recv(data.connfd, buf, 1, 0);
    if (n == 1 && buf[0] == '1') {
        data.accepted = true;
    }
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno != ECONNRESET) {
            hlog("Socket recv error %s", strerror(errno));
        }
    }
    if (n <= 0) {
        close(data.connfd);
        data.connfd = -1;
    }
}

void capture_init_shtex(
        int width, int height, int format, int stride,
        int offset, uint64_t modifier, uint32_t winid,
        bool flip, int fd)
{
    struct capture_texture_data td;
    td.width = width;
    td.height = height;
    td.format = format;
    td.stride = stride;
    td.offset = offset;
    td.modifier = modifier;
    td.winid = winid;
    td.flip = flip;

    struct msghdr msg = {0};

    struct iovec io = {
        .iov_base = &td,
        .iov_len = CAPTURE_TEXTURE_DATA_SIZE,
    };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    const ssize_t sent = sendmsg(data.connfd, &msg, 0);
    if (sent < 0) {
        hlog("Socket sendmsg error %s", strerror(errno));
    }

    data.capturing = true;
}

void capture_stop()
{
    data.accepted = false;
    data.capturing = false;

    if (data.connfd >= 0) {
        close(data.connfd);
        data.connfd = -1;
    }
}

bool capture_should_stop()
{
    return data.capturing && data.connfd < 0;
}

bool capture_should_init()
{
    return !data.capturing && data.connfd >= 0 && data.accepted;
}

bool capture_ready()
{
    return data.capturing;
}
