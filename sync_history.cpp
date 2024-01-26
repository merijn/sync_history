#include <pwd.h>
#include <signal.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <fstream>
#include <iostream>
#include <unordered_map>

#include "limits.hpp"
#include "commands.hpp"
#include "util.hpp"
#include "history_cache.hpp"

const size_t max_size = sizeof (Request) + max_command_size;

static char messageBuffer[max_size];

static void
closeFd(int fd)
{
    if (close(fd) == -1) {
        //FIXME: log error
    }
}

static void
setProcName(int argc, char *argv[], const char *name)
{
    char *argvEnd = strrchr(argv[argc-1], '\0');
    size_t argvSize = static_cast<size_t>(argvEnd - argv[0]);
    size_t len = strlcpy(argv[0], name, argvSize);
    if (len >= argvSize) {
        throw FatalError("Name too long!");
    }
    memset(&argv[0][len], '\0', argvSize - len);
}


class UnixSocket {
    bool initialised;
    bool received;
    int sock;
    struct sockaddr_un origin;
    struct sockaddr_un from;

    static sockaddr_un
    addr_from_path(const std::string& path)
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

  public:
    UnixSocket(const sockaddr_un& addr)
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

    UnixSocket(const std::string& path)
     : UnixSocket(addr_from_path(path))
    {}

    UnixSocket(UnixSocket&& s)
     : initialised(s.initialised), received(s.received), sock(s.sock)
     , origin(s.origin), from(s.from)
    { s.initialised = false; }

    ~UnixSocket()
    { close(true); }

    void close(bool do_unlink = false)
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

    template<typename T>
    void recv(T* buf)
    {
        ssize_t ret;

        struct iovec buffers[] = { { buf, max_size } };
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
        if (ret == -1) {
            throw ErrnoFatal("recvmsg");
        } else if (static_cast<size_t>(ret) != buf->length + sizeof *buf) {
            throw FatalError("recvmsg(): Incorrect message length");
        }
        received = true;
    }

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
    void send(T& msg, void *data, sockaddr_un&& addr)
    { send(msg, data, addr); }

    template<typename T>
    void send(T& msg, void *data, sockaddr_un& addr)
    {
        ssize_t ret;
        struct iovec buffers[] = {
            { &msg, sizeof msg },
            { data, msg.length }
        };

        struct msghdr header =
            { .msg_name = &addr
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
        } else if (static_cast<size_t>(ret) != msg.length + sizeof msg) {
            throw FatalError("sendmsg(): Sent incorrect length!");
        }
        received = true;
    }
};

using namespace std;

static bool shutdownServer = false;

// Signals to replace with graceful shutdown
static int signals[] =
{ SIGHUP
, SIGINT
, SIGQUIT
, SIGPIPE
, SIGALRM
, SIGTERM
, SIGXCPU
, SIGXFSZ
, SIGVTALRM
, SIGPROF
, SIGUSR1
, SIGUSR2
};

static void
handleSignal(int)
{ shutdownServer = true; }

