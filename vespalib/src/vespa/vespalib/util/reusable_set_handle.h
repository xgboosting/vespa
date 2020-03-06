// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "reusable_set.h"

namespace vespalib {

class ReusableSetPool;

class ReusableSetHandle
{
private:
    using Mark = ReusableSet::Mark;
    using RSUP = std::unique_ptr<ReusableSet>;

    Mark *_bits;
    Mark _curval;
    RSUP _owned;
    ReusableSetPool &_pool;

public:
    ReusableSetHandle(RSUP backing, ReusableSetPool& owner)
      : _bits(backing->bits()),
        _curval(backing->generation()),
        _owned(std::move(backing)),
        _pool(owner)
    {}

    ~ReusableSetHandle();

    void mark(size_t id) {
        _bits[id] = _curval;
    }

    bool is_marked(size_t id) const {
        return (_bits[id] == _curval);
    }

    // for unit tests and statistics
    size_t capacity() const { return _owned->capacity(); }
    Mark generation() const { return _curval; }
};

} // namespace
