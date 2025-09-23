#pragma once
#include <cmath>
#include <limits>
#include <cstdint>
#include <algorithm>

template <typename T>
class Selection {
private:
    const T lowest;

    T value1;
    T value2;

public:
    Selection(const T lowestToUse = std::numeric_limits<T>::lowest())
        : lowest(lowestToUse),
          value1(std::numeric_limits<T>::max()),
          value2(std::numeric_limits<T>::max())
    {
    }

    T getStart() const
    {
        return (value1 < value2) ? value1 : value2;
    }

    T getEnd() const
    {
        return (value1 < value2) ? value2 : value1;
    }

    void startSelection(const T initialValue)
    {
        value1 = std::max(initialValue, lowest);
        value2 = std::max(initialValue, lowest);
    }

    void setValue1(const T v)
    {
        value1 = std::max(v, lowest);
    }

    void setValue2(const T v)
    {
        value2 = std::max(v, lowest);
    }

    T getStartFloor() const
    {
        return std::floor((value1 < value2) ? value1 : value2);
    }

    T getEndFloor() const
    {
        return std::floor((value1 < value2) ? value2 : value1);
    }

    int64_t getStartFloorInt() const
    {
        return static_cast<int64_t>(std::floor((value1 < value2) ? value1 : value2));
    }

    int64_t getEndFloorInt() const
    {
        return static_cast<int64_t>(std::floor((value1 < value2) ? value2 : value1));
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
               getStartFloor() != getEndFloor();
    }
};