static void __attribute__((noreturn))
server(UnixSocket sock, ofstream&& history)
{
    int result;
    unordered_map<pid_t,HistoryCache> caches;

    Reply rep;
    Request *req = reinterpret_cast<Request*>(messageBuffer);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    result = daemon(0, 0);
#pragma clang diagnostic pop
    if (result == -1) throw ErrnoFatal("daemon");

    struct sigaction handler;

    sigemptyset(&handler.sa_mask);
    handler.sa_handler = handleSignal;
    handler.sa_flags = 0;

    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++) {
        result = sigaction(signals[i], &handler, nullptr);
        if (result == -1) throw ErrnoFatal("sigaction");
    }

    while (!shutdownServer) {
        /* receive command from new connection */
        sock.recv(req);

        switch (req->cmd) {
            /* Handle update */
            case Request::Command::update:
            {
                struct timeval time;
                result = gettimeofday(&time, nullptr);
                if (result == -1) throw ErrnoFatal("gettimeofday");

                if (req->length > 0) {
                    /* Generate history entry */
                    //FIXME: optimise
                    stringstream buf;
                    buf << '#' << time.tv_sec << "\n" << req->payload << "\n";
                    string output = buf.str();
                    history << output;
                    history.flush();

                    /* Append to all histories */
                    for(auto it = caches.begin(); it != caches.end(); ) {
                        it->second << output;
                        /* Purge full/errored caches */
                        if (it->second.should_reload()) {
                            it = caches.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                /* Check whether to reload or update */
                auto& current = caches[req->origin];
                if (current.is_new()) {
                    rep.cmd = Reply::Command::reload_file;
                    rep.length = 0;
                } else {
                    rep.cmd = Reply::Command::new_hist;
                    rep.length = current.length();
                    if (rep.length) rep.length += 1;
                }

                /* Send result and close connection */
                sock.send(rep, current);
                current.reset();
                break;
            }
            case Request::Command::deregister:
                /* Remove from cache */
                caches.erase(req->origin);
                break;
            case Request::Command::shutdown:
                shutdownServer = true;
                break;
            case Request::Command::log_start: {
                //FIXME: Error handling
                freopen(req->payload, "w", stderr);
                cerr << "Logging started!" << endl;
                cerr << "IOV_MAX: " << IOV_MAX << endl;
                cerr << "ARG_MAX: " << ARG_MAX << endl;
                break;
            }
            case Request::Command::log_stop: {
                cerr << "Stopping logging!" << endl;
                //FIXME: Error handling
                freopen("/dev/null", "w", stderr);
                break;
            }
        }
    }

    throw Terminate(EXIT_SUCCESS);
}

static void
launchServer
( int argc, char *argv[], UnixSocket& sock, const string& sockPath
, const string& historyPath)
{
    int ret = fork();
    if (ret == -1) throw ErrnoFatal("fork");
    else if (ret == 0) {
        /* Child process */
        sock.close();

        try {
            setProcName(argc, argv, "sync-historyd");
            server(UnixSocket(sockPath), ofstream(historyPath, ios_base::app));
        } catch (const ErrnoFatal& exc) {
            if (exc.error == EADDRINUSE && exc.func == "bind") {
                /* Either another server won the race, or left over socket. */
                throw Terminate(EXIT_SUCCESS);
            }
            throw;
        }
    }

    /* Wait for child to exit */
    if (waitpid(ret, &ret, 0) == -1) throw ErrnoFatal("waitpid");

    /* Child should return successful */
    if (ret != 0) throw Terminate();
}

static int
client
( int argc, char *argv[], const string& runtimePath, const string& historyPath
, pid_t pid, Request::Command cmd, char* data)
{
    ssize_t ret;
    UnixSocket sock(runtimePath + "/.sync-" + to_string(pid));
    string sockPath(runtimePath + "/.sync_history");
    Reply *rep = reinterpret_cast<Reply*>(messageBuffer);
    Request req { pid , cmd , data ? strlen(data) + 1 : 0};

    try {
        sock.send(req, data, sockPath);
    } catch (const ErrnoFatal& exc) {
        if (exc.error == ENOENT && exc.func == "sendmsg") {
            /* No server socket! */
            launchServer(argc, argv, sock, sockPath, historyPath);
            /* Retry after launching server */
            sock.send(req, data, sockPath);
        } else {
            /* Rethrow error */
            throw;
        }
    }

    /* Only wait for reply when updating */
    if (cmd != Request::Command::update) return 0;

    sock.recv(rep);
    switch (rep->cmd) {
        case Reply::Command::new_hist:
            if (rep->length) {
                ret = write(STDOUT_FILENO, rep->payload, --rep->length);
                if (ret == -1) {
                    throw ErrnoFatal("write");
                } else if (static_cast<size_t>(ret) != rep->length) {
                    throw FatalError("write(): Didn't write full history!");
                }
            }
            break;
        case Reply::Command::reload_file:
            /* Indicate history reload */
            return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Request::Command cmd;
    pid_t pid = 0;
    char *data = nullptr;
    struct passwd *passwdEnt;

    try {
        passwdEnt = getpwuid(getuid());
        if (passwdEnt == nullptr) throw ErrnoFatal("getpwduid");

        string homeDirPath = string(passwdEnt->pw_dir);
        string historyPath = homeDirPath + "/.bash_history_synced";

        if ((argc == 3 || argc == 4) && !strcmp(argv[1], "update")) {
            cmd = Request::Command::update;
            pid = stoi(argv[2]);
            if (argc == 4) data = argv[3];
        } else if (argc == 3 && !strcmp(argv[1], "deregister")) {
            cmd = Request::Command::deregister;
            pid = stoi(argv[2]);
        } else if (argc == 2 && !strcmp(argv[1], "shutdown")) {
            cmd = Request::Command::shutdown;
        } else if (argc == 2 && !strcmp(argv[1], "history-path")) {
            cout << historyPath << endl;
            exit(EXIT_SUCCESS);
        } else if (argc == 4 && !strcmp(argv[1], "log") && !strcmp(argv[2], "start") ) {
            cmd = Request::Command::log_start;
            data = argv[3];
        } else if (argc == 3 && !strcmp(argv[1], "log") && !strcmp(argv[2], "stop") ) {
            cmd = Request::Command::log_stop;
        } else {
            throw Terminate();
        }


        string runtimePath;
        const char *runtimePathPtr = std::getenv("XDG_RUNTIME_DIR");
        if (runtimePathPtr) runtimePath = runtimePathPtr;
        else runtimePath = homeDirPath;

        return client(argc, argv, runtimePath, historyPath, pid, cmd, data);
    } catch (const FatalError& exc) {
        /* Program exit due to unexpected errors */
        cerr << exc.what() << endl;
        exit(EXIT_FAILURE);
    } catch (const Terminate& exc) {
        /* Controlled program exit. Allows RAII to unlink sockets. */
        exit(exc.status);
    } catch (const exception& exc) {
        /* Standard library errors */
        cerr << exc.what() << endl;
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}
