/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2022, Ben Maxwell <macdue@dueutil.tech>
 * Copyright (c) 2022, Torsten Engelmann <engelTorsten@gmx.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(AK_COMPILER_GCC)
#    pragma GCC optimize("O3")
#endif

#include "FillPathImplementation.h"
#include <AK/Function.h>
#include <AK/NumericLimits.h>
#include <LibGfx/AntiAliasingPainter.h>
#include <LibGfx/Line.h>

namespace Gfx {

// Base algorithm from https://en.wikipedia.org/wiki/Xiaolin_Wu%27s_line_algorithm,
// because there seems to be no other known method for drawing AA'd lines (?)
template<AntiAliasingPainter::AntiAliasPolicy policy>
void AntiAliasingPainter::draw_anti_aliased_line(FloatPoint const& actual_from, FloatPoint const& actual_to, Color color, float thickness, Painter::LineStyle style, Color)
{
    // FIXME: Implement this :P
    VERIFY(style == Painter::LineStyle::Solid);

    auto corrected_thickness = thickness > 1 ? thickness - 1 : thickness;
    auto size = IntSize(corrected_thickness, corrected_thickness);
    auto mapped_from = m_transform.map(actual_from);
    auto mapped_to = m_transform.map(actual_to);
    auto draw_direction = mapped_from - mapped_to;
    auto is_straight_line = draw_direction.x() == 0 || draw_direction.y() == 0;
    auto rotated_rectangle_reference_coords = FloatQuad({ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 });
    float drawing_edge_offset = fabs((corrected_thickness / 2) - fmodf(corrected_thickness, 2.0f));

    auto integer_part = [](float x) { return floorf(x); };
    auto round = [&](float x) { return integer_part(x + 0.5f); };

    if (is_straight_line) {
        // draw top to bottom line in one call
        if (draw_direction.x() == 0)
            m_underlying_painter.fill_rect(
                { mapped_from.x() - drawing_edge_offset,
                    round(min(mapped_from.y(), mapped_to.y())),
                    thickness,
                    round(fabsf(draw_direction.y())) },
                color);
        // draw left to right line in one call
        if (draw_direction.y() == 0) {
            m_underlying_painter.fill_rect(
                { round(min(mapped_from.x(), mapped_to.x())),
                    mapped_from.y() - drawing_edge_offset,
                    round(fabsf(draw_direction.x())),
                    thickness },
                color);
        }
        return;
    }

    rotated_rectangle_reference_coords = build_rotated_rectangle(draw_direction, corrected_thickness);

    auto plot = [&](int x, int y, float c) {
        // ignore rotation if rectangle is fairly small to reduce overhead
        if (is_straight_line || corrected_thickness < 4) {
            m_underlying_painter.fill_rect(IntRect::centered_on({ x, y }, size), color.with_alpha(color.alpha() * c));
        } else if (min(AK::abs(mapped_from.distance_from({ x, y })), AK::abs(mapped_to.distance_from({ x, y }))) >= drawing_edge_offset || AK::abs(mapped_from.distance_from(mapped_to)) < drawing_edge_offset) {
            // don't draw if we are close to the edge but draw if the whole line is shorter than allowed edge distance
            m_rotated_rectangle_path.clear();
            m_rotated_rectangle_path.move_to({ x + rotated_rectangle_reference_coords.p1().x(), y + rotated_rectangle_reference_coords.p1().y() });
            m_rotated_rectangle_path.line_to({ x + rotated_rectangle_reference_coords.p2().x(), y + rotated_rectangle_reference_coords.p2().y() });
            m_rotated_rectangle_path.line_to({ x + rotated_rectangle_reference_coords.p3().x(), y + rotated_rectangle_reference_coords.p3().y() });
            m_rotated_rectangle_path.line_to({ x + rotated_rectangle_reference_coords.p4().x(), y + rotated_rectangle_reference_coords.p4().y() });

            m_rotated_rectangle_path.close();
            m_underlying_painter.fill_path(m_rotated_rectangle_path, color.with_alpha(color.alpha() * c));
        }
    };
    auto fractional_part = [&](float x) { return x - floorf(x); };
    auto one_minus_fractional_part = [&](float x) { return 1.0f - fractional_part(x); };

    auto draw_line = [&](float x0, float y0, float x1, float y1) {
        bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);

        if (steep) {
            swap(x0, y0);
            swap(x1, y1);
        }

        if (x0 > x1) {
            swap(x0, x1);
            swap(y0, y1);
        }

        float dx = x1 - x0;
        float dy = y1 - y0;

        float gradient;
        if (dx == 0.0f)
            gradient = 1.0f;
        else
            gradient = dy / dx;

        // Handle first endpoint.
        int x_end = round(x0);
        int y_end = y0 + gradient * (x_end - x0);
        float x_gap = one_minus_fractional_part(x0 + 0.5f);

        int xpxl1 = x_end; // This will be used in the main loop.
        int ypxl1 = integer_part(y_end);

        if (steep) {
            plot(ypxl1, xpxl1, one_minus_fractional_part(y_end) * x_gap);
            plot(ypxl1 + 1, xpxl1, fractional_part(y_end) * x_gap);
        } else {
            plot(xpxl1, ypxl1, one_minus_fractional_part(y_end) * x_gap);
            plot(xpxl1, ypxl1 + 1, fractional_part(y_end) * x_gap);
        }

        float intery = y_end + gradient; // First y-intersection for the main loop.

        // Handle second endpoint.
        x_end = round(x1);
        y_end = y1 + gradient * (x_end - x1);
        x_gap = fractional_part(x1 + 0.5f);
        int xpxl2 = x_end; // This will be used in the main loop
        int ypxl2 = integer_part(y_end);

        if (steep) {
            plot(ypxl2, xpxl2, one_minus_fractional_part(y_end) * x_gap);
            plot(ypxl2 + 1, xpxl2, fractional_part(y_end) * x_gap);
        } else {
            plot(xpxl2, ypxl2, one_minus_fractional_part(y_end) * x_gap);
            plot(xpxl2, ypxl2 + 1, fractional_part(y_end) * x_gap);
        }

        // Main loop.
        if (steep) {
            for (int x = xpxl1 + 1; x <= xpxl2 - 1; ++x) {
                if constexpr (policy == AntiAliasPolicy::OnlyEnds) {
                    plot(integer_part(intery), x, 1);
                } else {
                    plot(integer_part(intery), x, one_minus_fractional_part(intery));
                }
                plot(integer_part(intery) + 1, x, fractional_part(intery));
                intery += gradient;
            }
        } else {
            for (int x = xpxl1 + 1; x <= xpxl2 - 1; ++x) {
                if constexpr (policy == AntiAliasPolicy::OnlyEnds) {
                    plot(x, integer_part(intery), 1);
                } else {
                    plot(x, integer_part(intery), one_minus_fractional_part(intery));
                }
                plot(x, integer_part(intery) + 1, fractional_part(intery));
                intery += gradient;
            }
        }
    };

