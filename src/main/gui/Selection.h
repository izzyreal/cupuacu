#pragma once
#include <cmath>
#include <limits>
#include <cstdint>
#include <algorithm>

template <typename T>
class Selection {
private:
    const T lowest;

    T highest = std::numeric_limits<T>::max();
    T value1;
    T value2;

public:
    Selection(const T lowestToUse = std::numeric_limits<T>::lowest())
        : lowest(lowestToUse),
          value1(std::numeric_limits<T>::max()),
          value2(std::numeric_limits<T>::max())
    {
    }

    void setHighest(const T highestToUse)
    {
        highest = highestToUse;
    }

    T getStart() const
    {
        return (value1 < value2) ? value1 : value2;
    }

    T getEnd() const
    {
        return (value1 < value2) ? value2 : value1;
    }

    void setValue1(const T v)
    {
        value1 = std::clamp(std::round(v), lowest, highest);
        printf("Setting value1 at %f\n", value1);
    }

    void setValue2(const T v)
    {
        value2 = std::clamp(v, lowest, highest);
        printf("Setting value2 at %f\n", value2);
    }

    int64_t getStartInt() const
    {
        return static_cast<int64_t>(std::round(value2 < value1 ? value2 : value1));
    }

    int64_t getEndInt() const
    {
        if (value2 < value1)
        {
            return static_cast<int64_t>(std::round(value1 - 1));
        }

        return static_cast<int64_t>(std::round(value2 - 1));
    }

    int64_t getLengthInt() const
    {
        return (getEndInt() - getStartInt()) + 1;
    }

    void reset()
    {
        value1 = std::numeric_limits<T>::max();
        value2 = std::numeric_limits<T>::max();
    }

    T getLength() const
    {
        return getEnd() - getStart();
    }

    bool isActive() const
    {
        return value1 != std::numeric_limits<T>::max() &&
               value2 != std::numeric_limits<T>::max() &&
               getLengthInt() > 0;
    }
};
