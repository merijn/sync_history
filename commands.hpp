#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <unistd.h>

struct Request {
    enum class Command {
        update,
        deregister,
        shutdown,
        log_start,
        log_stop
    };

    pid_t origin;
    Command cmd;
    size_t length;
    char payload[];
};

struct Reply {
    enum class Command {
        new_hist,
        reload_file
    };

    Command cmd;
    size_t length;
    char payload[];
};
#endif
