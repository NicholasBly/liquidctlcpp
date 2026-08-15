// LiquidCam - FanCurve.h
// Header-only, allocation-free fan curve. Evaluated once per poll tick on the
// worker thread, so it stays a fixed-size array and a linear scan.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace lc {

struct FanCurve {
    static constexpr int kMaxPoints = 8;

    struct Point {
        int16_t x = 0;   // temperature in C, or load in %
        int16_t y = 0;   // duty in %
    };

    std::array<Point, kMaxPoints> points{};
    uint8_t count = 0;

    void clear() { count = 0; }

    void add(int x, int y)
    {
        if (count >= kMaxPoints)
            return;
        points[count].x = static_cast<int16_t>(std::clamp(x, 0, 100));
        points[count].y = static_cast<int16_t>(std::clamp(y, 0, 100));
        ++count;
    }

    void sort()
    {
        std::sort(points.begin(), points.begin() + count,
                  [](const Point& a, const Point& b) { return a.x < b.x; });
    }

    // Linear interpolation, flat outside the end points.
    int eval(float x) const
    {
        if (count == 0)
            return 50;
        if (x <= points[0].x)
            return points[0].y;
        for (uint8_t i = 1; i < count; ++i) {
            if (x <= points[i].x) {
                const float x0 = points[i - 1].x, x1 = points[i].x;
                const float y0 = points[i - 1].y, y1 = points[i].y;
                const float t  = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.f;
                return static_cast<int>(y0 + t * (y1 - y0) + 0.5f);
            }
        }
        return points[count - 1].y;
    }

    static FanCurve silent()
    {
        FanCurve c;
        c.add(30, 20); c.add(45, 25); c.add(60, 40); c.add(70, 60); c.add(80, 100);
        return c;
    }

    static FanCurve performance()
    {
        FanCurve c;
        c.add(30, 40); c.add(45, 55); c.add(55, 75); c.add(65, 90); c.add(75, 100);
        return c;
    }

    static FanCurve defaultCustom()
    {
        FanCurve c;
        c.add(30, 30); c.add(50, 45); c.add(65, 70); c.add(80, 100);
        return c;
    }
};

} // namespace lc
