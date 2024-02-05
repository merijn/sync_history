#include "history_cache.hpp"

std::streamsize
membuf::xsputn(const char_type* s, std::streamsize count)
{
    std::streamsize result = std::streambuf::xsputn(s, count);
    /* Always legal, because epptr points before in due to constructor */
    *pptr() = '\0';
    return result;
}

membuf::pos_type
membuf::seekpos(pos_type pos, openmode which)
{ return seekoff(pos, ios_base::beg, which); }

membuf::pos_type
membuf::seekoff(off_type off, seekdir dir, openmode which)
{
    if ((which & ios_base::out) != ios_base::out) {
        return pos_type(off_type(-1));
    }

    off_type current = pptr() - pbase();
    off_type max = epptr() - pbase();

    if (dir == ios_base::end) off = max - off;
    else if (dir == ios_base::cur) off = current + off;

    if (off < 0 || off > max) return pos_type(off_type(-1));
    pbump(static_cast<int>(off - current));
    return pptr() - pbase();
}

membuf::~membuf() {}
