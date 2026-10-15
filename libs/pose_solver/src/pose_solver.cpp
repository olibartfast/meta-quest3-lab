#include "pose_solver/pose_solver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace questlab::pose {
namespace {

constexpr int kMaxEigenSize = 12;
constexpr int kMinimalSampleSize = 6;

// Spread classification of the object-point cloud: degenerate (coincident or
// collinear), coplanar (2D spread; needs the planar homography path), or
// non-coplanar (3D spread; needs the full 6-point DLT path).
enum class PointSpread {
    kDegenerate,
    kPlanar,
    kNonPlanar,
};

struct PlaneGeometry {
    double centroid[3] = {0.0, 0.0, 0.0};
    double basis[3][3] = {};  // rows u, v, n (orthonormal, right-handed)
};

// --- Deterministic PRNG (splitmix64) ----------------------------------------

uint64_t SplitMix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// --- Symmetric Jacobi eigendecomposition ------------------------------------

// Eigen-decomposes the n x n symmetric matrix aIn (row-major). On return,
// values[j] is the j-th eigenvalue and column j of vectors holds the
// corresponding eigenvector (vectors[i*n+j] = i-th component). Eigenvalues are
// not sorted. n must not exceed kMaxEigenSize.
void JacobiEigen(int n, const double* aIn, double* values, double* vectors) {
    double a[kMaxEigenSize][kMaxEigenSize] = {};
    double v[kMaxEigenSize][kMaxEigenSize] = {};
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            a[i][j] = aIn[i * n + j];
        }
        v[i][i] = 1.0;
    }
    const int maxSweeps = 64;
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                off += a[p][q] * a[p][q];
            }
        }
        if (off < 1e-30) {
            break;
        }
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[p][q];
                if (std::fabs(apq) < 1e-300) {
                    continue;
                }
                const double app = a[p][p];
                const double aqq = a[q][q];
                const double theta = (aqq - app) / (2.0 * apq);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                    (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = a[k][p];
                    const double akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = a[p][k];
                    const double aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = v[k][p];
                    const double vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int j = 0; j < n; ++j) {
        values[j] = a[j][j];
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            vectors[i * n + j] = v[i][j];
        }
    }
}

// --- Hartley normalization + 6-point DLT ------------------------------------

struct Normalize2D {
    double scale = 1.0;
    double cx = 0.0;
    double cy = 0.0;
};

struct Normalize3D {
    double scale = 1.0;
    double cx = 0.0;
    double cy = 0.0;
    double cz = 0.0;
};

// Centroid to the origin, then scale so mean distance from the origin is
// sqrt(3) for 3D points (a cube's mean radius) and sqrt(2) for 2D points.
bool NormalizeObject(const double points[kMinimalSampleSize][3],
                     Normalize3D* norm,
                     double out[kMinimalSampleSize][3]) {
    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        cx += points[i][0];
        cy += points[i][1];
        cz += points[i][2];
    }
    cx /= kMinimalSampleSize;
    cy /= kMinimalSampleSize;
    cz /= kMinimalSampleSize;
    double meanDistance = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        const double dx = points[i][0] - cx;
        const double dy = points[i][1] - cy;
        const double dz = points[i][2] - cz;
        meanDistance += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    meanDistance /= kMinimalSampleSize;
    if (!(meanDistance > 1e-12)) {
        return false;
    }
    const double scale = std::sqrt(3.0) / meanDistance;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        out[i][0] = scale * (points[i][0] - cx);
        out[i][1] = scale * (points[i][1] - cy);
        out[i][2] = scale * (points[i][2] - cz);
    }
    norm->scale = scale;
    norm->cx = cx;
    norm->cy = cy;
    norm->cz = cz;
    return true;
}

bool NormalizeImage(const double points[kMinimalSampleSize][2],
                    Normalize2D* norm,
                    double out[kMinimalSampleSize][2]) {
    double cx = 0.0, cy = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        cx += points[i][0];
        cy += points[i][1];
    }
    cx /= kMinimalSampleSize;
    cy /= kMinimalSampleSize;
    double meanDistance = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        const double dx = points[i][0] - cx;
        const double dy = points[i][1] - cy;
        meanDistance += std::sqrt(dx * dx + dy * dy);
    }
    meanDistance /= kMinimalSampleSize;
    if (!(meanDistance > 1e-12)) {
        return false;
    }
    const double scale = std::sqrt(2.0) / meanDistance;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        out[i][0] = scale * (points[i][0] - cx);
        out[i][1] = scale * (points[i][1] - cy);
    }
    norm->scale = scale;
    norm->cx = cx;
    norm->cy = cy;
    return true;
}

