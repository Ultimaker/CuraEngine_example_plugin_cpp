#ifndef SIMPLIFY_SIMPLIFY_H
#define SIMPLIFY_SIMPLIFY_H

#include <simplify/types.h>

#include <boost/geometry/algorithms/simplify.hpp>

namespace simplify
{

namespace detail
{

struct simplify_fn
{
    linestring_t operator()(linestring_t&& polygon, auto distance) const
    {
        linestring_t simplified;
        boost::geometry::simplify<>(polygon, simplified, distance);
        return simplified;
    }
};

} // namespace detail

constexpr auto simplify = detail::simplify_fn{};

} // namespace simplify

#endif // SIMPLIFY_SIMPLIFY_H
