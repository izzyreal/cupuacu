#pragma once
#include <cmath>
#include <limits>
#include <algorithm>

namespace cupuacu::gui
{

    template <typename T> class Selection
    {
    private:
        const T lowest;

        T highest = std::numeric_limits<T>::max();
        T value1;
        T value2;

    public:
        Selection(const T lowestToUse = std::numeric_limits<T>::lowest())
            : lowest(lowestToUse), value1(std::numeric_limits<T>::max()),
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
        }

        void setValue2(const T v)
        {
            value2 = std::clamp(v, lowest, highest);
        }

        int64_t getStartInt() const
        {
            return static_cast<int64_t>(std::round(getStart()));
        }

        int64_t getEndInt() const
        {
            return static_cast<int64_t>(std::round(getEnd() - 1));
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

        void fixOrder()
        {
            if (value1 > value2)
            {
                std::swap(value1, value2);
            }
        }

        bool isActive() const
        {
            return value1 != std::numeric_limits<T>::max() &&
                   value2 != std::numeric_limits<T>::max() &&
                   getLengthInt() > 0;
        }

        void printInfo()
        {
            printf(
                "active: %i, value1: %d, value2: %d, startInt: %i, endInt: %i, "
                "lengthInt: %i\n",
                isActive(), value1, value2, getStartInt(), getEndInt(),
                getLengthInt());
        }
    };
} // namespace cupuacu::gui
