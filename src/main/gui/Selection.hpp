#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace cupuacu::gui
{

    template <typename T> class Selection
    {
    private:
        static constexpr int64_t kUnset =
            std::numeric_limits<int64_t>::max();

        int64_t lowest;
        int64_t highest = kUnset;
        int64_t value1 = kUnset;
        int64_t value2 = kUnset;

        static int64_t toFrameBoundary(const T v)
        {
            return static_cast<int64_t>(std::llround(v));
        }

        bool hasValue1() const
        {
            return value1 != kUnset;
        }

        bool hasValue2() const
        {
            return value2 != kUnset;
        }

        bool hasBothValues() const
        {
            return hasValue1() && hasValue2();
        }

        bool hasAnyValue() const
        {
            return hasValue1() || hasValue2();
        }

        int64_t anchorValue() const
        {
            if (hasValue1())
            {
                return value1;
            }

            if (hasValue2())
            {
                return value2;
            }

            return kUnset;
        }

    public:
        Selection(const T lowestToUse = std::numeric_limits<T>::lowest())
            : lowest(toFrameBoundary(lowestToUse))
        {
        }

        void setHighest(const T highestToUse)
        {
            highest = toFrameBoundary(highestToUse);
        }

        T getStart() const
        {
            if (!hasAnyValue())
            {
                return std::numeric_limits<T>::max();
            }
            return static_cast<T>(
                hasBothValues() ? std::min(value1, value2) : anchorValue());
        }

        T getEnd() const
        {
            if (!hasAnyValue())
            {
                return std::numeric_limits<T>::max();
            }
            return static_cast<T>(
                hasBothValues() ? std::max(value1, value2) : anchorValue());
        }

        void setValue1(const T v)
        {
            value1 = std::clamp(toFrameBoundary(v), lowest, highest);
        }

        void setValue2(const T v)
        {
            value2 = std::clamp(toFrameBoundary(v), lowest, highest);
        }

        int64_t getStartInt() const
        {
            if (!hasAnyValue())
            {
                return kUnset;
            }
            return hasBothValues() ? std::min(value1, value2) : anchorValue();
        }

        int64_t getEndExclusiveInt() const
        {
            if (!hasAnyValue())
            {
                return kUnset;
            }
            return hasBothValues() ? std::max(value1, value2) : anchorValue();
        }

        int64_t getEndInt() const
        {
            if (!hasAnyValue())
            {
                return kUnset;
            }
            if (!hasBothValues())
            {
                return anchorValue();
            }
            return getEndExclusiveInt() - 1;
        }

        int64_t getLengthInt() const
        {
            if (!hasBothValues())
            {
                return 0;
            }
            return getEndExclusiveInt() - getStartInt();
        }

        void reset()
        {
            value1 = kUnset;
            value2 = kUnset;
        }

        T getLength() const
        {
            return static_cast<T>(getLengthInt());
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
            return hasBothValues() && getLengthInt() > 0;
        }

        void printInfo() const
        {
            printf(
                "active: %i, value1: %lld, value2: %lld, startInt: %lld, "
                "endExclusiveInt: %lld, endInt: %lld, lengthInt: %lld\n",
                isActive(), static_cast<long long>(value1),
                static_cast<long long>(value2),
                static_cast<long long>(getStartInt()),
                static_cast<long long>(getEndExclusiveInt()),
                static_cast<long long>(getEndInt()),
                static_cast<long long>(getLengthInt()));
        }
    };
} // namespace cupuacu::gui
