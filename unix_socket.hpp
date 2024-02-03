#ifndef UNIX_SOCKET_HPP
#define UNIX_SOCKET_HPP

#include <sys/socket.h>
#include <sys/un.h>

#include <string>

#include "util.hpp"

class UnixSocket {
    bool initialised;
    bool received;
    int sock;
    struct sockaddr_un origin;
    struct sockaddr_un from;

    static sockaddr_un addr_from_path(const std::string& path);

  public:
    UnixSocket(const sockaddr_un& addr);
    UnixSocket(const std::string& path);
    UnixSocket(UnixSocket&& s);
    ~UnixSocket();

    void close(bool do_unlink = false);

    template<typename T>
    T* recv()
    {
        T* buf = nullptr;
        ssize_t size = recv(reinterpret_cast<void**>(&buf));
        if (static_cast<size_t>(size) != buf->length + sizeof *buf) {
            throw FatalError("recvmsg(): Incorrect message length");
        }

        return buf;
    }

    ssize_t recv(void **resultPtr);

    template<typename T>
    void send(T& msg, void *data)
    {
        if (received) send(msg, data, from);
        else throw FatalError("Can't reply without receiving first!");
    }

    template<typename T>
    void send(T& msg, void *data, const std::string& path)
    { send(msg, data, addr_from_path(path)); }

    template<typename T>
    void send(T& msg, void *data, const sockaddr_un& addr)
    { send(&msg, sizeof msg, data, msg.length, addr); }

    void send(void *msg_header, size_t header_length, void *data, size_t data_length, const sockaddr_un& addr);
};
#endif