// Solves the 6-point DLT for the 3x4 projection matrix P mapping object points
// (metres) to normalized image coordinates (pixels after K^-1). Output p is
// row-major, denormalized back to metres/normalized-pixels, but still only up
// to an arbitrary scale and sign (the caller resolves cheirality + metric
// scale). Returns false when the sample is degenerate.
bool SolveProjectionMatrix(const double object[kMinimalSampleSize][3],
                           const double image[kMinimalSampleSize][2],
                           double p[12]) {
    Normalize3D normObject;
    Normalize2D normImage;
    double obj[kMinimalSampleSize][3];
    double img[kMinimalSampleSize][2];
    if (!NormalizeObject(object, &normObject, obj)) {
        return false;
    }
    if (!NormalizeImage(image, &normImage, img)) {
        return false;
    }

    // Build A^T A for the two cross-product equations per point:
    //   -X, -Y, -Z, -1, 0, 0, 0, 0, xX, xY, xZ, x   (x residual)
    //   0, 0, 0, 0, -X, -Y, -Z, -1, yX, yY, yZ, y   (y residual)
    double a[kMaxEigenSize][kMaxEigenSize] = {};
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        const double X = obj[i][0];
        const double Y = obj[i][1];
        const double Z = obj[i][2];
        const double x = img[i][0];
        const double y = img[i][1];
        const double r1[12] = {
            -X, -Y, -Z, -1.0, 0.0, 0.0, 0.0, 0.0, x * X, x * Y, x * Z, x};
        const double r2[12] = {
            0.0, 0.0, 0.0, 0.0, -X, -Y, -Z, -1.0, y * X, y * Y, y * Z, y};
        for (int r = 0; r < 12; ++r) {
            for (int c = 0; c < 12; ++c) {
                a[r][c] += r1[r] * r1[c] + r2[r] * r2[c];
            }
        }
    }

    double values[12] = {};
    double vectors[12 * 12] = {};
    JacobiEigen(12, &a[0][0], values, vectors);
    int smallest = 0;
    double smallestAbs = std::fabs(values[0]);
    for (int j = 1; j < 12; ++j) {
        if (std::fabs(values[j]) < smallestAbs) {
            smallestAbs = std::fabs(values[j]);
            smallest = j;
        }
    }
    double pn[12];
    for (int i = 0; i < 12; ++i) {
        pn[i] = vectors[i * 12 + smallest];
    }

    // Denormalize: P = Ti^{-1} * Pn * To, where
    //   Ti^{-1} = [[1/si, 0, ci_x], [0, 1/si, ci_y], [0, 0, 1]]
    //   To      = [so I | -so co]  (top 3 rows of the 4x4 object transform)
    const double si = normImage.scale;
    const double so = normObject.scale;
    double a2[3][4];
    for (int c = 0; c < 4; ++c) {
        a2[0][c] = pn[c] / si + normImage.cx * pn[8 + c];
        a2[1][c] = pn[4 + c] / si + normImage.cy * pn[8 + c];
        a2[2][c] = pn[8 + c];
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            p[r * 4 + c] = so * a2[r][c];
        }
        p[r * 4 + 3] = a2[r][3] -
            so * (a2[r][0] * normObject.cx +
                  a2[r][1] * normObject.cy +
                  a2[r][2] * normObject.cz);
    }
    return true;
}

// --- Rotation recovery from the DLT projection matrix -----------------------

// Resolves cheirality + metric scale on a non-coplanar DLT projection matrix
// and orthonormalizes its rotation block. The whole 12-vector is negated when
// the sampled points' camera depths are negative (never rotation and
// translation separately). Columns 0 and 1 of the 3x3 block carry the metric
// scale (each equals lambda * a unit rotation column): they are scaled to
// unit length and column 2 is completed by their cross product, which
// enforces orthonormality and det = +1 by construction and gives the last
// column its translation scale. Coplanar samples never reach this function;
// they take the plane-homography path (PoseFromHomography).
bool MinimalPoseFromP(const double p[12],
                      const double object[kMinimalSampleSize][3],
                      double r[3][3],
                      double t[3]) {
    // Cheirality: negate the whole 12-vector so the sampled depths are
    // positive (row 3 dotted with each homogeneous object point).
    double depthSum = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        depthSum += p[8] * object[i][0] +
            p[9] * object[i][1] +
            p[10] * object[i][2] +
            p[11];
    }
    if (!std::isfinite(depthSum) || depthSum == 0.0) {
        return false;
    }
    const double sign = depthSum < 0.0 ? -1.0 : 1.0;
    // Column 0 = [p0, p4, p8], column 1 = [p1, p5, p9], column 3 = [p3, p7,
    // p11] (column 2 = [p2, p6, p10] is discarded).
    double c0[3] = {sign * p[0], sign * p[4], sign * p[8]};
    double c1[3] = {sign * p[1], sign * p[5], sign * p[9]};
    double ct[3] = {sign * p[3], sign * p[7], sign * p[11]};
    const double norm0 = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]);
    const double norm1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    const double lambda = 0.5 * (norm0 + norm1);
    if (!(lambda > 1e-12) || !std::isfinite(lambda)) {
        return false;
    }
    double r0[3];
    double r1[3];
    for (int k = 0; k < 3; ++k) {
        r0[k] = c0[k] / lambda;
        r1[k] = c1[k] / lambda;
    }
    const double projection = r0[0] * r1[0] + r0[1] * r1[1] + r0[2] * r1[2];
    for (int k = 0; k < 3; ++k) {
        r1[k] -= projection * r0[k];
    }
    const double r1Norm = std::sqrt(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]);
    if (!(r1Norm > 1e-12)) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        r1[k] /= r1Norm;
    }
    const double r2[3] = {
        r0[1] * r1[2] - r0[2] * r1[1],
        r0[2] * r1[0] - r0[0] * r1[2],
        r0[0] * r1[1] - r0[1] * r1[0],
    };
    // Assemble the rotation matrix from its columns (r0, r1, r2) and read the
    // translation with the same metric scale.
    r[0][0] = r0[0];
    r[0][1] = r1[0];
    r[0][2] = r2[0];
    r[1][0] = r0[1];
    r[1][1] = r1[1];
    r[1][2] = r2[1];
    r[2][0] = r0[2];
    r[2][1] = r1[2];
    r[2][2] = r2[2];
    for (int k = 0; k < 3; ++k) {
        t[k] = ct[k] / lambda;
    }
    return true;
}