    draw_line(mapped_from.x(), mapped_from.y(), mapped_to.x(), mapped_to.y());
}

void AntiAliasingPainter::draw_aliased_line(FloatPoint const& actual_from, FloatPoint const& actual_to, Color color, float thickness, Painter::LineStyle style, Color alternate_color)
{
    draw_anti_aliased_line<AntiAliasPolicy::OnlyEnds>(actual_from, actual_to, color, thickness, style, alternate_color);
}

void AntiAliasingPainter::draw_dotted_line(IntPoint point1, IntPoint point2, Color color, int thickness)
{
    // AA circles don't really work below a radius of 2px.
    if (thickness < 4)
        return m_underlying_painter.draw_line(point1, point2, color, thickness, Painter::LineStyle::Dotted);

    auto draw_spaced_dots = [&](int start, int end, auto to_point) {
        int step = thickness * 2;
        if (start > end)
            swap(start, end);
        int delta = end - start;
        int dots = delta / step;
        if (dots == 0)
            return;
        int fudge_per_dot = 0;
        int extra_fudge = 0;
        if (dots > 3) {
            // Fudge the numbers so the last dot is drawn at the `end' point (otherwise you can get lines cuts short).
            // You need at least a handful of dots to do this.
            int fudge = delta % step;
            fudge_per_dot = fudge / dots;
            extra_fudge = fudge % dots;
        }
        for (int dot = start; dot <= end; dot += (step + fudge_per_dot + (extra_fudge > 0))) {
            fill_circle(to_point(dot), thickness / 2, color);
            --extra_fudge;
        }
    };

    if (point1.y() == point2.y()) {
        draw_spaced_dots(point1.x(), point2.x(), [&](int dot_x) {
            return IntPoint { dot_x, point1.y() };
        });
    } else if (point1.x() == point2.x()) {
        draw_spaced_dots(point1.y(), point2.y(), [&](int dot_y) {
            return IntPoint { point1.x(), dot_y };
        });
    } else {
        TODO();
    }
}

void AntiAliasingPainter::draw_line(FloatPoint const& actual_from, FloatPoint const& actual_to, Color color, float thickness, Painter::LineStyle style, Color alternate_color)
{
    if (style == Painter::LineStyle::Dotted)
        return draw_dotted_line(actual_from.to_rounded<int>(), actual_to.to_rounded<int>(), color, static_cast<int>(round(thickness)));
    draw_anti_aliased_line<AntiAliasPolicy::Full>(actual_from, actual_to, color, thickness, style, alternate_color);
}

