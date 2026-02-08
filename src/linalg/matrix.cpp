#include "linalg/matrix.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace telemetry::linalg {

Matrix::Matrix(size_t r, size_t c) : rows(r), cols(c), data(r * c, 0.0) {}

auto Matrix::operator()(size_t r, size_t c) -> double& {
    return data[r * cols + c];
}

auto Matrix::operator()(size_t r, size_t c) const -> double {
    return data[r * cols + c];
}

auto identity(size_t n) -> Matrix {
    Matrix m(n, n);
    for (size_t i = 0; i < n; ++i) {
        m(i, i) = 1.0;
    }
    return m;
}

auto transpose(const Matrix& m) -> Matrix {
    Matrix t(m.cols, m.rows);
    for (size_t r = 0; r < m.rows; ++r) {
        for (size_t c = 0; c < m.cols; ++c) {
            t(c, r) = m(r, c);
        }
    }
    return t;
}

auto matmul(const Matrix& a, const Matrix& b) -> Matrix {
    if (a.cols != b.rows) {
        throw std::runtime_error("matmul dimension mismatch");
    }
    Matrix out(a.rows, b.cols);
    for (size_t i = 0; i < a.rows; ++i) {
        for (size_t k = 0; k < a.cols; ++k) {
            double av = a(i, k);
            for (size_t j = 0; j < b.cols; ++j) {
                out(i, j) += av * b(k, j);
            }
        }
    }
    return out;
}

auto matvec(const Matrix& a, const Vector& x) -> Vector {
    if (a.cols != x.size()) {
        throw std::runtime_error("matvec dimension mismatch");
    }
    Vector out(a.rows, 0.0);
    for (size_t i = 0; i < a.rows; ++i) {
        double sum = 0.0;
        for (size_t j = 0; j < a.cols; ++j) {
            sum += a(i, j) * x[j];
        }
        out[i] = sum;
    }
    return out;
}

auto dot(const Vector& a, const Vector& b) -> double {
    if (a.size() != b.size()) {
        throw std::runtime_error("dot dimension mismatch");
    }
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

auto l2_norm(const Vector& v) -> double {
    return std::sqrt(dot(v, v));
}

auto argsort_desc(const Vector& v) -> std::vector<size_t> {
    std::vector<size_t> idx(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        idx[i] = i;
    }
    std::sort(idx.begin(), idx.end(), [&v](size_t a, size_t b) {
        if (v[a] == v[b]) {
            return a < b;
        }
        return v[a] > v[b];
    });
    return idx;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto max_offdiag(const Matrix& a, size_t& p, size_t& q) -> double {
    double max_val = 0.0;
    p = 0;
    q = 0;
    for (size_t i = 0; i < a.rows; ++i) {
        for (size_t j = i + 1; j < a.cols; ++j) {
            double val = std::abs(a(i, j));
            if (val > max_val) {
                max_val = val;
                p = i;
                q = j;
            }
        }
    }
    return max_val;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto eigen_sym_jacobi(const Matrix& a, int max_iter, double eps) -> EigenSymResult {
    if (a.rows != a.cols) {
        throw std::runtime_error("eigen_sym_jacobi requires square matrix");
    }
    size_t n = a.rows;
    Matrix v = identity(n);
    Matrix d = a;

    for (int iter = 0; iter < max_iter; ++iter) {
        size_t p = 0;
        size_t q = 0;
        double off = max_offdiag(d, p, q);
        if (off < eps) {
            break;
        }

        double app = d(p, p);
        double aqq = d(q, q);
        double apq = d(p, q);

        double phi = 0.5 * std::atan2(2.0 * apq, (aqq - app));
        double c = std::cos(phi);
        double s = std::sin(phi);

        for (size_t k = 0; k < n; ++k) {
            double dpk = d(p, k);
            double dqk = d(q, k);
            d(p, k) = c * dpk - s * dqk;
            d(q, k) = s * dpk + c * dqk;
        }
        for (size_t k = 0; k < n; ++k) {
            double dkp = d(k, p);
            double dkq = d(k, q);
            d(k, p) = c * dkp - s * dkq;
            d(k, q) = s * dkp + c * dkq;
        }

        d(p, p) = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        d(q, q) = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        d(p, q) = 0.0;
        d(q, p) = 0.0;

        for (size_t k = 0; k < n; ++k) {
            double vkp = v(k, p);
            double vkq = v(k, q);
            v(k, p) = c * vkp - s * vkq;
            v(k, q) = s * vkp + c * vkq;
        }
    }

    Vector eigenvalues(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        eigenvalues[i] = d(i, i);
    }

    return EigenSymResult{eigenvalues, v};
}

} // namespace telemetry::linalg