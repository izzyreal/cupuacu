#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

float cubicInterpolate(float p0, float p1, float p2, float p3, float t)
{
    float a0 = -0.5f*p0 + 1.5f*p1 - 1.5f*p2 + 0.5f*p3;
    float a1 =        p0 - 2.5f*p1 + 2.0f*p2 - 0.5f*p3;
    float a2 = -0.5f*p0 + 0.5f*p2;
    float a3 =        p1;

    return ((a0 * t + a1) * t + a2) * t + a3;
}

// Quadratic interpolation helper: Lagrange form
float interpolate(float x, float x0, float x1, float x2, float y0, float y1, float y2)
{
    float L0 = ((x - x1) * (x - x2)) / ((x0 - x1) * (x0 - x2));
    float L1 = ((x - x0) * (x - x2)) / ((x1 - x0) * (x1 - x2));
    float L2 = ((x - x0) * (x - x1)) / ((x2 - x0) * (x2 - x1));
    return y0 * L0 + y1 * L1 + y2 * L2;
}

std::vector<float> smoothen(const std::vector<int16_t>& source, const int newNumberOfDataPoints)
{
    if (source.size() < 3 || newNumberOfDataPoints < 1)
        return {};

    std::vector<float> smoothed(newNumberOfDataPoints);
    const float scale = static_cast<float>(source.size() - 1) / (newNumberOfDataPoints - 1);

    for (int i = 0; i < newNumberOfDataPoints; ++i)
    {
        float pos = i * scale;
        int idx1 = static_cast<int>(std::floor(pos));

        int idx0 = std::clamp(idx1 - 1, 0, static_cast<int>(source.size()) - 1);
        int idx2 = std::clamp(idx1 + 1, 0, static_cast<int>(source.size()) - 1);

        if (idx0 == idx1) idx0 = std::max(0, idx1 - 2);
        if (idx2 == idx1) idx2 = std::min(static_cast<int>(source.size()) - 1, idx1 + 2);

        if (idx0 == idx1 || idx1 == idx2 || idx0 == idx2) {
            smoothed[i] = static_cast<float>(source[idx1]);
            continue;
        }

        float y = interpolate(
            pos,
            static_cast<float>(idx0),
            static_cast<float>(idx1),
            static_cast<float>(idx2),
            static_cast<float>(source[idx0]),
            static_cast<float>(source[idx1]),
            static_cast<float>(source[idx2])
        );

        smoothed[i] = std::clamp(y, -32768.0f, 32767.0f);
    }

    return smoothed;
}


static std::vector<float> smoothenCubic(std::vector<int16_t>::const_iterator begin,
                                 std::vector<int16_t>::const_iterator end,
                                 int newNumberOfDataPoints)
{
    const int sourceSize = static_cast<int>(std::distance(begin, end));

    if (sourceSize < 4 || newNumberOfDataPoints < 1)
    {
        return {};
    }

    std::vector<float> smoothed(newNumberOfDataPoints);
    const float scale = static_cast<float>(sourceSize - 1) / (newNumberOfDataPoints - 1);

    for (int i = 0; i < newNumberOfDataPoints; ++i)
    {
        float pos = i * scale;
        int idx = static_cast<int>(std::floor(pos));
        float t = pos - idx;

        int idx0 = std::clamp(idx - 1, 0, sourceSize - 1);
        int idx1 = std::clamp(idx    , 0, sourceSize - 1);
        int idx2 = std::clamp(idx + 1, 0, sourceSize - 1);
        int idx3 = std::clamp(idx + 2, 0, sourceSize - 1);

        float y = cubicInterpolate(
            static_cast<float>(*(begin + idx0)),
            static_cast<float>(*(begin + idx1)),
            static_cast<float>(*(begin + idx2)),
            static_cast<float>(*(begin + idx3)),
            t
        );

        smoothed[i] = std::clamp(y, -32768.0f, 32767.0f);
    }

    return smoothed;
}

