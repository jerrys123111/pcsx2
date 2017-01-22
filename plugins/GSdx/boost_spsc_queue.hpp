// This version is a stripped down version of boost/lockfree/spsc_queue.hpp boost_spsc_queue.hpp
// Rational
// * Performance is better on linux than the standard std::queue
// * Performance in the same on windows
// => 100-200MB of dependency feel rather unfriendly

// Potential optimization
// * plug condition variable into the queue directly to avoid redundant m_count

// * Restore boost optimization
//   => unlikely or replace it with a % (if size is 2^n)


//  lock-free single-producer/single-consumer ringbuffer
//  this algorithm is implemented in various projects (linux kernel)
//
//  Copyright (C) 2009-2013 Tim Blechmann
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

// Boost Software License - Version 1.0 - August 17th, 2003
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <atomic>

// I don't like it
using namespace std;

template <typename T, size_t max_size>
class ringbuffer_base
{
    static const int padding_size = 64 - sizeof(size_t);

    atomic<size_t> write_index_;
    char padding1[padding_size]; /* force read_index and write_index to different cache lines */
    atomic<size_t> read_index_;
    char padding2[padding_size]; /* force read_index and pending_pop_read_index to different cache lines */

    size_t pending_pop_read_index;

    T *buffer;

    ringbuffer_base(ringbuffer_base const &) = delete;
    ringbuffer_base(ringbuffer_base &&)      = delete;
    const ringbuffer_base& operator=( const ringbuffer_base& ) = delete;

public:
    ringbuffer_base(void):
        write_index_(0), read_index_(0), pending_pop_read_index(0)
    {
        // Use dynamically allocation here with no T object dependency
        // Otherwise the ringbuffer_base destructor will call the destructor
        // of T which crash if T is a (invalid) shared_ptr.
        //
        // Note another solution will be to create a char buffer as union of T
        buffer = (T*)_aligned_malloc(sizeof(T)*max_size, 32);
    }

    ~ringbuffer_base(void) {
        // destroy all remaining items
        T out;
        while (pop(out)) {};

        _aligned_free(buffer);
    }


    static size_t next_index(size_t arg)
    {
        size_t ret = arg + 1;
#if 0
        // Initial boost code
        while (unlikely(ret >= max_size))
            ret -= max_size;
#else
        ret %= max_size;
#endif
        return ret;
    }

    bool push(T const & t)
    {
        const size_t write_index = write_index_.load(memory_order_relaxed);  // only written from push thread
        const size_t next = next_index(write_index);

        if (next == read_index_.load(memory_order_acquire))
            return false; /* ringbuffer is full */

        new (buffer + write_index) T(t); // copy-construct

        write_index_.store(next, memory_order_release);

        return true;
    }

    bool pop (T & ret)
    {
        const size_t write_index = write_index_.load(memory_order_acquire);
        const size_t read_index  = read_index_.load(memory_order_relaxed); // only written from pop thread
        if (empty(write_index, read_index))
            return false;

        ret = buffer[read_index];
        buffer[read_index].~T();

        size_t next = next_index(read_index);
        read_index_.store(next, memory_order_release);
        return true;
    }

    T& front()
    {
        pending_pop_read_index = read_index_.load(memory_order_relaxed); // only written from pop thread

        return buffer[pending_pop_read_index];
    }

    void pop()
    {
        buffer[pending_pop_read_index].~T();

        size_t next = next_index(pending_pop_read_index);
        read_index_.store(next, memory_order_release);
    }

    template <typename Functor>
    bool consume_one(Functor & f)
    {
        const size_t write_index = write_index_.load(memory_order_acquire);
        const size_t read_index  = read_index_.load(memory_order_relaxed); // only written from pop thread
        if (empty(write_index, read_index))
            return false;

        f(buffer[read_index]);
        buffer[read_index].~T();

        size_t next = next_index(read_index);
        read_index_.store(next, memory_order_release);
        return true;
    }

public:
    /** reset the ringbuffer
     *
     * \note Not thread-safe
     * */
    void reset(void)
    {
        write_index_.store(0, memory_order_relaxed);
        read_index_.store(0, memory_order_release);
    }

    /** Check if the ringbuffer is empty
     *
     * \return true, if the ringbuffer is empty, false otherwise
     * \note Due to the concurrent nature of the ringbuffer the result may be inaccurate.
     * */
    bool empty(void)
    {
        return empty(write_index_.load(memory_order_relaxed), read_index_.load(memory_order_relaxed));
    }

    /**
     * \return true, if implementation is lock-free.
     *
     * */
    bool is_lock_free(void) const
    {
        return write_index_.is_lock_free() && read_index_.is_lock_free();
    }

    size_t size() const
    {
        const size_t write_index =  write_index_.load(memory_order_relaxed);
        const size_t read_index = read_index_.load(memory_order_relaxed);
        if (read_index > write_index) {
            return (write_index + max_size) - read_index;
        } else {
            return write_index - read_index;
        }
    }

private:
    bool empty(size_t write_index, size_t read_index)
    {
        return write_index == read_index;
    }
};
