// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "reusable_set.h"
#include "reusable_set_handle.h"

#include <vector>
#include <mutex>

namespace vespalib {

class ReusableSetPool
{
    using RSUP = std::unique_ptr<ReusableSet>;
    using Guard = std::lock_guard<std::mutex>;
    std::vector<RSUP> _lru_stack;
    std::mutex _lock;
    size_t _reuse_count;
    size_t _create_count;
    size_t _total_memory_used;
    const size_t _min_size;
    const size_t _grow_percent;

    ReusableSetPool(const ReusableSetPool &) = delete;
    ReusableSetPool& operator= (const ReusableSetPool &) = delete;

public:
    ReusableSetPool()
      : _lru_stack(), _lock(),
        _reuse_count(0), _create_count(0),
        _total_memory_used(sizeof(ReusableSetPool)),
        _min_size(248), _grow_percent(20)
    {}

    ReusableSetHandle get(size_t size) {
        Guard guard(_lock);
        size_t last_used_size = 0;
        while (! _lru_stack.empty()) {
            RSUP r = std::move(_lru_stack.back());
            _lru_stack.pop_back();
            if (r->capacity() >= size) {
                r->clear();
                ++_reuse_count;
                return ReusableSetHandle(std::move(r), *this);
            }
            _total_memory_used -= r->memory_usage();
            last_used_size = std::max(last_used_size, r->capacity());
        }
        double grow_factor = (1.0 + _grow_percent / 100.0);
        last_used_size *= grow_factor;
        size_t at_least_size = std::max(_min_size, last_used_size);
        RSUP r = std::make_unique<ReusableSet>(std::max(at_least_size, size));
        ++_create_count;
        _total_memory_used += r->memory_usage();
        return ReusableSetHandle(std::move(r), *this);
    }

    void reuse(RSUP used) {
        Guard guard(_lock);
        _lru_stack.push_back(std::move(used));
    }

    // for unit testing and statistics
    size_t reuse_count() const { return _reuse_count; }
    size_t create_count() const { return _create_count; }
    size_t memory_usage() const { return _total_memory_used; }
};

} // namespace
