/*
 * Copyright (c) 2024 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * C++20 concepts for the gem5 packet infrastructure.
 *
 * These concepts replace the previous unconstrained template parameters
 * and void* usage in the packet data access and SenderState APIs,
 * providing compile-time type safety.
 */

#ifndef __MEM_PACKET_CONCEPTS_HH__
#define __MEM_PACKET_CONCEPTS_HH__

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gem5
{

class Packet;

/**
 * Concept for types that can be used as packet data payloads.
 *
 * A PacketDataType must be trivially copyable (safe for memcpy) and
 * must not be void or a function type. This replaces the unconstrained
 * template parameters in getPtr<T>(), dataStatic<T>(), etc. that
 * previously allowed any type including void.
 */
template <typename T>
concept PacketDataType =
    !std::is_void_v<T> &&
    !std::is_function_v<T> &&
    (std::is_trivially_copyable_v<T> || std::is_same_v<T, std::byte>);

/**
 * Concept for types that can be used with getRaw<T>() / setRaw<T>()
 * and the byte-swapping accessors (getBE, getLE, get, setBE, setLE, set).
 *
 * These must be concrete value types with a known size that can be safely
 * reinterpreted from a byte buffer. This includes arithmetic types, enums,
 * trivially copyable types, and standard-layout types (e.g. gem5 BitUnion
 * types which have user-defined operators but are still POD-like).
 */
template <typename T>
concept PacketScalarType =
    !std::is_void_v<T> &&
    !std::is_function_v<T> &&
    !std::is_reference_v<T> &&
    !std::is_pointer_v<T> &&
    !std::is_array_v<T> &&
    (std::is_trivially_copyable_v<T> || std::is_standard_layout_v<T>);

/**
 * Concept for types that can serve as raw memory buffers for packet
 * data operations like readBlob/writeBlob in PortProxy.
 *
 * Accepts byte-like types (uint8_t, char, unsigned char, std::byte)
 * as well as void for backward compatibility with existing APIs that
 * accept void* for arbitrary memory regions.
 */
template <typename T>
concept MemoryBufferType =
    std::is_void_v<T> ||
    std::is_same_v<std::remove_cv_t<T>, uint8_t> ||
    std::is_same_v<std::remove_cv_t<T>, char> ||
    std::is_same_v<std::remove_cv_t<T>, unsigned char> ||
    std::is_same_v<std::remove_cv_t<T>, std::byte> ||
    std::is_trivially_copyable_v<T>;

} // namespace gem5

#endif // __MEM_PACKET_CONCEPTS_HH__