// --- Planar path: homography + decomposition ---------------------------------

// Solves the 3x3 homography mapping 2D plane points to normalized image
// coordinates from exactly 6 correspondences (a coplanar sample). Hartley-
// normalized DLT with the smallest-eigenvector extraction; output h is
// row-major, denormalized, up to scale and sign.
bool SolveHomography(const double plane[kMinimalSampleSize][2],
                     const double image[kMinimalSampleSize][2],
                     double h[9]) {
    Normalize2D normPlane;
    Normalize2D normImage;
    double planeNorm[kMinimalSampleSize][2];
    double imageNorm[kMinimalSampleSize][2];
    if (!NormalizeImage(plane, &normPlane, planeNorm)) {
        return false;
    }
    if (!NormalizeImage(image, &normImage, imageNorm)) {
        return false;
    }
    // The normal matrix must be a contiguous 9x9 block: JacobiEigen indexes
    // aIn as n*n row-major, so the row stride has to equal n (not
    // kMaxEigenSize).
    double a[9][9] = {};
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        const double x = planeNorm[i][0];
        const double y = planeNorm[i][1];
        const double u = imageNorm[i][0];
        const double v = imageNorm[i][1];
        const double r1[9] = {-x, -y, -1.0, 0.0, 0.0, 0.0, u * x, u * y, u};
        const double r2[9] = {0.0, 0.0, 0.0, -x, -y, -1.0, v * x, v * y, v};
        for (int r = 0; r < 9; ++r) {
            for (int c = 0; c < 9; ++c) {
                a[r][c] += r1[r] * r1[c] + r2[r] * r2[c];
            }
        }
    }
    double values[kMaxEigenSize] = {};
    double vectors[kMaxEigenSize * kMaxEigenSize] = {};
    JacobiEigen(9, &a[0][0], values, vectors);
    int smallest = 0;
    double smallestAbs = std::fabs(values[0]);
    for (int j = 1; j < 9; ++j) {
        if (std::fabs(values[j]) < smallestAbs) {
            smallestAbs = std::fabs(values[j]);
            smallest = j;
        }
    }
    double hn[9];
    for (int i = 0; i < 9; ++i) {
        hn[i] = vectors[i * 9 + smallest];
    }
    // Denormalize: H = Ti^{-1} * Hn * Tp.
    const double si = normImage.scale;
    const double sp = normPlane.scale;
    double a2[3][3];
    for (int c = 0; c < 3; ++c) {
        a2[0][c] = hn[c] / si + normImage.cx * hn[6 + c];
        a2[1][c] = hn[3 + c] / si + normImage.cy * hn[6 + c];
        a2[2][c] = hn[6 + c];
    }
    for (int r = 0; r < 3; ++r) {
        h[r * 3] = sp * a2[r][0];
        h[r * 3 + 1] = sp * a2[r][1];
        h[r * 3 + 2] = a2[r][2] -
            sp * (normPlane.cx * a2[r][0] + normPlane.cy * a2[r][1]);
    }
    return true;
}

// Recovers the camera-from-object pose from a plane homography H = [Ru Rv
// Rc+t] (columns), resolving cheirality (whole-matrix sign) and metric scale
// from columns 0 and 1, then composing the in-plane rotation back into the 3D
// object frame via the plane basis and centroid.
bool PoseFromHomography(const double h[9],
                        const double plane[kMinimalSampleSize][2],
                        const PlaneGeometry& planeGeometry,
                        double r[3][3],
                        double t[3]) {
    // Cheirality: negate the whole homography if the sampled depths are
    // negative (row 3 of H dotted with each homogeneous plane point).
    double depthSum = 0.0;
    for (int i = 0; i < kMinimalSampleSize; ++i) {
        depthSum += h[6] * plane[i][0] + h[7] * plane[i][1] + h[8];
    }
    if (!std::isfinite(depthSum) || depthSum == 0.0) {
        return false;
    }
    const double sign = depthSum < 0.0 ? -1.0 : 1.0;
    // Columns of H: c0 = [h0,h3,h6], c1 = [h1,h4,h7], ct = [h2,h5,h8].
    double c0[3] = {sign * h[0], sign * h[3], sign * h[6]};
    double c1[3] = {sign * h[1], sign * h[4], sign * h[7]};
    double ct[3] = {sign * h[2], sign * h[5], sign * h[8]};
    const double norm0 = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]);
    const double norm1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    const double lambda = 0.5 * (norm0 + norm1);
    if (!(lambda > 1e-12) || !std::isfinite(lambda)) {
        return false;
    }
    double r0[3];
    double r1[3];
    for (int k = 0; k < 3; ++k) {
        r0[k] = c0[k] / lambda;
        r1[k] = c1[k] / lambda;
        ct[k] /= lambda;
    }
    const double projection = r0[0] * r1[0] + r0[1] * r1[1] + r0[2] * r1[2];
    for (int k = 0; k < 3; ++k) {
        r1[k] -= projection * r0[k];
    }
    const double r1Norm = std::sqrt(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]);
    if (!(r1Norm > 1e-12)) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        r1[k] /= r1Norm;
    }
    const double r2[3] = {
        r0[1] * r1[2] - r0[2] * r1[1],
        r0[2] * r1[0] - r0[0] * r1[2],
        r0[0] * r1[1] - r0[1] * r1[0],
    };
    // M = [r0 r1 r2] (columns). R_full = M * basis (rows u, v, n).
    const double m[3][3] = {
        {r0[0], r1[0], r2[0]},
        {r0[1], r1[1], r2[1]},
        {r0[2], r1[2], r2[2]},
    };
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += m[row][k] * planeGeometry.basis[k][col];
            }
            r[row][col] = sum;
        }
    }
    // t = t' - R_full * centroid.
    for (int row = 0; row < 3; ++row) {
        const double rc = r[row][0] * planeGeometry.centroid[0] +
            r[row][1] * planeGeometry.centroid[1] +
            r[row][2] * planeGeometry.centroid[2];
        t[row] = ct[row] - rc;
    }
    return true;
}

