/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"

template<size_t size> struct ByteSwapT
{
};

template<> struct ByteSwapT<1>
{
    using UIntType = uint8_t;
    static uint8_t SwapBE(uint8_t value)
    {
        return value;
    }
};

template<> struct ByteSwapT<2>
{
    using UIntType = uint16_t;
    static uint16_t SwapBE(uint16_t value)
    {
        return static_cast<uint16_t>((value << 8) | (value >> 8));
    }
};

template<> struct ByteSwapT<4>
{
    using UIntType = uint32_t;
    static uint32_t SwapBE(uint32_t value)
    {
        return static_cast<uint32_t>(
            ((value << 24) | ((value << 8) & 0x00FF0000) | ((value >> 8) & 0x0000FF00) | (value >> 24)));
    }
};

template<> struct ByteSwapT<8>
{
    using UIntType = uint64_t;
    static uint64_t SwapBE(uint64_t value)
    {
        value = (value & 0x00000000FFFFFFFF) << 32 | (value & 0xFFFFFFFF00000000) >> 32;
        value = (value & 0x0000FFFF0000FFFF) << 16 | (value & 0xFFFF0000FFFF0000) >> 16;
        value = (value & 0x00FF00FF00FF00FF) << 8 | (value & 0xFF00FF00FF00FF00) >> 8;
        return value;
    }
};

template<typename T> static T ByteSwapBE(const T& value)
{
    using ByteSwap = ByteSwapT<sizeof(T)>;
    typename ByteSwap::UIntType result = ByteSwap::SwapBE(reinterpret_cast<const typename ByteSwap::UIntType&>(value));
    return *reinterpret_cast<T*>(&result);
}
