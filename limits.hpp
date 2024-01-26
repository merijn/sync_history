#ifndef LIMITS_HPP
#define LIMITS_HPP

#include <unistd.h>

#include <climits>

constexpr size_t max_command_size = ARG_MAX;
constexpr size_t max_buffer_size = IOV_MAX;
#endif