// --- Degenerate-geometry / spread analysis ----------------------------------

// Classifies n object points (row-major xyz): degenerate (coincident or
// collinear), coplanar, or non-coplanar. When planar, fills `plane` with the
// centroid and an orthonormal right-handed in-plane basis (rows u, v) plus the
// plane normal n = u x v. The coplanarity tolerance is scale-relative.
PointSpread AnalyzeSpread(const double* points, int n, PlaneGeometry* plane) {
    if (n < 3) {
        return PointSpread::kDegenerate;
    }
    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (int i = 0; i < n; ++i) {
        cx += points[i * 3];
        cy += points[i * 3 + 1];
        cz += points[i * 3 + 2];
    }
    cx /= n;
    cy /= n;
    cz /= n;
    double scale = 0.0;
    for (int i = 0; i < n; ++i) {
        const double dx = points[i * 3] - cx;
        const double dy = points[i * 3 + 1] - cy;
        const double dz = points[i * 3 + 2] - cz;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > scale) {
            scale = d;
        }
    }
    if (scale <= 1e-12) {
        return PointSpread::kDegenerate;  // all points coincide
    }
    const double epsilon = 1e-8 * scale;
    int unique = 0;
    for (int i = 0; i < n; ++i) {
        bool duplicate = false;
        for (int j = 0; j < i; ++j) {
            const double dx = points[i * 3] - points[j * 3];
            const double dy = points[i * 3 + 1] - points[j * 3 + 1];
            const double dz = points[i * 3 + 2] - points[j * 3 + 2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) <= epsilon) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            ++unique;
        }
    }
    if (unique < 3) {
        return PointSpread::kDegenerate;
    }
    double cov[3][3] = {};
    for (int i = 0; i < n; ++i) {
        const double dx = points[i * 3] - cx;
        const double dy = points[i * 3 + 1] - cy;
        const double dz = points[i * 3 + 2] - cz;
        cov[0][0] += dx * dx;
        cov[0][1] += dx * dy;
        cov[0][2] += dx * dz;
        cov[1][0] += dy * dx;
        cov[1][1] += dy * dy;
        cov[1][2] += dy * dz;
        cov[2][0] += dz * dx;
        cov[2][1] += dz * dy;
        cov[2][2] += dz * dz;
    }
    double values[3] = {};
    double vectors[9] = {};
    JacobiEigen(3, &cov[0][0], values, vectors);
    double l[3] = {values[0], values[1], values[2]};
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (l[order[j]] > l[order[i]]) {
                const int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }
    const double l0 = l[order[0]];
    const double l1 = l[order[1]];
    const double l2 = l[order[2]];
    if (!(l0 > 0.0)) {
        return PointSpread::kDegenerate;
    }
    if (l1 <= 1e-8 * l0) {
        return PointSpread::kDegenerate;  // collinear
    }
    if (plane != nullptr && l2 <= 1e-6 * l1) {
        // Coplanar: build the centroid + orthonormal basis (u, v, n = u x v).
        plane->centroid[0] = cx;
        plane->centroid[1] = cy;
        plane->centroid[2] = cz;
        for (int axis = 0; axis < 3; ++axis) {
            const int col = order[axis];
            plane->basis[axis][0] = vectors[col * 3];
            plane->basis[axis][1] = vectors[col * 3 + 1];
            plane->basis[axis][2] = vectors[col * 3 + 2];
        }
        // Force right-handedness: n = u x v.
        plane->basis[2][0] = plane->basis[0][1] * plane->basis[1][2] -
            plane->basis[0][2] * plane->basis[1][1];
        plane->basis[2][1] = plane->basis[0][2] * plane->basis[1][0] -
            plane->basis[0][0] * plane->basis[1][2];
        plane->basis[2][2] = plane->basis[0][0] * plane->basis[1][1] -
            plane->basis[0][1] * plane->basis[1][0];
    }
    return l2 <= 1e-6 * l1 ? PointSpread::kPlanar : PointSpread::kNonPlanar;
}