void AntiAliasingPainter::fill_path(Path& path, Color color, Painter::WindingRule rule)
{
    Detail::fill_path<Detail::FillPathMode::AllowFloatingPoints>(*this, path, color, rule);
}

void AntiAliasingPainter::stroke_path(Path const& path, Color color, float thickness)
{
    FloatPoint cursor;
    bool previous_was_line = false;
    FloatLine last_line;
    Optional<FloatLine> first_line;

    for (auto& segment : path.segments()) {
        switch (segment.type()) {
        case Segment::Type::Invalid:
            VERIFY_NOT_REACHED();
        case Segment::Type::MoveTo:
            cursor = segment.point();
            break;
        case Segment::Type::LineTo:
            if (!first_line.has_value())
                first_line = FloatLine(cursor, segment.point());

            draw_line(cursor, segment.point(), color, thickness);
            if (previous_was_line) {
                stroke_segment_intersection(cursor, segment.point(), last_line, color, thickness);
            }

            last_line.set_a(cursor);
            last_line.set_b(segment.point());
            cursor = segment.point();
            break;
        case Segment::Type::QuadraticBezierCurveTo: {
            auto& through = static_cast<QuadraticBezierCurveSegment const&>(segment).through();
            draw_quadratic_bezier_curve(through, cursor, segment.point(), color, thickness);
            cursor = segment.point();
            break;
        }
        case Segment::Type::CubicBezierCurveTo: {
            auto& curve = static_cast<CubicBezierCurveSegment const&>(segment);
            auto& through_0 = curve.through_0();
            auto& through_1 = curve.through_1();
            draw_cubic_bezier_curve(through_0, through_1, cursor, segment.point(), color, thickness);
            cursor = segment.point();
            break;
        }
        case Segment::Type::EllipticalArcTo:
            auto& arc = static_cast<EllipticalArcSegment const&>(segment);
            draw_elliptical_arc(cursor, segment.point(), arc.center(), arc.radii(), arc.x_axis_rotation(), arc.theta_1(), arc.theta_delta(), color, thickness);
            cursor = segment.point();
            break;
        }

        previous_was_line = segment.type() == Segment::Type::LineTo;
    }

    // check if the figure was started and closed as line at the same position
    if (previous_was_line && path.segments().size() >= 2 && path.segments().first().point() == cursor && (path.segments().first().type() == Segment::Type::LineTo || (path.segments().first().type() == Segment::Type::MoveTo && path.segments()[1].type() == Segment::Type::LineTo)))
        stroke_segment_intersection(first_line.value().a(), first_line.value().b(), last_line, color, thickness);
}

void AntiAliasingPainter::draw_elliptical_arc(FloatPoint const& p1, FloatPoint const& p2, FloatPoint const& center, FloatPoint const& radii, float x_axis_rotation, float theta_1, float theta_delta, Color color, float thickness, Painter::LineStyle style)
{
    Painter::for_each_line_segment_on_elliptical_arc(p1, p2, center, radii, x_axis_rotation, theta_1, theta_delta, [&](FloatPoint const& fp1, FloatPoint const& fp2) {
        draw_line(fp1, fp2, color, thickness, style);
    });
}

void AntiAliasingPainter::draw_quadratic_bezier_curve(FloatPoint const& control_point, FloatPoint const& p1, FloatPoint const& p2, Color color, float thickness, Painter::LineStyle style)
{
    Painter::for_each_line_segment_on_bezier_curve(control_point, p1, p2, [&](FloatPoint const& fp1, FloatPoint const& fp2) {
        draw_line(fp1, fp2, color, thickness, style);
    });
}

void AntiAliasingPainter::draw_cubic_bezier_curve(FloatPoint const& control_point_0, FloatPoint const& control_point_1, FloatPoint const& p1, FloatPoint const& p2, Color color, float thickness, Painter::LineStyle style)
{
    Painter::for_each_line_segment_on_cubic_bezier_curve(control_point_0, control_point_1, p1, p2, [&](FloatPoint const& fp1, FloatPoint const& fp2) {
        draw_line(fp1, fp2, color, thickness, style);
    });
}

