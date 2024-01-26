#ifndef UTIL_HPP
#define UTIL_HPP

#include <stdlib.h>

#include <stdexcept>
#include <string>

#if defined(__GLIBC__)
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

class Terminate {
  public:
    const int status;

    Terminate(int s = EXIT_FAILURE) : status(s)
    {}

    Terminate(const Terminate& exc) : status(exc.status)
    {}

    ~Terminate();
};

/* Handle premature exit and grabbing errno code */
class FatalError : public std::runtime_error {
  public:
    FatalError(const std::string &msg) : std::runtime_error(msg)
    {}

    FatalError(const FatalError& exc) : std::runtime_error(exc.what())
    {}

    ~FatalError() override;
};

class ErrnoFatal : public FatalError {
    std::string from_extra(const std::string& msg)
    {
        if (msg.empty()) return "";
        return ": " + msg;
    }

  public:
    const int error;
    const std::string func;

    ErrnoFatal(const std::string &function, const std::string &extra = "")
     : FatalError(function + "(): " + std::string(strerror(errno)) + from_extra(extra))
     , error(errno), func(function)
    {}

    ErrnoFatal(const ErrnoFatal& exc)
     : FatalError(exc.what()), error(exc.error), func(exc.func)
    {}

    ~ErrnoFatal() override;
};
#endif