// True when the object points are degenerate (coincident or collinear).
bool ObjectPointsDegenerate(const double* points, int n) {
    return AnalyzeSpread(points, n, nullptr) == PointSpread::kDegenerate;
}

// --- Reprojection -----------------------------------------------------------

double ReprojectionErrorPx(const Correspondence& c,
                           const Intrinsics& k,
                           const double r[3][3],
                           const double t[3]) {
    const double X = c.objectPoint.x;
    const double Y = c.objectPoint.y;
    const double Z = c.objectPoint.z;
    const double px = r[0][0] * X + r[0][1] * Y + r[0][2] * Z + t[0];
    const double py = r[1][0] * X + r[1][1] * Y + r[1][2] * Z + t[1];
    const double pz = r[2][0] * X + r[2][1] * Y + r[2][2] * Z + t[2];
    if (!(pz > 1e-12)) {
        return 1.0e30;  // behind the camera: never an inlier
    }
    const double u = k.fx * px / pz + k.cx;
    const double v = k.fy * py / pz + k.cy;
    const double dx = u - c.pixel.x;
    const double dy = v - c.pixel.y;
    return std::sqrt(dx * dx + dy * dy);
}

// --- Gauss-Newton refinement -------------------------------------------------

void ExpMap(const double w[3], double r[3][3]) {
    const double theta2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
    const double theta = std::sqrt(theta2);
    double sinOverTheta;
    double oneMinusCosOverTheta2;
    if (theta < 1e-12) {
        sinOverTheta = 1.0 - theta2 / 6.0;
        oneMinusCosOverTheta2 = 0.5 - theta2 / 24.0;
    } else {
        sinOverTheta = std::sin(theta) / theta;
        oneMinusCosOverTheta2 = (1.0 - std::cos(theta)) / theta2;
    }
    const double wx[3][3] = {
        {0.0, -w[2], w[1]},
        {w[2], 0.0, -w[0]},
        {-w[1], w[0], 0.0},
    };
    double wx2[3][3] = {};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) {
                s += wx[i][k] * wx[k][j];
            }
            wx2[i][j] = s;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[i][j] = (i == j ? 1.0 : 0.0) +
                sinOverTheta * wx[i][j] +
                oneMinusCosOverTheta2 * wx2[i][j];
        }
    }
}

bool Solve6x6(double a[6][6], const double b[6], double x[6]) {
    double m[6][7];
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            m[i][j] = a[i][j];
        }
        m[i][6] = b[i];
    }
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 6; ++row) {
            if (std::fabs(m[row][col]) > std::fabs(m[pivot][col])) {
                pivot = row;
            }
        }
        if (std::fabs(m[pivot][col]) < 1e-15) {
            return false;
        }
        if (pivot != col) {
            for (int j = col; j < 7; ++j) {
                const double tmp = m[col][j];
                m[col][j] = m[pivot][j];
                m[pivot][j] = tmp;
            }
        }
        for (int row = col + 1; row < 6; ++row) {
            const double factor = m[row][col] / m[col][col];
            if (factor == 0.0) {
                continue;
            }
            for (int j = col; j < 7; ++j) {
                m[row][j] -= factor * m[col][j];
            }
        }
    }
    for (int i = 5; i >= 0; --i) {
        double sum = m[i][6];
        for (int j = i + 1; j < 6; ++j) {
            sum -= m[i][j] * x[j];
        }
        x[i] = sum / m[i][i];
    }
    return true;
}