void AntiAliasingPainter::fill_rect(FloatRect const& float_rect, Color color)
{
    // Draw the integer part of the rectangle:
    float right_x = float_rect.x() + float_rect.width();
    float bottom_y = float_rect.y() + float_rect.height();
    int x1 = ceilf(float_rect.x());
    int y1 = ceilf(float_rect.y());
    int x2 = floorf(right_x);
    int y2 = floorf(bottom_y);
    auto solid_rect = Gfx::IntRect::from_two_points({ x1, y1 }, { x2, y2 });
    m_underlying_painter.fill_rect(solid_rect, color);

    if (float_rect == solid_rect)
        return;

    // Draw the rest:
    float left_subpixel = x1 - float_rect.x();
    float top_subpixel = y1 - float_rect.y();
    float right_subpixel = right_x - x2;
    float bottom_subpixel = bottom_y - y2;
    float top_left_subpixel = top_subpixel * left_subpixel;
    float top_right_subpixel = top_subpixel * right_subpixel;
    float bottom_left_subpixel = bottom_subpixel * left_subpixel;
    float bottom_right_subpixel = bottom_subpixel * right_subpixel;

    auto subpixel = [&](float alpha) {
        return color.with_alpha(color.alpha() * alpha);
    };

    auto set_pixel = [&](int x, int y, float alpha) {
        m_underlying_painter.set_pixel(x, y, subpixel(alpha), true);
    };

    auto line_to_rect = [&](int x1, int y1, int x2, int y2) {
        return IntRect::from_two_points({ x1, y1 }, { x2 + 1, y2 + 1 });
    };

    set_pixel(x1 - 1, y1 - 1, top_left_subpixel);
    set_pixel(x2, y1 - 1, top_right_subpixel);
    set_pixel(x2, y2, bottom_right_subpixel);
    set_pixel(x1 - 1, y2, bottom_left_subpixel);
    m_underlying_painter.fill_rect(line_to_rect(x1, y1 - 1, x2 - 1, y1 - 1), subpixel(top_subpixel));
    m_underlying_painter.fill_rect(line_to_rect(x1, y2, x2 - 1, y2), subpixel(bottom_subpixel));
    m_underlying_painter.fill_rect(line_to_rect(x1 - 1, y1, x1 - 1, y2 - 1), subpixel(left_subpixel));
    m_underlying_painter.fill_rect(line_to_rect(x2, y1, x2, y2 - 1), subpixel(right_subpixel));
}

void AntiAliasingPainter::draw_ellipse(IntRect const& a_rect, Color color, int thickness)
{
    // FIXME: Come up with an allocation-free version of this!
    // Using draw_line() for segments of an ellipse was attempted but gave really poor results :^(
    // There probably is a way to adjust the fill of draw_ellipse_part() to do this, but getting it rendering correctly is tricky.
    // The outline of the steps required to paint it efficiently is:
    //     - Paint the outer ellipse without the fill (from the fill() lambda in draw_ellipse_part())
    //     - Paint the inner ellipse, but in the set_pixel() invert the alpha values
    //     - Somehow fill in the gap between the two ellipses (the tricky part to get right)
    //          - Have to avoid overlapping pixels and accidentally painting over some of the edge pixels

    auto color_no_alpha = color;
    color_no_alpha.set_alpha(255);
    auto outline_ellipse_bitmap = ({
        auto bitmap = Bitmap::try_create(BitmapFormat::BGRA8888, a_rect.size());
        if (bitmap.is_error())
            return warnln("Failed to allocate temporary bitmap for antialiased outline ellipse!");
        bitmap.release_value();
    });

    auto outer_rect = a_rect;
    outer_rect.set_location({ 0, 0 });
    auto inner_rect = outer_rect.shrunken(thickness * 2, thickness * 2);
    Painter painter { outline_ellipse_bitmap };
    AntiAliasingPainter aa_painter { painter };
    aa_painter.fill_ellipse(outer_rect, color_no_alpha);
    aa_painter.fill_ellipse(inner_rect, color_no_alpha, BlendMode::AlphaSubtract);
    m_underlying_painter.blit(a_rect.location(), outline_ellipse_bitmap, outline_ellipse_bitmap->rect(), color.alpha() / 255.);
}

void AntiAliasingPainter::fill_circle(IntPoint const& center, int radius, Color color, BlendMode blend_mode)
{
    if (radius <= 0)
        return;
    draw_ellipse_part(center, radius, radius, color, false, {}, blend_mode);
}

void AntiAliasingPainter::fill_ellipse(IntRect const& a_rect, Color color, BlendMode blend_mode)
{
    auto center = a_rect.center();
    auto radius_a = a_rect.width() / 2;
    auto radius_b = a_rect.height() / 2;
    if (radius_a <= 0 || radius_b <= 0)
        return;
    if (radius_a == radius_b)
        return fill_circle(center, radius_a, color, blend_mode);
    auto x_paint_range = draw_ellipse_part(center, radius_a, radius_b, color, false, {}, blend_mode);
    // FIXME: This paints some extra fill pixels that are clipped
    draw_ellipse_part(center, radius_b, radius_a, color, true, x_paint_range, blend_mode);
}

