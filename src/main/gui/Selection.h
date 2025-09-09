#pragma once
#include <cmath>
#include <limits>
#include <cstdint>

template <typename T>
class Selection {
private:
    T value1;
    T value2;

    void orderSelection()
    {
        if (value1 < value2)
        {
            return;
        }

        const auto tmpValue = value2;
        value2 = value1;
        value1 = tmpValue;
    }


public:
    Selection()
        : value1(std::numeric_limits<T>::max()),
          value2(std::numeric_limits<T>::max()) {}

    T getStart() const {
        return (value1 < value2) ? value1 : value2;
    }

    T getEnd() const {
        return (value1 < value2) ? value2 : value1;
    }

    void startSelection(const double initialValue)
    {
        value1 = initialValue;
        value2 = initialValue;
    }

    void endSelection(const double endValue)
    {
        value2 = endValue;
        orderSelection();
        
        if (value1 < 0)
        {
            value1 = 0;
        }
    }

    void setValue1(const double v)
    {
        value1 = v;
    }

    void setValue2(const double v)
    {
        value2 = v;
    }

    void bumpValue2(const double amount)
    {
        value2 = amount;
    }

    T getStartFloor() const {
        return std::floor((value1 < value2) ? value1 : value2);
    }

    T getEndFloor() const {
        return std::floor((value1 < value2) ? value2 : value1);
    }

    int64_t getStartFloorInt() const {
        return static_cast<int64_t>(std::floor((value1 < value2) ? value1 : value2));
    }

    int64_t getEndFloorInt() const {
        return static_cast<int64_t>(std::floor((value1 < value2) ? value2 : value1));
    }

    void reset() {
        value1 = std::numeric_limits<T>::max();
        value2 = std::numeric_limits<T>::max();
    }

    T getLength()
    {
        return getEnd() - getStart();
    }

    bool isActive() const {
        return value1 != std::numeric_limits<T>::max() &&
               value2 != std::numeric_limits<T>::max() &&
               getStartFloor() != getEndFloor();
    }
};
