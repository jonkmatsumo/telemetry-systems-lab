#pragma once

#include <cstddef>
#include <vector>

namespace telemetry {
namespace linalg {

using Vector = std::vector<double>;

struct Matrix {
    size_t rows = 0;
    size_t cols = 0;
    std::vector<double> data;

    Matrix() = default;
    Matrix(size_t r, size_t c);

    double& operator()(size_t r, size_t c);
    double operator()(size_t r, size_t c) const;
};

struct EigenSymResult {
    Vector eigenvalues;
    Matrix eigenvectors; // columns are eigenvectors
};

Matrix identity(size_t n);
Matrix transpose(const Matrix& m);
Matrix matmul(const Matrix& a, const Matrix& b);
Vector matvec(const Matrix& a, const Vector& x);

double dot(const Vector& a, const Vector& b);
double l2_norm(const Vector& v);

std::vector<size_t> argsort_desc(const Vector& v);

EigenSymResult eigen_sym_jacobi(const Matrix& a, int max_iter, double eps);

} // namespace linalg
} // namespace telemetry
