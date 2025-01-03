// Copyright 2024 The VGC Developers
// See the COPYRIGHT file at the top-level directory of this distribution
// and at https://github.com/vgc/vgc/blob/master/COPYRIGHT
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VGC_CORE_RANGES_H
#define VGC_CORE_RANGES_H

/// \file vgc/core/ranges.h
/// \brief Utilities for using ranges.
///
/// This header contains functions and structures for creating, manipulating,
/// and iterating over ranges.

// range-v3 uses un-parenthesized min/max, so we need this in case <windows.h>
// was included before this header, otherwise inluding range-v3 fails to compile.
#ifdef min
#    undef min
#endif
#ifdef max
#    undef max
#endif

#include <range/v3/all.hpp>

#include <vgc/core/templateutil.h>

namespace vgc {

namespace c20 {

/// Namespace where range concepts and algorithms are defined.
/// For now, this is an alias of `ranges::cpp20` from range-v3.
/// Once we require C++20, this should become an alias for `std::ranges`.
///
namespace ranges = ::ranges::cpp20;

/// Namespace where range views are defined.
/// For now, this is an alias of `ranges::cpp20::views` from range-v3.
/// Once we require C++20, this should become an alias for `std::ranges::view`.
///
namespace views = ::ranges::cpp20::views;

// The aliases below are provided in range-v3 `ranges::cpp20` namespace, but we
// import them here as they are directy in the `std` namespace in C++20.

// From <range/v3/iterator/concepts.hpp>
using ranges::bidirectional_iterator;
using ranges::contiguous_iterator;
using ranges::forward_iterator;
using ranges::incrementable;
using ranges::indirect_relation;
using ranges::indirect_result_t;
using ranges::indirect_strict_weak_order;
using ranges::indirect_unary_predicate;
using ranges::indirectly_comparable;
using ranges::indirectly_copyable;
using ranges::indirectly_copyable_storable;
using ranges::indirectly_movable;
using ranges::indirectly_movable_storable;
using ranges::indirectly_readable;
using ranges::indirectly_regular_unary_invocable;
using ranges::indirectly_swappable;
using ranges::indirectly_unary_invocable;
using ranges::indirectly_writable;
using ranges::input_iterator;
using ranges::input_or_output_iterator;
using ranges::mergeable;
using ranges::output_iterator;
using ranges::permutable;
using ranges::projected;
using ranges::random_access_iterator;
using ranges::sentinel_for;
using ranges::sized_sentinel_for;
using ranges::sortable;
using ranges::weakly_incrementable;

// From <range/v3/iterator/traits.hpp>
using ranges::iter_common_reference_t;
using ranges::iter_difference_t;
using ranges::iter_reference_t;
using ranges::iter_rvalue_reference_t;
using ranges::iter_value_t;

} // namespace c20

namespace c26 {

// https://en.cppreference.com/w/cpp/iterator/projected_value_t
template<
    typename I,
    typename Proj,
    VGC_REQUIRES(c20::indirectly_readable<I> //
                     && c20::indirectly_regular_unary_invocable<Proj, I>)>
using projected_value_t =
    c20::remove_cvref_t<std::invoke_result_t<Proj&, c20::iter_value_t<I>&>>;

namespace ranges {

// https://en.cppreference.com/w/cpp/algorithm/ranges/contains
struct __contains_fn {

    // TODO: Uncomment concept requirements once we can use C++20

    //template<
    //    std::input_iterator I,
    //    std::sentinel_for<I> S,
    //    class Proj = std::identity,
    //    class T = std::projected_value_t<I, Proj>>
    //requires std::indirect_binary_predicate<
    //    ranges::equal_to,
    //    std::projected<I, Proj>,
    //    const T*>
    template<
        typename I,
        typename S,
        typename Proj = c20::identity,
        typename T = projected_value_t<I, Proj>,
        VGC_REQUIRES(c20::input_iterator<I>&& c20::sentinel_for<I, S>)>
    constexpr bool operator()(I first, S last, const T& value, Proj proj = {}) const {
        return c20::ranges::find(std::move(first), last, value, proj) != last;
    }

