//          Copyright Jean Pierre Cimalando 2019.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <cstddef>
#include <sys/stat.h>
#include <sys/mman.h>

struct mapped_file {
    mapped_file() {}
    ~mapped_file() { close(); }
    bool open(int fd);
    void close();

    void *data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    void *data_ = nullptr;
    std::size_t size_ = 0;
    mapped_file(const mapped_file &) = delete;
    mapped_file &operator=(const mapped_file &) = delete;
};

inline bool mapped_file::open(int fd)
{
    close();

    struct stat st;
    if (fstat(fd, &st) != 0)
        return false;

    void *data = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED)
        return false;

    data_ = data;
    size_ = st.st_size;
    return true;
}

inline void mapped_file::close()
{
    if (data_) {
        munmap(data_, size_);
        data_ = nullptr;
        size_ = 0;
    }
}