// Gauss-Newton on the given inlier indices, minimizing reprojection error with
// an analytic Jacobian (p = R X + t; d(p)/domega = -[q]x with q = R X,
// p = q + t, for the left perturbation R <- Exp(omega) R; d(p)/dt = I).
// Returns false only when the initial pose is unusable (a point behind the
// camera); a non-improving or singular step stops the iteration at the
// best-so-far pose, which is convergence, not an error.
bool RefinePose(const Correspondence* correspondences,
                const int* indices,
                int n,
                const Intrinsics& k,
                double r[3][3],
                double t[3]) {
    const double fx = k.fx;
    const double fy = k.fy;
    const double cx = k.cx;
    const double cy = k.cy;
    const auto computeCost = [&](const double rr[3][3],
                                 const double tt[3],
                                 double* cost) -> bool {
        double sum = 0.0;
        for (int ii = 0; ii < n; ++ii) {
            const Correspondence& c = correspondences[indices[ii]];
            const double X = c.objectPoint.x;
            const double Y = c.objectPoint.y;
            const double Z = c.objectPoint.z;
            const double px = rr[0][0] * X + rr[0][1] * Y + rr[0][2] * Z + tt[0];
            const double py = rr[1][0] * X + rr[1][1] * Y + rr[1][2] * Z + tt[1];
            const double pz = rr[2][0] * X + rr[2][1] * Y + rr[2][2] * Z + tt[2];
            if (!(pz > 1e-12)) {
                return false;
            }
            const double invZ = 1.0 / pz;
            const double u = fx * px * invZ + cx;
            const double v = fy * py * invZ + cy;
            const double dx = u - c.pixel.x;
            const double dy = v - c.pixel.y;
            sum += dx * dx + dy * dy;
        }
        *cost = sum;
        return true;
    };

    double cost = 0.0;
    if (!computeCost(r, t, &cost)) {
        return false;
    }
    for (int iter = 0; iter < 50; ++iter) {
        double g[6][6] = {};
        double rhs[6] = {};
        for (int ii = 0; ii < n; ++ii) {
            const Correspondence& c = correspondences[indices[ii]];
            const double X = c.objectPoint.x;
            const double Y = c.objectPoint.y;
            const double Z = c.objectPoint.z;
            const double px = r[0][0] * X + r[0][1] * Y + r[0][2] * Z + t[0];
            const double py = r[1][0] * X + r[1][1] * Y + r[1][2] * Z + t[1];
            const double pz = r[2][0] * X + r[2][1] * Y + r[2][2] * Z + t[2];
            if (!(pz > 1e-12)) {
                return false;
            }
            const double invZ = 1.0 / pz;
            const double u = fx * px * invZ + cx;
            const double v = fy * py * invZ + cy;
            const double fxResidual = c.pixel.x - u;
            const double fyResidual = c.pixel.y - v;
            const double a = fx * invZ;
            const double b = fy * invZ;
            const double cPrime = -fx * px * invZ * invZ;
            const double dPrime = -fy * py * invZ * invZ;
            // Rotated point q = R X (before translation): the rotational
            // Jacobian is -[q]x, not -[p]x, for the left perturbation
            // R <- Exp(omega) R with p = q + t.
            const double qx = px - t[0];
            const double qy = py - t[1];
            const double qz = pz - t[2];
            double j[2][6];
            j[0][0] = cPrime * qy;
            j[0][1] = a * qz - cPrime * qx;
            j[0][2] = -a * qy;
            j[0][3] = a;
            j[0][4] = 0.0;
            j[0][5] = cPrime;
            j[1][0] = -b * qz + dPrime * qy;
            j[1][1] = -dPrime * qx;
            j[1][2] = b * qx;
            j[1][3] = 0.0;
            j[1][4] = b;
            j[1][5] = dPrime;
            for (int row = 0; row < 6; ++row) {
                for (int col = 0; col < 6; ++col) {
                    g[row][col] += j[0][row] * j[0][col] + j[1][row] * j[1][col];
                }
                rhs[row] += j[0][row] * fxResidual + j[1][row] * fyResidual;
            }
        }
        double trace = 0.0;
        for (int i = 0; i < 6; ++i) {
            trace += g[i][i];
        }
        const double lambda = 1e-6 * trace / 6.0;
        double a[6][6];
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 6; ++col) {
                a[row][col] = g[row][col] + (row == col ? lambda : 0.0);
            }
        }
        double delta[6];
        if (!Solve6x6(a, rhs, delta)) {
            break;  // cannot update: keep the best-so-far pose
        }
        bool stepFinite = true;
        for (int i = 0; i < 6; ++i) {
            if (!std::isfinite(delta[i])) {
                stepFinite = false;
                break;
            }
        }
        if (!stepFinite) {
            break;
        }
        double w[3] = {delta[0], delta[1], delta[2]};
        double dt[3] = {delta[3], delta[4], delta[5]};
        double stepSquared = 0.0;
        for (int i = 0; i < 6; ++i) {
            stepSquared += delta[i] * delta[i];
        }
        double rExp[3][3];
        ExpMap(w, rExp);
        double rNext[3][3];
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                double s = 0.0;
                for (int k2 = 0; k2 < 3; ++k2) {
                    s += rExp[row][k2] * r[k2][col];
                }
                rNext[row][col] = s;
            }
        }
        double tNext[3] = {t[0] + dt[0], t[1] + dt[1], t[2] + dt[2]};
        if (stepSquared < 1e-24) {
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    r[row][col] = rNext[row][col];
                }
            }
            t[0] = tNext[0];
            t[1] = tNext[1];
            t[2] = tNext[2];
            break;
        }
        double nextCost = 0.0;
        if (!computeCost(rNext, tNext, &nextCost) ||
            !std::isfinite(nextCost) || nextCost > cost) {
            // The step does not improve the fit: stop at the best-so-far
            // pose. Gauss-Newton has no line search, so a rejected step is
            // convergence, not an error.
            break;
        }
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                r[row][col] = rNext[row][col];
            }
        }
        t[0] = tNext[0];
        t[1] = tNext[1];
        t[2] = tNext[2];
        cost = nextCost;
    }
    return true;
}

// --- Quaternion from rotation matrix ----------------------------------------

