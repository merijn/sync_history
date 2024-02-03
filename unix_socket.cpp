#include <unistd.h>

#include "limits.hpp"
#include "commands.hpp"
#include "unix_socket.hpp"

const size_t max_size = sizeof (Request) + max_command_size;

static char messageBuffer[max_size];

static void
closeFd(int fd)
{
    if (close(fd) == -1) {
        //FIXME: log error
    }
}

sockaddr_un
UnixSocket::addr_from_path(const std::string& path)
{
    size_t ret;
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    ret = strlcpy(addr.sun_path, path.c_str(), sizeof addr.sun_path);
    if (ret >= sizeof addr.sun_path) {
        throw FatalError("Socket path to long");
    }
#ifdef __APPLE__
    addr.sun_len = static_cast<unsigned char>(ret);
#endif
    return addr;
}

UnixSocket::UnixSocket(const sockaddr_un& addr)
  : initialised(true), received(false), origin(addr)
{
    int ret;
    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1) throw ErrnoFatal("socket");

    ret = bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    if (ret == -1) {
        closeFd(sock);
        throw ErrnoFatal("bind", addr.sun_path);
    }
}

UnixSocket::UnixSocket(const std::string& path)
  : UnixSocket(addr_from_path(path))
{}

UnixSocket::UnixSocket(UnixSocket&& s)
  : initialised(s.initialised), received(s.received), sock(s.sock)
  , origin(s.origin), from(s.from)
{ s.initialised = false; }

UnixSocket::~UnixSocket()
{ close(true); }

void
UnixSocket::close(bool do_unlink)
{
    int ret;
    if (initialised) {
        closeFd(sock);
        if (do_unlink) {
            ret = unlink(origin.sun_path);
            if (ret == -1) {
                //FIXME: log error
            }
        }
        initialised = false;
    }
}

ssize_t
UnixSocket::recv(void **resultPtr)
{
    ssize_t ret;

    struct iovec buffers[] = { { messageBuffer, max_size } };
    struct msghdr header =
        { .msg_name = &from
        , .msg_namelen = sizeof from
        , .msg_iov = buffers
        , .msg_iovlen = 1
        , .msg_control = nullptr
        , .msg_controllen = 0
        , .msg_flags = 0
        };

    ret = recvmsg(sock, &header, 0);
    if (ret == -1) throw ErrnoFatal("recvmsg");

    received = true;

    *resultPtr = reinterpret_cast<void*>(&messageBuffer);

    return ret;
}

void
UnixSocket::send(void *msg_header, size_t header_length, void *data, size_t data_length, const sockaddr_un& addr)
{
    ssize_t ret;
    struct iovec buffers[] = {
        { msg_header, header_length },
        { data, data_length }
    };

    struct msghdr header =
        { .msg_name = const_cast<sockaddr_un*>(&addr)
        , .msg_namelen = sizeof addr
        , .msg_iov = buffers
        , .msg_iovlen = 2
        , .msg_control = nullptr
        , .msg_controllen = 0
        , .msg_flags = 0
        };

    ret = sendmsg(sock, &header, 0);
    if (ret == -1) {
        throw ErrnoFatal("sendmsg");
    } else if (static_cast<size_t>(ret) != header_length + data_length) {
        throw FatalError("sendmsg(): Sent incorrect length!");
    }
    received = true;
}
