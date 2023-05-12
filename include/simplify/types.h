#ifndef SIMPLIFY_TYPES_H
#define SIMPLIFY_TYPES_H


#include <boost/geometry/geometries/linestring.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace simplify
{

using point_t = boost::geometry::model::d2::point_xy<int64_t>;
using linestring_t = boost::geometry::model::linestring<point_t>;

} // namespace simplify


#endif // SIMPLIFY_TYPES_H
