#ifndef HISTORY_CACHE_HPP
#define HISTORY_CACHE_HPP

#include <sstream>

struct membuf : public std::streambuf {
    using seekdir = std::ios_base::seekdir;
    using openmode = std::ios_base::openmode;
    using ios_base = std::ios_base;

    template <size_t Size>
    membuf(char (&array)[Size])
    {
        setp(array, array + Size - 2);
        std::fill_n(array, Size, 0);
    }

  protected:
    std::streamsize
    xsputn(const char_type* s, std::streamsize count) override;

    pos_type seekpos(pos_type pos, openmode which = ios_base::out) override;

    pos_type
    seekoff(off_type off, seekdir dir, openmode which = ios_base::out) override;

    ~membuf() override;
};

template<size_t MAX>
class HistoryCache : virtual membuf, public std::ostream {
    bool fresh;
    char array[MAX];

  public:
    HistoryCache() : membuf(array), std::ostream(this), fresh(true)
    {}

    ~HistoryCache() override
    {}

    operator void*()
    { return array; }

    size_t length()
    { return static_cast<size_t>(tellp()); }

    bool is_new()
    {
        bool result = fresh;
        fresh = false;
        return result;
    }

    bool should_reload()
    { return eof() || fail(); }

    void reset()
    { clear(); seekp(0); }
};
#endif
