#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include <vector>
#include <cmath>
#include <algorithm>

static std::vector<float> splineInterpolateNonUniform(
    const std::vector<double> &x,  // sample positions (strictly increasing)
    const std::vector<double> &y,  // sample values at those positions
    const std::vector<double> &xq) // query x positions to evaluate spline at
{
    int n = static_cast<int>(x.size()) - 1;
    if (n < 1 || xq.empty())
    {
        return {};
    }

    std::vector<double> h(n);
    for (int i = 0; i < n; ++i)
    {
        h[i] = x[i + 1] - x[i];
    }

    // Build tridiagonal system for natural spline second derivatives
    std::vector<double> alpha(n);
    for (int i = 1; i < n; ++i)
    {
        alpha[i] = (3.0 / h[i]) * (y[i + 1] - y[i]) -
                   (3.0 / h[i - 1]) * (y[i] - y[i - 1]);
    }

    std::vector<double> l(n + 1), mu(n + 1), z(n + 1);
    l[0] = 1.0;
    mu[0] = z[0] = 0.0;

    for (int i = 1; i < n; ++i)
    {
        l[i] = 2.0 * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }

    l[n] = 1.0;
    z[n] = 0.0;

    std::vector<double> c(n + 1), b(n), d(n);
    c[n] = 0.0;

    for (int j = n - 1; j >= 0; --j)
    {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (y[j + 1] - y[j]) / h[j] - h[j] * (c[j + 1] + 2.0 * c[j]) / 3.0;
        d[j] = (c[j + 1] - c[j]) / (3.0 * h[j]);
    }

    // Evaluate spline at query points
    std::vector<float> result;
    result.reserve(xq.size());

    for (double xi : xq)
    {
        // Find the right interval i so that x[i] <= xi <= x[i+1]
        int i = n - 1; // fallback to last interval
        if (xi <= x[0])
        {
            i = 0;
        }
        else if (xi >= x[n])
        {
            i = n - 1;
        }
        else
        {
            // Binary search for interval:
            int low = 0, high = n;
            while (low <= high)
            {
                int mid = (low + high) / 2;
                if (x[mid] <= xi && xi <= x[mid + 1])
                {
                    i = mid;
                    break;
                }
                else if (xi < x[mid])
                {
                    high = mid - 1;
                }
                else
                {
                    low = mid + 1;
                }
            }
        }

        double dx = xi - x[i];
        double val = y[i] + b[i] * dx + c[i] * dx * dx + d[i] * dx * dx * dx;
        val = std::clamp(val, -32768.0, 32767.0);
        result.push_back(static_cast<float>(val));
    }

    return result;
}

static std::vector<float>
splineInterpolate(const std::vector<int16_t>::const_iterator begin,
                  const std::vector<int16_t>::const_iterator end,
                  int newNumberOfDataPoints)
{
    int n =
        static_cast<int>(std::distance(begin, end)) - 1; // number of intervals
    if (n < 1 || newNumberOfDataPoints < 2)
    {
        return {};
    }

    // Copy input samples to double array for easier math
    std::vector<double> y(n + 1);
    for (int i = 0; i <= n; ++i)
    {
        y[i] = static_cast<double>(*(begin + i));
    }

    // Assume uniform spacing of x = 0,1,...,n
    // Compute the spline coefficients by solving tridiagonal system for second
    // derivatives (M)
    std::vector<double> a(n + 1), b(n + 1), c(n + 1), d(n + 1);
    std::vector<double> h(n, 1.0); // uniform spacing h_i = x_i+1 - x_i = 1

    // Step 1: Setup the tridiagonal system
    std::vector<double> alpha(n);
    for (int i = 1; i < n; ++i)
    {
        alpha[i] = 3.0 * (y[i + 1] - 2.0 * y[i] + y[i - 1]);
    }

    // Step 2: Solve the system
    std::vector<double> l(n + 1), mu(n + 1), z(n + 1);
    l[0] = 1.0;
    mu[0] = z[0] = 0.0;

    for (int i = 1; i < n; ++i)
    {
        l[i] = 4.0 - mu[i - 1];
        mu[i] = 1.0 / l[i];
        z[i] = (alpha[i] - z[i - 1]) / l[i];
    }

    l[n] = 1.0;
    z[n] = 0.0;
    c[n] = 0.0;

    for (int j = n - 1; j >= 0; --j)
    {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (y[j + 1] - y[j]) - (c[j + 1] + 2.0 * c[j]) / 3.0;
        d[j] = (c[j + 1] - c[j]) / 3.0;
        a[j] = y[j];
    }

    std::vector<float> result(newNumberOfDataPoints);
    double scale = static_cast<double>(n) / (newNumberOfDataPoints - 1);

    for (int i = 0; i < newNumberOfDataPoints; ++i)
    {
        double x = i * scale;
        int interval = std::min(static_cast<int>(std::floor(x)), n - 1);
        double t = x - interval;

        double val = a[interval] + b[interval] * t + c[interval] * t * t +
                     d[interval] * t * t * t;

        val = std::clamp(val, -32768.0, 32767.0);

        result[i] = static_cast<float>(val);
    }

    return result;
}

static float cubicInterpolate(float p0, float p1, float p2, float p3, float t)
{
    float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    float a1 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    float a2 = -0.5f * p0 + 0.5f * p2;
    float a3 = p1;

    return ((a0 * t + a1) * t + a2) * t + a3;
}

static std::vector<float>
smoothenCubic(std::vector<int16_t>::const_iterator begin,
              std::vector<int16_t>::const_iterator end,
              int newNumberOfDataPoints)
{
    const int sourceSize = static_cast<int>(std::distance(begin, end));

    if (sourceSize < 4 || newNumberOfDataPoints < 1)
    {
        return {};
    }

    std::vector<float> smoothed(newNumberOfDataPoints);
    const float scale =
        static_cast<float>(sourceSize - 1) / (newNumberOfDataPoints - 1);

    for (int i = 0; i < newNumberOfDataPoints; ++i)
    {
        float pos = i * scale;
        int idx = static_cast<int>(std::floor(pos));
        float t = pos - idx;

        int idx0 = std::clamp(idx - 1, 0, sourceSize - 1);
        int idx1 = std::clamp(idx, 0, sourceSize - 1);
        int idx2 = std::clamp(idx + 1, 0, sourceSize - 1);
        int idx3 = std::clamp(idx + 2, 0, sourceSize - 1);

        float y = cubicInterpolate(static_cast<float>(*(begin + idx0)),
                                   static_cast<float>(*(begin + idx1)),
                                   static_cast<float>(*(begin + idx2)),
                                   static_cast<float>(*(begin + idx3)), t);

        smoothed[i] = std::clamp(y, -32768.0f, 32767.0f);
    }

    return smoothed;
}