FLATTEN AntiAliasingPainter::Range AntiAliasingPainter::draw_ellipse_part(
    IntPoint center, int radius_a, int radius_b, Color color, bool flip_x_and_y, Optional<Range> x_clip, BlendMode blend_mode)
{
    /*
    Algorithm from: https://cs.uwaterloo.ca/research/tr/1984/CS-84-38.pdf

    This method can draw a whole circle with a whole circle in one call using
    8-way symmetry, or an ellipse in two calls using 4-way symmetry.
    */

    center *= m_underlying_painter.scale();
    radius_a *= m_underlying_painter.scale();
    radius_b *= m_underlying_painter.scale();

    // If this is a ellipse everything can be drawn in one pass with 8 way symmetry
    bool const is_circle = radius_a == radius_b;

    // These happen to be the same here, but are treated separately in the paper:
    // intensity is the fill alpha
    int const intensity = 255;
    // 0 to subpixel_resolution is the range of alpha values for the circle edges
    int const subpixel_resolution = intensity;

    // Current pixel address
    int i = 0;
    int q = radius_b;

    // 1st and 2nd order differences of y
    int delta_y = 0;
    int delta2_y = 0;

    int const a_squared = radius_a * radius_a;
    int const b_squared = radius_b * radius_b;

    // Exact and predicted values of f(i) -- the ellipse equation scaled by subpixel_resolution
    int y = subpixel_resolution * radius_b;
    int y_hat = 0;

    // The value of f(i)*f(i)
    int f_squared = y * y;

    // 1st and 2nd order differences of f(i)*f(i)
    int delta_f_squared = -(static_cast<int64_t>(b_squared) * subpixel_resolution * subpixel_resolution) / a_squared;
    int delta2_f_squared = 2 * delta_f_squared;

    // edge_intersection_area/subpixel_resolution = percentage of pixel intersected by circle
    // (aka the alpha for the pixel)
    int edge_intersection_area = 0;
    int old_area = edge_intersection_area;

    auto predict = [&] {
        delta_y += delta2_y;
        // y_hat is the predicted value of f(i)
        y_hat = y + delta_y;
    };

    auto minimize = [&] {
        // Initialize the minimization
        delta_f_squared += delta2_f_squared;
        f_squared += delta_f_squared;

        int min_squared_error = y_hat * y_hat - f_squared;
        int prediction_overshot = 1;
        y = y_hat;

        // Force error negative
        if (min_squared_error > 0) {
            min_squared_error = -min_squared_error;
            prediction_overshot = -1;
        }

        // Minimize
        int previous_error = min_squared_error;
        while (min_squared_error < 0) {
            y += prediction_overshot;
            previous_error = min_squared_error;
            min_squared_error += y + y - prediction_overshot;
        }

        if (min_squared_error + previous_error > 0)
            y -= prediction_overshot;
    };

    auto correct = [&] {
        int error = y - y_hat;

        // FIXME: The alpha values seem too low, which makes things look
        // overly pointy. This fixes that, though there's probably a better
        // solution to be found. (This issue seems to exist in the base algorithm)
        error /= 4;

        delta2_y += error;
        delta_y += error;
    };

    int min_paint_x = NumericLimits<int>::max();
    int max_paint_x = NumericLimits<int>::min();
    auto pixel = [&](int x, int y, int alpha) {
        if (alpha <= 0 || alpha > 255)
            return;
        if (flip_x_and_y)
            swap(x, y);
        if (x_clip.has_value() && x_clip->contains_inclusive(x))
            return;
        min_paint_x = min(x, min_paint_x);
        max_paint_x = max(x, max_paint_x);
        alpha = (alpha * color.alpha()) / 255;
        if (blend_mode == BlendMode::AlphaSubtract)
            alpha = ~alpha;
        auto pixel_color = color;
        pixel_color.set_alpha(alpha);
        m_underlying_painter.set_pixel(center + IntPoint { x, y }, pixel_color, blend_mode == BlendMode::Normal);
    };

    auto fill = [&](int x, int ymax, int ymin, int alpha) {
        while (ymin <= ymax) {
            pixel(x, ymin, alpha);
            ymin += 1;
        }
    };

    auto symmetric_pixel = [&](int x, int y, int alpha) {
        pixel(x, y, alpha);
        pixel(x, -y - 1, alpha);
        pixel(-x - 1, -y - 1, alpha);
        pixel(-x - 1, y, alpha);
        if (is_circle) {
            pixel(y, x, alpha);
            pixel(y, -x - 1, alpha);
            pixel(-y - 1, -x - 1, alpha);
            pixel(-y - 1, x, alpha);
        }
    };

    // These are calculated incrementally (as it is possibly a tiny bit faster)
    int ib_squared = 0;
    int qa_squared = q * a_squared;

    auto in_symmetric_region = [&] {
        // Main fix two stop cond here
        return is_circle ? i < q : ib_squared < qa_squared;
    };

    // Draws a 8 octants for a circle or 4 quadrants for a (partial) ellipse
    while (in_symmetric_region()) {
        predict();
        minimize();
        correct();
        old_area = edge_intersection_area;
        edge_intersection_area += delta_y;
        if (edge_intersection_area >= 0) {
            // Single pixel on perimeter
            symmetric_pixel(i, q, (edge_intersection_area + old_area) / 2);
            fill(i, q - 1, -q, intensity);
            fill(-i - 1, q - 1, -q, intensity);
        } else {
            // Two pixels on perimeter
            edge_intersection_area += subpixel_resolution;
            symmetric_pixel(i, q, old_area / 2);
            q -= 1;
            qa_squared -= a_squared;
            fill(i, q - 1, -q, intensity);
            fill(-i - 1, q - 1, -q, intensity);
            if (!is_circle || in_symmetric_region()) {
                symmetric_pixel(i, q, (edge_intersection_area + subpixel_resolution) / 2);
                if (is_circle) {
                    fill(q, i - 1, -i, intensity);
                    fill(-q - 1, i - 1, -i, intensity);
                }
            } else {
                edge_intersection_area += subpixel_resolution;
            }
        }
        i += 1;
        ib_squared += b_squared;
    }

    if (is_circle) {
        int alpha = edge_intersection_area / 2;
        pixel(q, q, alpha);
        pixel(-q - 1, q, alpha);
        pixel(-q - 1, -q - 1, alpha);
        pixel(q, -q - 1, alpha);
    }

    return Range { min_paint_x, max_paint_x };
}

