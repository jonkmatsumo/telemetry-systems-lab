#include <gtest/gtest.h>

#include "linalg/matrix.h"

using telemetry::linalg::Matrix;
using telemetry::linalg::Vector;

TEST(LinalgTest, EigenSymmetric2x2) {
    Matrix a(2, 2);
    a(0, 0) = 2.0;
    a(0, 1) = 1.0;
    a(1, 0) = 1.0;
    a(1, 1) = 2.0;

    auto res = telemetry::linalg::eigen_sym_jacobi(a, 100, 1e-12);

    ASSERT_EQ(res.eigenvalues.size(), 2u);
    // Expected eigenvalues: 3 and 1
    double max_ev = std::max(res.eigenvalues[0], res.eigenvalues[1]);
    double min_ev = std::min(res.eigenvalues[0], res.eigenvalues[1]);
    EXPECT_NEAR(max_ev, 3.0, 1e-6);
    EXPECT_NEAR(min_ev, 1.0, 1e-6);

    // Orthonormality: V^T V = I
    auto vt = telemetry::linalg::transpose(res.eigenvectors);
    auto vtv = telemetry::linalg::matmul(vt, res.eigenvectors);
    EXPECT_NEAR(vtv(0, 0), 1.0, 1e-6);
    EXPECT_NEAR(vtv(1, 1), 1.0, 1e-6);
    EXPECT_NEAR(vtv(0, 1), 0.0, 1e-6);
    EXPECT_NEAR(vtv(1, 0), 0.0, 1e-6);
}

TEST(LinalgTest, EigenSymmetric3x3Recompose) {
    Matrix a(3, 3);
    a(0, 0) = 4.0; a(0, 1) = 1.0; a(0, 2) = 1.0;
    a(1, 0) = 1.0; a(1, 1) = 3.0; a(1, 2) = 0.0;
    a(2, 0) = 1.0; a(2, 1) = 0.0; a(2, 2) = 2.0;

    auto res = telemetry::linalg::eigen_sym_jacobi(a, 200, 1e-12);

    Matrix d(3, 3);
    d(0, 0) = res.eigenvalues[0];
    d(1, 1) = res.eigenvalues[1];
    d(2, 2) = res.eigenvalues[2];

    auto vt = telemetry::linalg::transpose(res.eigenvectors);
    auto vdv = telemetry::linalg::matmul(telemetry::linalg::matmul(res.eigenvectors, d), vt);

    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(vdv(r, c), a(r, c), 1e-6);
        }
    }
}
