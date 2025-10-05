#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <mutex>

namespace cupuacu::gui {

struct Peak {
    float min;
    float max;
};

class WaveformCache {
public:
    static void build(const float* samples, int64_t numSamples, int levels = 8)
    {
        std::scoped_lock lock(mutex());
        auto& data = cache();
        data.clear();
        data.resize(levels);

        // Level 0 (raw samples)
        data[0].resize(numSamples);
        for (int64_t i = 0; i < numSamples; ++i)
        {
            float s = samples[i];
            data[0][i] = { s, s };
        }

        // Build each subsequent level using ceil division to preserve tails
        for (int level = 1; level < levels; ++level)
{
    auto& prev = data[level - 1];
    auto& curr = data[level];

    const int64_t blockSize = 2;
    const int64_t prevSize = static_cast<int64_t>(prev.size());
    const int64_t newSize = (prevSize + blockSize - 1) / blockSize;
    curr.resize(newSize);

    for (int64_t i = 0; i < newSize; ++i)
    {
        const int64_t start = i * blockSize;
        const int64_t end = std::min(start + blockSize, prevSize);

        float minv = prev[start].min;
        float maxv = prev[start].max;
        for (int64_t j = start + 1; j < end; ++j)
        {
            minv = std::min(minv, prev[j].min);
            maxv = std::max(maxv, prev[j].max);
        }
        curr[i] = { minv, maxv };
    }

    if (newSize <= 1)
        break;
}
    }

    static const std::vector<Peak>& getLevel(double samplesPerPixel)
    {
        std::scoped_lock lock(mutex());
        auto& data = cache();
        if (data.empty()) return empty();

        int level = static_cast<int>(std::floor(std::log2(std::max(1.0, samplesPerPixel))));
        level = std::clamp(level, 0, static_cast<int>(data.size() - 1));
        return data[level];
    }

    static void clear()
    {
        std::scoped_lock lock(mutex());
        cache().clear();
    }

private:
    static std::vector<std::vector<Peak>>& cache()
    {
        static std::vector<std::vector<Peak>> instance;
        return instance;
    }

    static std::vector<Peak>& empty()
    {
        static std::vector<Peak> emptyVec;
        return emptyVec;
    }

    static std::mutex& mutex()
    {
        static std::mutex m;
        return m;
    }
};

}