void AntiAliasingPainter::fill_rect_with_rounded_corners(IntRect const& a_rect, Color color, int radius)
{
    fill_rect_with_rounded_corners(a_rect, color, radius, radius, radius, radius);
}

void AntiAliasingPainter::fill_rect_with_rounded_corners(IntRect const& a_rect, Color color, int top_left_radius, int top_right_radius, int bottom_right_radius, int bottom_left_radius)
{
    fill_rect_with_rounded_corners(a_rect, color,
        { top_left_radius, top_left_radius },
        { top_right_radius, top_right_radius },
        { bottom_right_radius, bottom_right_radius },
        { bottom_left_radius, bottom_left_radius });
}

void AntiAliasingPainter::fill_rect_with_rounded_corners(IntRect const& a_rect, Color color, CornerRadius top_left, CornerRadius top_right, CornerRadius bottom_right, CornerRadius bottom_left, BlendMode blend_mode)
{
    if (!top_left && !top_right && !bottom_right && !bottom_left) {
        if (blend_mode == BlendMode::Normal)
            return m_underlying_painter.fill_rect(a_rect, color);
        else if (blend_mode == BlendMode::AlphaSubtract)
            return m_underlying_painter.clear_rect(a_rect, Color());
    }

    if (color.alpha() == 0)
        return;

    IntPoint top_left_corner {
        a_rect.x() + top_left.horizontal_radius,
        a_rect.y() + top_left.vertical_radius,
    };
    IntPoint top_right_corner {
        a_rect.x() + a_rect.width() - top_right.horizontal_radius,
        a_rect.y() + top_right.vertical_radius,
    };
    IntPoint bottom_left_corner {
        a_rect.x() + bottom_left.horizontal_radius,
        a_rect.y() + a_rect.height() - bottom_left.vertical_radius
    };
    IntPoint bottom_right_corner {
        a_rect.x() + a_rect.width() - bottom_right.horizontal_radius,
        a_rect.y() + a_rect.height() - bottom_right.vertical_radius
    };

    // All corners are centered at the same point, so this can be painted as a single ellipse.
    if (top_left_corner == top_right_corner && top_right_corner == bottom_left_corner && bottom_left_corner == bottom_right_corner)
        return fill_ellipse(a_rect, color, blend_mode);

    IntRect top_rect {
        a_rect.x() + top_left.horizontal_radius,
        a_rect.y(),
        a_rect.width() - top_left.horizontal_radius - top_right.horizontal_radius,
        top_left.vertical_radius
    };
    IntRect right_rect {
        a_rect.x() + a_rect.width() - top_right.horizontal_radius,
        a_rect.y() + top_right.vertical_radius,
        top_right.horizontal_radius,
        a_rect.height() - top_right.vertical_radius - bottom_right.vertical_radius
    };
    IntRect bottom_rect {
        a_rect.x() + bottom_left.horizontal_radius,
        a_rect.y() + a_rect.height() - bottom_right.vertical_radius,
        a_rect.width() - bottom_left.horizontal_radius - bottom_right.horizontal_radius,
        bottom_right.vertical_radius
    };
    IntRect left_rect {
        a_rect.x(),
        a_rect.y() + top_left.vertical_radius,
        bottom_left.horizontal_radius,
        a_rect.height() - top_left.vertical_radius - bottom_left.vertical_radius
    };

    IntRect inner = {
        left_rect.x() + left_rect.width(),
        left_rect.y(),
        a_rect.width() - left_rect.width() - right_rect.width(),
        a_rect.height() - top_rect.height() - bottom_rect.height()
    };

    if (blend_mode == BlendMode::Normal) {
        m_underlying_painter.fill_rect(top_rect, color);
        m_underlying_painter.fill_rect(right_rect, color);
        m_underlying_painter.fill_rect(bottom_rect, color);
        m_underlying_painter.fill_rect(left_rect, color);
        m_underlying_painter.fill_rect(inner, color);
    } else if (blend_mode == BlendMode::AlphaSubtract) {
        m_underlying_painter.clear_rect(top_rect, Color());
        m_underlying_painter.clear_rect(right_rect, Color());
        m_underlying_painter.clear_rect(bottom_rect, Color());
        m_underlying_painter.clear_rect(left_rect, Color());
        m_underlying_painter.clear_rect(inner, Color());
    }

    auto fill_corner = [&](auto const& ellipse_center, auto const& corner_point, CornerRadius const& corner) {
        PainterStateSaver save { m_underlying_painter };
        m_underlying_painter.add_clip_rect(IntRect::from_two_points(ellipse_center, corner_point));
        fill_ellipse(IntRect::centered_at(ellipse_center, { corner.horizontal_radius * 2, corner.vertical_radius * 2 }), color, blend_mode);
    };

    auto bounding_rect = a_rect.inflated(0, 1, 1, 0);
    if (top_left)
        fill_corner(top_left_corner, bounding_rect.top_left(), top_left);
    if (top_right)
        fill_corner(top_right_corner, bounding_rect.top_right(), top_right);
    if (bottom_left)
        fill_corner(bottom_left_corner, bounding_rect.bottom_left(), bottom_left);
    if (bottom_right)
        fill_corner(bottom_right_corner, bounding_rect.bottom_right(), bottom_right);
}

