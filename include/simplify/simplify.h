// Copyright (c) 2023 UltiMaker
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_SIMPLIFY_H
#define UTILS_SIMPLIFY_H

#include <queue>
#include <vector>

#include "simplify/point_container.h"

class Simplify
{
    constexpr static int64_t min_resolution = 5; // 5 units, regardless of how big those are, to allow for rounding errors.

public:
    /*!
     * Construct a simplifier, storing the simplification parameters in the
     * instance (as a factory pattern).
     * \param max_resolution Line segments smaller than this are considered for
     * joining with other line segments.
     * \param max_deviation If removing a vertex would cause a deviation larger
     * than this, it cannot be removed.
     * \param max_area_deviation If removing a vertex would cause the covered
     * area in total to change more than this, it cannot be removed.
     */
    constexpr Simplify(const int64_t max_resolution, const int64_t max_deviation, const int64_t max_area_deviation) noexcept : max_resolution{max_resolution}, max_deviation{max_deviation}, max_area_deviation{max_area_deviation} {};

    /*!
     * Line segments shorter than this size should be considered for removal.
     */
    int64_t max_resolution;

    /*!
     * If removing a vertex causes a deviation further than this, it may not be
     * removed.
     */
    int64_t max_deviation;

    /*!
     * If removing a vertex causes the covered area of the line segments to
     * change by more than this, it may not be removed.
     */
    int64_t max_area_deviation;

    /*!
     * The main simplification algorithm starts here.
     * \tparam Polygonal A polygonal object, which is a list of vertices.
     * \param polygon The polygonal chain to simplify.
     * \param is_closed Whether this is a closed polygon or an open polyline.
     * \return A simplified polygonal chain.
     */
    concepts::poly_range auto simplify(const concepts::poly_range auto& polygon)
    {
        using Polygonal = decltype(polygon);
        using poly_t = std::remove_cvref_t<Polygonal>;
        constexpr bool is_closed = concepts::is_closed_point_container<Polygonal>;
        constexpr size_t min_size = is_closed ? 3 : 2;

        if (polygon.size() < min_size) // For polygon, 2 or fewer vertices is degenerate. Delete it. For polyline, 1 vertex is degenerate.
        {
            return poly_t{};
        }
        if (polygon.size() == min_size) // For polygon, don't reduce below 3. For polyline, not below 2.
        {
            return polygon;
        }

        std::vector<bool> to_delete(polygon.size(), false);
        auto comparator = [](const std::pair<size_t, int64_t>& vertex_a, const std::pair<size_t, int64_t>& vertex_b) { return vertex_a.second > vertex_b.second || (vertex_a.second == vertex_b.second && vertex_a.first > vertex_b.first); };
        std::priority_queue<std::pair<size_t, int64_t>, std::vector<std::pair<size_t, int64_t>>, decltype(comparator)> by_importance(comparator);

        // Add the initial points.
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const int64_t vertex_importance = importance(polygon, to_delete, i);
            by_importance.emplace(i, vertex_importance);
        }

        // Iteratively remove the least important point until a threshold.
        poly_t result(polygon); // Make a copy so that we can also shift vertices.
        int64_t vertex_importance = 0;
        while (by_importance.size() > min_size)
        {
            std::pair<size_t, int64_t> vertex = by_importance.top();
            by_importance.pop();
            // The importance may have changed since this vertex was inserted. Re-compute it now.
            // If it doesn't change, it's safe to process.
            vertex_importance = importance(result, to_delete, vertex.first);
            if (vertex_importance != vertex.second)
            {
                by_importance.emplace(vertex.first, vertex_importance); // Re-insert with updated importance.
                continue;
            }

            if (vertex_importance <= max_deviation * max_deviation)
            {
                remove(result, to_delete, vertex.first, vertex_importance);
            }
        }

        // Now remove the marked vertices in one sweep.
        poly_t filtered;
        for (size_t i = 0; i < result.size(); ++i)
        {
            if (! to_delete[i])
            {
                filtered.emplace_back(result[i]);
            }
        }

        return filtered;
    }

