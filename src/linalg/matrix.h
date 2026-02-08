#pragma once

#include <cstddef>
#include <vector>

namespace telemetry::linalg {

using Vector = std::vector<double>;

struct Matrix {
    size_t rows = 0;
    size_t cols = 0;
    std::vector<double> data;

    Matrix() = default;
    Matrix(size_t r, size_t c);

    auto operator()(size_t r, size_t c) -> double&;
    auto operator()(size_t r, size_t c) const -> double;
};

struct EigenSymResult {
    Vector eigenvalues;
    Matrix eigenvectors; // columns are eigenvectors
};

auto identity(size_t n) -> Matrix;
auto transpose(const Matrix& m) -> Matrix;
auto matmul(const Matrix& a, const Matrix& b) -> Matrix;
auto matvec(const Matrix& a, const Vector& x) -> Vector;

auto dot(const Vector& a, const Vector& b) -> double;
auto l2_norm(const Vector& v) -> double;

auto argsort_desc(const Vector& v) -> std::vector<size_t>;

auto eigen_sym_jacobi(const Matrix& a, int max_iter, double eps) -> EigenSymResult;

} // namespace telemetry::linalg