    //template<
    //    ranges::input_range R,
    //    class Proj = std::identity,
    //    class T = std::projected_value_t<ranges::iterator_t<R>, Proj>>
    //requires std::indirect_binary_predicate<
    //    ranges::equal_to,
    //    std::projected<ranges::iterator_t<R>, Proj>,
    //    const T*>
    template<
        typename R,
        typename Proj = c20::identity,
        typename T = projected_value_t<c20::ranges::iterator_t<R>, Proj>,
        VGC_REQUIRES(c20::ranges::input_range<R>)>
    constexpr bool operator()(R&& r, const T& value, Proj proj = {}) const {
        return (*this)(
            c20::ranges::begin(r), c20::ranges::end(r), std::move(value), proj);
    }
};

inline constexpr __contains_fn contains{};

} // namespace ranges

} // namespace c26

namespace core {

/// Type trait for `isCompatibleIterator<It, T>`.
///
template<typename It, typename T, typename Proj = c20::identity, typename SFINAE = void>
struct IsCompatibleIterator : std::false_type {};

template<typename It, typename T, typename Proj>
struct IsCompatibleIterator<
    It,
    T,
    Proj,
    Requires<std::is_convertible_v<c26::projected_value_t<It, Proj>, T>>>
    : std::true_type {};

/// Returns whether `It` is an iterator type whose projected value by `Proj` is
/// convertible to `T`.
///
template<typename It, typename T, typename Proj = c20::identity>
inline constexpr bool isCompatibleIterator = IsCompatibleIterator<It, T, Proj>::value;

/// Type trait for `isCompatibleInputRange<Range, T, Proj>`.
///
template<
    typename Range,
    typename T,
    typename Proj = c20::identity,
    typename SFINAE = void>
struct IsCompatibleInputRange : std::false_type {};

template<typename Range, typename T, typename Proj>
struct IsCompatibleInputRange<
    Range,
    T,
    Proj,
    Requires<                           //
        c20::ranges::input_range<Range> //
        && isCompatibleIterator<c20::ranges::iterator_t<Range>, T, Proj>>>
    : std::true_type {};

/// Returns whether `Range` is an input range type whose iterator type
/// is compatible with `T` under the projection `Proj`.
///
/// \sa `isCompatibleIterator`.
///
template<typename Range, typename T, typename Proj = c20::identity>
inline constexpr bool isCompatibleInputRange =
    IsCompatibleInputRange<Range, T, Proj>::value;

/// Type trait for `isInputIteratorPair<I, S>`.
///
template<typename I, typename S, typename SFINAE = void>
struct IsInputIteratorPair : std::false_type {};

template<typename I, typename S>
struct IsInputIteratorPair<
    I,
    S,
    Requires<c20::input_iterator<I> && c20::sentinel_for<S, I>>> //
    : std::true_type {};

/// Returns whether the pair of types `I` and `S` define an input range.
///
template<typename I, typename S>
inline constexpr bool isInputIteratorPair = IsInputIteratorPair<I, S>::value;

/// Type trait for `isCompatibleInputIteratorPair<I, S, Proj>`.
///
template<
    typename I,
    typename S,
    typename T,
    typename Proj = c20::identity,
    typename SFINAE = void>
struct IsCompatibleInputIteratorPair : std::false_type {};

template<typename I, typename S, typename T, typename Proj>
struct IsCompatibleInputIteratorPair<
    I,
    S,
    T,
    Proj,
    Requires<isInputIteratorPair<I, S> && isCompatibleIterator<I, T, Proj>>>
    : std::true_type {};

/// Returns whether the pair of types `I` and `S` define an input range
/// with `I` being compatible with `T` under the projection `Proj`.
///
/// \sa `isInputIteratorPair`, `isCompatibleIterator`.
///
template<typename I, typename S, typename T, typename Proj = c20::identity>
inline constexpr bool isCompatibleInputIteratorPair =
    IsCompatibleInputIteratorPair<I, S, T, Proj>::value;

} // namespace core

} // namespace vgc

#endif // VGC_CORE_RANGES_H