math::Quat QuatFromRotation(const double r[3][3]) {
    double qx = 0.0, qy = 0.0, qz = 0.0, qw = 0.0;
    const double trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        qw = 0.25 * s;
        qx = (r[2][1] - r[1][2]) / s;
        qy = (r[0][2] - r[2][0]) / s;
        qz = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
        qw = (r[2][1] - r[1][2]) / s;
        qx = 0.25 * s;
        qy = (r[0][1] + r[1][0]) / s;
        qz = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
        qw = (r[0][2] - r[2][0]) / s;
        qx = (r[0][1] + r[1][0]) / s;
        qy = 0.25 * s;
        qz = (r[1][2] + r[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
        qw = (r[1][0] - r[0][1]) / s;
        qx = (r[0][2] + r[2][0]) / s;
        qy = (r[1][2] + r[2][1]) / s;
        qz = 0.25 * s;
    }
    const double norm = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (!(norm > 0.0)) {
        return math::Quat{};
    }
    qx /= norm;
    qy /= norm;
    qz /= norm;
    qw /= norm;
    if (qw < 0.0) {
        qx = -qx;
        qy = -qy;
        qz = -qz;
        qw = -qw;
    }
    return math::Quat{
        static_cast<float>(qx),
        static_cast<float>(qy),
        static_cast<float>(qz),
        static_cast<float>(qw),
    };
}

bool DrawSample(uint64_t* state, int count, int indices[kMinimalSampleSize]) {
    for (int k = 0; k < kMinimalSampleSize; ++k) {
        bool placed = false;
        for (int attempt = 0; attempt < 128; ++attempt) {
            const int candidate = static_cast<int>(
                SplitMix64(state) % static_cast<uint64_t>(count));
            bool duplicate = false;
            for (int j = 0; j < k; ++j) {
                if (indices[j] == candidate) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                indices[k] = candidate;
                placed = true;
                break;
            }
        }
        if (!placed) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool SolvePose(
    const Correspondence* correspondences,
    int count,
    const Intrinsics& intrinsics,
    const Options& options,
    Result* result,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (result == nullptr) {
        return fail("invalid pose input");
    }
    *result = Result{};
    if (count < kMinimalSampleSize) {
        return fail("insufficient correspondences");
    }
    if (correspondences == nullptr) {
        return fail("invalid pose input");
    }
    for (int i = 0; i < count; ++i) {
        const Correspondence& c = correspondences[i];
        if (!std::isfinite(c.objectPoint.x) ||
            !std::isfinite(c.objectPoint.y) ||
            !std::isfinite(c.objectPoint.z) ||
            !std::isfinite(c.pixel.x) ||
            !std::isfinite(c.pixel.y)) {
            return fail("non-finite correspondence");
        }
    }
    if (!(intrinsics.fx > 0.0F && std::isfinite(intrinsics.fx)) ||
        !(intrinsics.fy > 0.0F && std::isfinite(intrinsics.fy)) ||
        !std::isfinite(intrinsics.cx) ||
        !std::isfinite(intrinsics.cy)) {
        return fail("invalid pose input");
    }
    if (!(options.reprojectionThresholdPx > 0.0F) ||
        options.maxIterations < 1 ||
        options.minInliers < 3) {
        return fail("invalid pose input");
    }

    // Precompute object points (double) and normalized image coordinates.
    const double inverseFx = 1.0 / intrinsics.fx;
    const double inverseFy = 1.0 / intrinsics.fy;
    std::vector<double> object(static_cast<size_t>(count) * 3);
    std::vector<double> normalized(static_cast<size_t>(count) * 2);
    for (int i = 0; i < count; ++i) {
        const Correspondence& c = correspondences[i];
        object[static_cast<size_t>(i) * 3] = c.objectPoint.x;
        object[static_cast<size_t>(i) * 3 + 1] = c.objectPoint.y;
        object[static_cast<size_t>(i) * 3 + 2] = c.objectPoint.z;
        normalized[static_cast<size_t>(i) * 2] =
            (c.pixel.x - intrinsics.cx) * inverseFx;
        normalized[static_cast<size_t>(i) * 2 + 1] =
            (c.pixel.y - intrinsics.cy) * inverseFy;
    }

    PlaneGeometry planeGeometry;
    const PointSpread spread = AnalyzeSpread(object.data(), count, &planeGeometry);
    if (spread == PointSpread::kDegenerate) {
        return fail("degenerate sample geometry");
    }

    // For a coplanar point set, project each point onto the plane basis once
    // up front; the RANSAC minimal solver then uses the plane homography.
    std::vector<double> plane2d;
    if (spread == PointSpread::kPlanar) {
        plane2d.resize(static_cast<size_t>(count) * 2);
        for (int i = 0; i < count; ++i) {
            const double dx = object[static_cast<size_t>(i) * 3] -
                planeGeometry.centroid[0];
            const double dy = object[static_cast<size_t>(i) * 3 + 1] -
                planeGeometry.centroid[1];
            const double dz = object[static_cast<size_t>(i) * 3 + 2] -
                planeGeometry.centroid[2];
            plane2d[static_cast<size_t>(i) * 2] =
                planeGeometry.basis[0][0] * dx +
                planeGeometry.basis[0][1] * dy +
                planeGeometry.basis[0][2] * dz;
            plane2d[static_cast<size_t>(i) * 2 + 1] =
                planeGeometry.basis[1][0] * dx +
                planeGeometry.basis[1][1] * dy +
                planeGeometry.basis[1][2] * dz;
        }
    }

    // RANSAC with a deterministic splitmix64 stream and adaptive iterations.
    const double confidence = 0.99;
    int adaptiveCount = options.maxIterations;
    int bestInlierCount = 0;
    double bestR[3][3] = {};
    double bestT[3] = {};
    std::vector<int> bestInliers;
    std::vector<int> currentInliers;
    currentInliers.reserve(static_cast<size_t>(count));

    uint64_t rng = options.seed;
    for (int iter = 0; iter < options.maxIterations; ++iter) {
        if (iter >= adaptiveCount) {
            break;
        }
        int sample[kMinimalSampleSize];
        if (!DrawSample(&rng, count, sample)) {
            break;
        }
        double sampleObject[kMinimalSampleSize][3];
        double sampleImage[kMinimalSampleSize][2];
        for (int k = 0; k < kMinimalSampleSize; ++k) {
            const int index = sample[k];
            sampleObject[k][0] = object[static_cast<size_t>(index) * 3];
            sampleObject[k][1] = object[static_cast<size_t>(index) * 3 + 1];
            sampleObject[k][2] = object[static_cast<size_t>(index) * 3 + 2];
            sampleImage[k][0] = normalized[static_cast<size_t>(index) * 2];
            sampleImage[k][1] = normalized[static_cast<size_t>(index) * 2 + 1];
        }
        if (ObjectPointsDegenerate(&sampleObject[0][0], kMinimalSampleSize)) {
            continue;
        }
        double r[3][3];
        double t[3];
        if (spread == PointSpread::kPlanar) {
            double samplePlane[kMinimalSampleSize][2];
            for (int k = 0; k < kMinimalSampleSize; ++k) {
                const int index = sample[k];
                samplePlane[k][0] = plane2d[static_cast<size_t>(index) * 2];
                samplePlane[k][1] = plane2d[static_cast<size_t>(index) * 2 + 1];
            }
            double h[9];
            if (!SolveHomography(samplePlane, sampleImage, h)) {
                continue;
            }
            if (!PoseFromHomography(h, samplePlane, planeGeometry, r, t)) {
                continue;
            }
        } else {
            double p[12];
            if (!SolveProjectionMatrix(sampleObject, sampleImage, p)) {
                continue;
            }
            if (!MinimalPoseFromP(p, sampleObject, r, t)) {
                continue;
            }
        }
        currentInliers.clear();
        for (int i = 0; i < count; ++i) {
            if (ReprojectionErrorPx(correspondences[i], intrinsics, r, t) <=
                options.reprojectionThresholdPx) {
                currentInliers.push_back(i);
            }
        }
        const int inlierCount = static_cast<int>(currentInliers.size());
        if (inlierCount > bestInlierCount) {
            bestInlierCount = inlierCount;
            bestInliers = currentInliers;
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    bestR[row][col] = r[row][col];
                }
                bestT[row] = t[row];
            }
            if (inlierCount == count) {
                adaptiveCount = iter + 1;
            } else if (inlierCount > 0) {
                const double ratio =
                    static_cast<double>(inlierCount) / count;
                double ratioToSix = 1.0;
                for (int e = 0; e < 6; ++e) {
                    ratioToSix *= ratio;
                }
                const double denominator = std::log(1.0 - ratioToSix);
                if (denominator < 0.0) {
                    const double required =
                        std::log(1.0 - confidence) / denominator;
                    adaptiveCount = static_cast<int>(std::ceil(std::min(
                        static_cast<double>(options.maxIterations), required)));
                }
            }
        }
    }

    if (bestInlierCount < options.minInliers) {
        return fail("no consensus");
    }

    // Refine on the consensus set, recompute inliers, then a final refit.
    if (!RefinePose(
            correspondences, bestInliers.data(),
            static_cast<int>(bestInliers.size()), intrinsics, bestR, bestT)) {
        return fail("refinement failed");
    }
    currentInliers.clear();
    for (int i = 0; i < count; ++i) {
        if (ReprojectionErrorPx(correspondences[i], intrinsics, bestR, bestT) <=
            options.reprojectionThresholdPx) {
            currentInliers.push_back(i);
        }
    }
    if (!RefinePose(
            correspondences, currentInliers.data(),
            static_cast<int>(currentInliers.size()), intrinsics, bestR,
            bestT)) {
        return fail("refinement failed");
    }

    int finalInlierCount = 0;
    double squaredErrorSum = 0.0;
    std::vector<uint8_t> inlierMask(
        options.outputInlierMask ? static_cast<size_t>(count) : 0U, 0U);
    for (int i = 0; i < count; ++i) {
        const double errorPx =
            ReprojectionErrorPx(correspondences[i], intrinsics, bestR, bestT);
        if (errorPx <= options.reprojectionThresholdPx) {
            ++finalInlierCount;
            squaredErrorSum += errorPx * errorPx;
            if (!inlierMask.empty()) {
                inlierMask[static_cast<size_t>(i)] = 1U;
            }
        }
    }
    const double rms = finalInlierCount > 0
        ? std::sqrt(squaredErrorSum / finalInlierCount)
        : 0.0;

    if (finalInlierCount < options.minInliers) {
        // The final refit collapsed the consensus: fail closed rather than
        // report a pose that fewer correspondences support than required.
        return fail("no consensus");
    }

    result->pose.orientation = QuatFromRotation(bestR);
    result->pose.position = math::Vec3{
        static_cast<float>(bestT[0]),
        static_cast<float>(bestT[1]),
        static_cast<float>(bestT[2]),
    };
    result->inlierCount = finalInlierCount;
    result->inlierMask = std::move(inlierMask);
    result->rmsReprojectionPixels = static_cast<float>(rms);
    return true;
}

}  // namespace questlab::pose