private:

    static auto getDistFromLine(const geometry::Point& p, const geometry::Point& a, const geometry::Point& b)
    {
        //  x.......a------------b
        //  :
        //  :
        //  p
        // return px_size
        const geometry::Point vab = b - a;
        const geometry::Point vap = p - a;
        const auto ab_size = std::hypot(vab.X, vab.Y);
        if(ab_size == 0) //Line of 0 length. Assume it's a line perpendicular to the direction to p.
        {
            return std::hypot(vap.X, vap.Y);
        }
        const auto area_times_two = std::abs((p.X - b.X) * (p.Y - a.Y) + (a.X - p.X) * (p.Y - b.Y)); // Shoelace formula, factored
        return area_times_two / ab_size;
    }

    static auto cross(const geometry::Point& p0, const geometry::Point& p1)
    {
        return p0.X * p1.Y - p0.Y * p1.X;
    }

    static constexpr auto round_divide_signed(const std::integral auto dividend, const std::integral auto divisor) //!< Return dividend divided by divisor rounded to the nearest integer
    {
        if ((dividend < 0) ^ (divisor < 0)) //Either the numerator or the denominator is negative, so the result must be negative.
        {
            return (dividend - divisor / 2) / divisor; //Flip the .5 offset to do proper rounding in the negatives too.
        }
        return (dividend + divisor / 2) / divisor;
    }

    static std::optional<geometry::Point> lineLineIntersection(const geometry::Point& a, const geometry::Point& b, const geometry::Point& c, const geometry::Point& d)
    {
        //Adapted from Apex: https://github.com/Ghostkeeper/Apex/blob/eb75f0d96e36c7193d1670112826842d176d5214/include/apex/line_segment.hpp#L91
        //Adjusted to work with lines instead of line segments.
        const auto l1_delta = b - a;
        const auto l2_delta = d - c;
        const auto divisor = cross(l1_delta, l2_delta); //Pre-compute divisor needed for the intersection check.
        if(divisor == 0)
        {
            //The lines are parallel if the cross product of their directions is zero.
            return std::nullopt;
        }

        //Create a parametric representation of each line.
        //We'll equate the parametric equations to each other to find the intersection then.
        //Parametric equation is L = P + Vt (where P and V are a starting point and directional vector).
        //We'll map the starting point of one line onto the parameter system of the other line.
        //Then using the divisor we can see whether and where they cross.
        const auto starts_delta = a - c;
        const auto l1_parametric = cross(l2_delta, starts_delta);
        auto result = a + geometry::Point { round_divide_signed(l1_parametric * l1_delta.X, divisor), round_divide_signed(l1_parametric * l1_delta.Y, divisor)};

        if(std::abs(result.X) > std::numeric_limits<int32_t>::max() || std::abs(result.Y) > std::numeric_limits<int32_t>::max())
        {
            //Intersection is so far away that it could lead to integer overflows.
            //Even though the lines aren't 100% parallel, it's better to pretend they are. They are practically parallel.
            return std::nullopt;
        }
        return result;
    }

    int64_t importance(const concepts::poly_range auto& polygon, const std::vector<bool>& to_delete, const size_t index)
    {
        using Polygonal = decltype(polygon);
        constexpr bool is_closed = concepts::is_closed_point_container<Polygonal>;
        size_t poly_size = polygon.size();
        if (! is_closed && (index == 0 || index == poly_size - 1))
        {
            return std::numeric_limits<int64_t>::max(); // Endpoints of the polyline must always be retained.
        }
        // From here on out we can safely look at the vertex neighbors and assume it's a polygon. We won't go out of bounds of the polyline.

        const geometry::Point& vertex = polygon[index];
        const size_t before_index = previousNotDeleted(index, to_delete);
        const size_t after_index = nextNotDeleted(index, to_delete);

        const auto& before = polygon[before_index];
        const auto& after = polygon[after_index];
        const int64_t deviation = getDistFromLine(vertex, before, after);
        if (deviation <= min_resolution) // Deviation so small that it's always desired to remove them.
        {
            return deviation;
        }

        const auto delta_before = before - vertex;
        const auto delta_after = after - vertex;
        if (std::hypot(delta_before.X, delta_before.Y) > max_resolution && std::hypot(delta_after.X, delta_before.Y) > max_resolution)
        {
            return std::numeric_limits<int64_t>::max(); // Long line segments, no need to remove this one.
        }
        return deviation;
    }

    /*!
     * Mark a vertex for removal.
     *
     * This function looks in the vertex and the four edges surrounding it to
     * determine the best way to remove the given vertex. It may choose instead
     * to delete an edge, fusing two vertices together.
     * \tparam Polygonal A polygonal object, which is a list of vertices.
     * \param polygon The polygon to remove a vertex from.
     * \param to_delete The vertices that have been marked for deletion so far.
     * This will be edited in-place.
     * \param vertex The index of the vertex to remove.
     * \param deviation The previously found deviation for this vertex.
     * \param is_closed Whether we're working on a closed polygon or an open
     * polyline.
     */
    void remove(concepts::poly_range auto& polygon, std::vector<bool>& to_delete, const size_t vertex, const int64_t deviation)
    {
        using Polygonal = decltype(polygon);
        constexpr bool is_closed = concepts::is_closed_point_container<Polygonal>;
        if (deviation <= min_resolution)
        {
            // At less than the minimum resolution we're always allowed to delete the vertex.
            // Even if the adjacent line segments are very long.
            to_delete[vertex] = true;
            return;
        }

        const size_t before = previousNotDeleted(vertex, to_delete);
        const size_t after = nextNotDeleted(vertex, to_delete);
        const auto& vertex_position = polygon[vertex];
        const auto& before_position = polygon[before];
        const auto& after_position = polygon[after];
        const auto delta_before = vertex_position - before_position;
        const auto delta_after = vertex_position - after_position;
        const auto length_before = std::hypot(delta_before.X, delta_before.Y);
        const auto length_after = std::hypot(delta_after.X, delta_before.Y);

        if (length_before <= max_resolution && length_after <= max_resolution) // Both adjacent line segments are short.
        {
            // Removing this vertex does little harm. No long lines will be shifted.
            to_delete[vertex] = true;
            return;
        }

        // Otherwise, one edge next to this vertex is longer than max_resolution. The other is shorter.
        // In this case we want to remove the short edge by replacing it with a vertex where the two surrounding edges intersect.
        // Find the two line segments surrounding the short edge here ("before" and "after" edges).
        geometry::Point before_from, before_to, after_from, after_to;
        if (length_before <= length_after) // Before is the shorter line.
        {
            if (! is_closed && before == 0) // No edge before the short edge.
            {
                return; // Edge cannot be deleted without shifting a long edge. Don't remove anything.
            }
            const size_t before_before = previousNotDeleted(before, to_delete);
            before_from = polygon[before_before];
            before_to = polygon[before];
            after_from = polygon[vertex];
            after_to = polygon[after];
        }
        else
        {
            if (! is_closed && after == polygon.size() - 1) // No edge after the short edge.
            {
                return; // Edge cannot be deleted without shifting a long edge. Don't remove anything.
            }
            const size_t after_after = nextNotDeleted(after, to_delete);
            before_from = polygon[before];
            before_to = polygon[vertex];
            after_from = polygon[after];
            after_to = polygon[after_after];
        }
        const auto intersection { lineLineIntersection(before_from, before_to, after_from, after_to) };
        if (! intersection.has_value())
        {
            return;
        }

        const auto intersection_deviation = getDistFromLine(intersection.value(), before_to, after_from);
        if (intersection_deviation <= max_deviation) // Intersection point doesn't deviate too much. Use it!
        {
            to_delete[vertex] = true;
            polygon[length_before <= length_after ? before : after] = intersection.value();
        }
    }

    /*!
     * Helper method to find the index of the next vertex that is not about to
     * get deleted.
     *
     * This method assumes that the polygon is looping. If it is a polyline, the
     * endpoints of the polyline may never be deleted so it should never be an
     * issue.
     * \param index The index of the current vertex.
     * \param to_delete For each vertex, whether it is to be deleted.
     * \return The index of the vertex afterwards.
     */
    static size_t nextNotDeleted(size_t index, const std::vector<bool>& to_delete)
    {
        const size_t size = to_delete.size();
        for (index = (index + 1) % size; to_delete[index]; index = (index + 1) % size)
            ; // Changes the index variable in-place until we found one that is not deleted.
        return index;
    }

    /*!
     * Helper method to find the index of the previous vertex that is not about
     * to get deleted.
     *
     * This method assumes that the polygon is looping. If it is a polyline, the
     * endpoints of the polyline may never be deleted so it should never be an
     * issue.
     * \param index The index of the current vertex.
     * \param to_delete For each vertex, whether it is to be deleted.
     * \return The index of the vertex before it.
     */
    static size_t previousNotDeleted(size_t index, const std::vector<bool>& to_delete)
    {
        const size_t size = to_delete.size();
        for (index = (index + size - 1) % size; to_delete[index]; index = (index + size - 1) % size)
            ; // Changes the index variable in-place until we found one that is not deleted.
        return index;
    }
};

#endif // UTILS_SIMPLIFY_H