void AntiAliasingPainter::stroke_segment_intersection(FloatPoint const& current_line_a, FloatPoint const& current_line_b, FloatLine const& previous_line, Color color, float thickness)
{
    // starting point of the current line is where the last line ended... this is an intersection
    auto mapped_intersection = m_transform.map(current_line_a);
    auto mapped_current_line_b = m_transform.map(current_line_b);
    auto mapped_previous_line_b = m_transform.map(previous_line.a());

    // if both are straight lines we can simply draw a rectangle at the intersection
    if ((current_line_a.x() == current_line_b.x() || current_line_a.y() == current_line_b.y()) && (previous_line.a().x() == previous_line.b().x() || previous_line.a().y() == previous_line.b().y())) {

        // adjust coordinates to handle rounding offsets
        auto intersection_rect = IntSize(thickness, thickness);
        float drawing_edge_offset = fmodf(thickness, 2.0f) < 0.5f && thickness > 3 ? 1 : 0;
        auto integer_part = [](float x) { return floorf(x); };
        auto round = [&](float x) { return integer_part(x + 0.5f); };

        if (thickness == 1)
            drawing_edge_offset = -1;
        if (current_line_a.x() == current_line_b.x() && previous_line.a().x() == previous_line.b().x()) {
            intersection_rect.set_height(1);
        }
        if (current_line_a.y() == current_line_b.y() && previous_line.a().y() == previous_line.b().y()) {
            intersection_rect.set_width(1);
            drawing_edge_offset = thickness == 1 ? -1 : 0;
            mapped_intersection.set_x(mapped_intersection.x() - 1 + (thickness == 1 ? 1 : 0));
            mapped_intersection.set_y(mapped_intersection.y() + (thickness > 3 && fmodf(thickness, 2.0f) < 0.5f ? 1 : 0));
        }

        m_underlying_painter.fill_rect(IntRect::centered_on({ round(mapped_intersection.x()) + drawing_edge_offset, round(mapped_intersection.y()) + drawing_edge_offset }, intersection_rect), color);
        return;
    }

    float scale_to_move_current = (thickness / 2) / mapped_intersection.distance_from(mapped_current_line_b);
    float scale_to_move_previous = (thickness / 2) / mapped_intersection.distance_from(mapped_previous_line_b);

    // move the point on the line by half of the thickness
    double offset_current_edge_x = scale_to_move_current * (mapped_current_line_b.x() - mapped_intersection.x());
    double offset_current_edge_y = scale_to_move_current * (mapped_current_line_b.y() - mapped_intersection.y());
    double offset_prev_edge_x = scale_to_move_previous * (mapped_previous_line_b.x() - mapped_intersection.x());
    double offset_prev_edge_y = scale_to_move_previous * (mapped_previous_line_b.y() - mapped_intersection.y());

    // rotate the point by 90 and 270 degrees to get the points for both edges
    double rad_90deg = 0.5 * M_PI;
    FloatPoint current_rotated_90deg = { (offset_current_edge_x * cos(rad_90deg) - offset_current_edge_y * sin(rad_90deg)), (offset_current_edge_x * sin(rad_90deg) + offset_current_edge_y * cos(rad_90deg)) };
    FloatPoint current_rotated_270deg = mapped_intersection - current_rotated_90deg;
    FloatPoint previous_rotated_90deg = { (offset_prev_edge_x * cos(rad_90deg) - offset_prev_edge_y * sin(rad_90deg)), (offset_prev_edge_x * sin(rad_90deg) + offset_prev_edge_y * cos(rad_90deg)) };
    FloatPoint previous_rotated_270deg = mapped_intersection - previous_rotated_90deg;

    // translate coordinates to the intersection point
    current_rotated_90deg += mapped_intersection;
    previous_rotated_90deg += mapped_intersection;

    FloatLine outer_line_current_90 = FloatLine({ current_rotated_90deg, mapped_current_line_b - static_cast<FloatPoint>(mapped_intersection - current_rotated_90deg) });
    FloatLine outer_line_current_270 = FloatLine({ current_rotated_270deg, mapped_current_line_b - static_cast<FloatPoint>(mapped_intersection - current_rotated_270deg) });
    FloatLine outer_line_prev_270 = FloatLine({ previous_rotated_270deg, mapped_previous_line_b - static_cast<FloatPoint>(mapped_intersection - previous_rotated_270deg) });
    FloatLine outer_line_prev_90 = FloatLine({ previous_rotated_90deg, mapped_previous_line_b - static_cast<FloatPoint>(mapped_intersection - previous_rotated_90deg) });

    Optional<FloatPoint> edge_spike_90 = outer_line_current_90.intersected(outer_line_prev_270);
    Optional<FloatPoint> edge_spike_270;

    if (edge_spike_90.has_value()) {
        edge_spike_270 = mapped_intersection + (mapped_intersection - edge_spike_90.value());
    } else {
        edge_spike_270 = outer_line_current_270.intersected(outer_line_prev_90);
        if (edge_spike_270.has_value()) {
            edge_spike_90 = mapped_intersection + (mapped_intersection - edge_spike_270.value());
        }
    }

    m_intersection_edge_path.clear();
    m_intersection_edge_path.move_to(current_rotated_90deg);
    if (edge_spike_90.has_value())
        m_intersection_edge_path.line_to(edge_spike_90.value());
    m_intersection_edge_path.line_to(previous_rotated_270deg);

    m_intersection_edge_path.line_to(current_rotated_270deg);
    if (edge_spike_270.has_value())
        m_intersection_edge_path.line_to(edge_spike_270.value());
    m_intersection_edge_path.line_to(previous_rotated_90deg);
    m_intersection_edge_path.close();

    m_underlying_painter.fill_path(m_intersection_edge_path, color);
}

// rotates a rectangle around 0,0
FloatQuad AntiAliasingPainter::build_rotated_rectangle(FloatPoint const& direction, float width)
{
    double half_size = width / 2;
    double radian = atan2(direction.y(), direction.x());
    if (radian < 0) {
        radian += (2 * M_PI);
    }
    // rotated by: (xcosθ−ysinθ ,xsinθ+ycosθ)
    // p1     p2
    //
    //   x,y
    //
    // p4     p3
    double cos_radian = cos(radian);
    double sin_radian = sin(radian);

    // FIXME: Performing the rotation with AffineTransform::rotate_quad seems to generate more glitches at the edges than rotating manually
    return FloatQuad(
        { ((-half_size * cos_radian) - (-half_size * sin_radian)), ((-half_size * sin_radian) + (-half_size * cos_radian)) },
        { ((half_size * cos_radian) - (-half_size * sin_radian)), ((half_size * sin_radian) + (-half_size * cos_radian)) },
        { ((half_size * cos_radian)) - (half_size * sin_radian), ((half_size * sin_radian) + (half_size * cos_radian)) },
        { ((-half_size * cos_radian) - (half_size * sin_radian)), ((-half_size * sin_radian) + (half_size * cos_radian)) });
}

}
