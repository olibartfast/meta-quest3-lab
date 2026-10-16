#include "screen_corners/screen_corners.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace questlab::corners {
namespace {

constexpr double kMinimumForegroundShare = 0.005;
constexpr double kMaximumForegroundShare = 0.80;
constexpr double kMinimumAspect = 1.1;
constexpr double kMaximumAspect = 2.6;
constexpr int kMinimumQuadPixels = 32;
constexpr double kSimplifyEpsilonShare = 0.02;

// 8-neighborhood directions, clockwise with y down: E, SE, S, SW, W, NW, N,
// NE. Shared by component labeling and Moore boundary tracing.
constexpr int kDirectionX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDirectionY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

int DirectionIndex(int deltaX, int deltaY) {
    for (int index = 0; index < 8; ++index) {
        if (kDirectionX[index] == deltaX && kDirectionY[index] == deltaY) {
            return index;
        }
    }
    return -1;
}

double Cross(const Point2& origin, const Point2& a, const Point2& b) {
    return (static_cast<double>(a.x) - origin.x) *
               (static_cast<double>(b.y) - origin.y) -
           (static_cast<double>(a.y) - origin.y) *
               (static_cast<double>(b.x) - origin.x);
}

double PolygonArea(const std::vector<Point2>& points) {
    if (points.size() < 3U) {
        return 0.0;
    }
    double twice = 0.0;
    for (size_t index = 0; index < points.size(); ++index) {
        const Point2& a = points[index];
        const Point2& b = points[(index + 1U) % points.size()];
        twice += static_cast<double>(a.x) * b.y -
                 static_cast<double>(b.x) * a.y;
    }
    return std::abs(twice) * 0.5;
}

double PolygonPerimeter(const std::vector<Point2>& points) {
    if (points.size() < 2U) {
        return 0.0;
    }
    double length = 0.0;
    for (size_t index = 0; index < points.size(); ++index) {
        const Point2& a = points[index];
        const Point2& b = points[(index + 1U) % points.size()];
        const double deltaX = static_cast<double>(b.x) - a.x;
        const double deltaY = static_cast<double>(b.y) - a.y;
        length += std::sqrt(deltaX * deltaX + deltaY * deltaY);
    }
    return length;
}

double PointSegmentDistance(
    const Point2& point, const Point2& first, const Point2& second) {
    const double deltaX = static_cast<double>(second.x) - first.x;
    const double deltaY = static_cast<double>(second.y) - first.y;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (lengthSquared <= 1e-12) {
        const double px = static_cast<double>(point.x) - first.x;
        const double py = static_cast<double>(point.y) - first.y;
        return std::sqrt(px * px + py * py);
    }
    const double px = static_cast<double>(point.x) - first.x;
    const double py = static_cast<double>(point.y) - first.y;
    double t = (px * deltaX + py * deltaY) / lengthSquared;
    t = std::clamp(t, 0.0, 1.0);
    const double closestX = first.x + t * deltaX;
    const double closestY = first.y + t * deltaY;
    const double dx = point.x - closestX;
    const double dy = point.y - closestY;
    return std::sqrt(dx * dx + dy * dy);
}

// Douglas-Peucker over the open polyline points[first..last]; marks kept
// indices in `keep` (the endpoints are kept by the caller).
void SimplifyPolyline(
    const std::vector<Point2>& points,
    size_t first,
    size_t last,
    double epsilon,
    std::vector<uint8_t>* keep) {
    std::vector<std::pair<size_t, size_t>> stack;
    stack.emplace_back(first, last);
    while (!stack.empty()) {
        const std::pair<size_t, size_t> range = stack.back();
        stack.pop_back();
        if (range.second <= range.first + 1U) {
            continue;
        }
        double farthest = -1.0;
        size_t farthestIndex = range.first;
        for (size_t index = range.first + 1U; index < range.second; ++index) {
            const double distance =
                PointSegmentDistance(
                    points[index], points[range.first],
                    points[range.second]);
            if (distance > farthest) {
                farthest = distance;
                farthestIndex = index;
            }
        }
        if (farthest > epsilon) {
            (*keep)[farthestIndex] = 1U;
            stack.emplace_back(range.first, farthestIndex);
            stack.emplace_back(farthestIndex, range.second);
        }
    }
}

// approxPolyDP for a closed contour: cut the loop at a stable anchor and
// its farthest point, simplify both halves, and rejoin without duplicating
// the cut points.
std::vector<Point2> SimplifyClosed(
    const std::vector<Point2>& points, double epsilon) {
    const size_t count = points.size();
    if (count < 4U) {
        return points;
    }
    size_t anchor = 0;
    for (size_t index = 1; index < count; ++index) {
        const double candidate =
            static_cast<double>(points[index].x) + points[index].y;
        const double current =
            static_cast<double>(points[anchor].x) + points[anchor].y;
        if (candidate < current) {
            anchor = index;
        }
    }
    size_t opposite = anchor;
    double farthest = -1.0;
    for (size_t index = 0; index < count; ++index) {
        const double deltaX =
            static_cast<double>(points[index].x) - points[anchor].x;
        const double deltaY =
            static_cast<double>(points[index].y) - points[anchor].y;
        const double distance = deltaX * deltaX + deltaY * deltaY;
        if (distance > farthest) {
            farthest = distance;
            opposite = index;
        }
    }
    std::vector<Point2> head;
    for (size_t index = anchor;; index = (index + 1U) % count) {
        head.push_back(points[index]);
        if (index == opposite) {
            break;
        }
    }
    std::vector<Point2> tail;
    for (size_t index = opposite;; index = (index + 1U) % count) {
        tail.push_back(points[index]);
        if (index == anchor) {
            break;
        }
    }
    std::vector<uint8_t> keepHead(head.size(), 0U);
    keepHead.front() = 1U;
    keepHead.back() = 1U;
    SimplifyPolyline(head, 0U, head.size() - 1U, epsilon, &keepHead);
    std::vector<uint8_t> keepTail(tail.size(), 0U);
    keepTail.front() = 1U;
    keepTail.back() = 1U;
    SimplifyPolyline(tail, 0U, tail.size() - 1U, epsilon, &keepTail);
    std::vector<Point2> simplified;
    // head.back() == tail.front() == opposite, tail.back() == anchor.
    for (size_t index = 0; index + 1U < head.size(); ++index) {
        if (keepHead[index] != 0U) {
            simplified.push_back(head[index]);
        }
    }
    for (size_t index = 0; index + 1U < tail.size(); ++index) {
        if (keepTail[index] != 0U) {
            simplified.push_back(tail[index]);
        }
    }
    return simplified;
}

bool IsConvex(const std::vector<Point2>& polygon) {
    if (polygon.size() < 3U) {
        return false;
    }
    double sign = 0.0;
    for (size_t index = 0; index < polygon.size(); ++index) {
        const double cross = Cross(
            polygon[index], polygon[(index + 1U) % polygon.size()],
            polygon[(index + 2U) % polygon.size()]);
        if (std::abs(cross) <= 1e-9) {
            continue;
        }
        const double candidate = cross > 0.0 ? 1.0 : -1.0;
        if (sign == 0.0) {
            sign = candidate;
        } else if (candidate != sign) {
            return false;
        }
    }
    return sign != 0.0;
}

// Order 4 unordered quad vertices as TL, TR, BR, BL (y down) by polar
// angle around the centroid: robust under head roll and strong
// perspective, where the x+y / x-y sums mislabel corners. The start index
// is rotated to the smallest x+y (image TL) and the winding is kept
// clockwise (positive signed area with y down).
bool OrderCorners(std::array<Point2, 4>* points) {
    if (points == nullptr) {
        return false;
    }
    std::array<Point2, 4>& p = *points;
    double centroidX = 0.0;
    double centroidY = 0.0;
    for (const Point2& point : p) {
        centroidX += point.x;
        centroidY += point.y;
    }
    centroidX *= 0.25;
    centroidY *= 0.25;
    std::array<double, 4> angles{};
    for (size_t index = 0; index < 4U; ++index) {
        angles[index] = std::atan2(
            static_cast<double>(p[index].y) - centroidY,
            static_cast<double>(p[index].x) - centroidX);
    }
    std::array<size_t, 4> order = {0U, 1U, 2U, 3U};
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return angles[a] < angles[b];
    });
    std::array<Point2, 4> sorted = {
        p[order[0]], p[order[1]], p[order[2]], p[order[3]]};
    // With y down, ascending atan2 order is TL, TR, BR, BL when the
    // quad is upright; rotate so the smallest x+y (image TL) is first.
    size_t topLeft = 0;
    for (size_t index = 1; index < 4U; ++index) {
        if (sorted[index].x + sorted[index].y <
            sorted[topLeft].x + sorted[topLeft].y) {
            topLeft = index;
        }
    }
    std::array<Point2, 4> rotated{};
    for (size_t index = 0; index < 4U; ++index) {
        rotated[index] = sorted[(topLeft + index) % 4U];
    }
    // Keep TL, TR, BR, BL (clockwise with y down, positive signed area).
    double twice = 0.0;
    for (size_t index = 0; index < 4U; ++index) {
        const Point2& a = rotated[index];
        const Point2& b = rotated[(index + 1U) % 4U];
        twice += static_cast<double>(a.x) * b.y -
                 static_cast<double>(b.x) * a.y;
    }
    if (twice < 0.0) {
        // Counter-clockwise: keep TL first, reverse the other three.
        const std::array<Point2, 4> fixed = {
            rotated[0], rotated[3], rotated[2], rotated[1]};
        rotated = fixed;
    }
    p = rotated;
    return true;
}

// Moore boundary tracing of the component containing (startX, startY),
// which must be the topmost-leftmost pixel of its component (its west
// neighbor is background, giving the initial backtrack direction). The
// trace stops when the state after the first step (pixel + predecessor)
// repeats, which is when the boundary has wrapped around the component.
std::vector<Point2> TraceBoundary(
    const uint8_t* mask, int width, int height, int startX, int startY) {
    const auto foreground = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
               mask[static_cast<size_t>(y) * width + x] != 0U;
    };
    std::vector<Point2> points;
    const size_t maximumSteps =
        static_cast<size_t>(width) * height * 4U + 16U;
    int currentX = startX;
    int currentY = startY;
    int previousX = startX - 1;
    int previousY = startY;
    // Scans clockwise from the backtrack direction for the first
    // foreground neighbor and steps there.
    const auto step = [&]() {
        const int back = DirectionIndex(
            previousX - currentX, previousY - currentY);
        if (back < 0) {
            return false;
        }
        for (int offset = 1; offset <= 8; ++offset) {
            const int index = (back + offset) % 8;
            const int nextX = currentX + kDirectionX[index];
            const int nextY = currentY + kDirectionY[index];
            if (foreground(nextX, nextY)) {
                previousX = currentX;
                previousY = currentY;
                currentX = nextX;
                currentY = nextY;
                return true;
            }
        }
        return false;
    };
    points.push_back(
        {static_cast<float>(startX), static_cast<float>(startY)});
    if (!step()) {
        return points;  // isolated pixel
    }
    const int firstStepX = currentX;
    const int firstStepY = currentY;
    const int firstStepPreviousX = previousX;
    const int firstStepPreviousY = previousY;
    while (points.size() < maximumSteps) {
        points.push_back(
            {static_cast<float>(currentX), static_cast<float>(currentY)});
        if (!step()) {
            break;
        }
        if (currentX == firstStepX && currentY == firstStepY &&
            previousX == firstStepPreviousX &&
            previousY == firstStepPreviousY) {
            break;  // wrapped around
        }
    }
    if (points.size() > 1U && points.front().x == points.back().x &&
        points.front().y == points.back().y) {
        points.pop_back();
    }
    return points;
}

// Largest 8-connected foreground component: returns its pixel count and
// the topmost-leftmost pixel (the Moore trace start).
bool LargestComponent(
    const uint8_t* mask,
    int width,
    int height,
    int64_t* pixelCount,
    int* startX,
    int* startY) {
    const size_t total = static_cast<size_t>(width) * height;
    std::vector<uint8_t> visited(total, 0U);
    std::vector<int32_t> stack;
    int64_t bestCount = 0;
    int bestX = -1;
    int bestY = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            if (mask[index] == 0U || visited[index] != 0U) {
                continue;
            }
            visited[index] = 1U;
            stack.clear();
            stack.push_back(static_cast<int32_t>(index));
            int64_t count = 0;
            int componentStartX = x;
            int componentStartY = y;
            while (!stack.empty()) {
                const int32_t current = stack.back();
                stack.pop_back();
                ++count;
                const int currentX =
                    current % static_cast<int32_t>(width);
                const int currentY =
                    current / static_cast<int32_t>(width);
                if (currentY < componentStartY ||
                    (currentY == componentStartY &&
                     currentX < componentStartX)) {
                    componentStartX = currentX;
                    componentStartY = currentY;
                }
                for (int direction = 0; direction < 8; ++direction) {
                    const int nextX = currentX + kDirectionX[direction];
                    const int nextY = currentY + kDirectionY[direction];
                    if (nextX < 0 || nextY < 0 || nextX >= width ||
                        nextY >= height) {
                        continue;
                    }
                    const size_t nextIndex =
                        static_cast<size_t>(nextY) * width + nextX;
                    if (mask[nextIndex] == 0U || visited[nextIndex] != 0U) {
                        continue;
                    }
                    visited[nextIndex] = 1U;
                    stack.push_back(static_cast<int32_t>(nextIndex));
                }
            }
            if (count > bestCount) {
                bestCount = count;
                bestX = componentStartX;
                bestY = componentStartY;
            }
        }
    }
    *pixelCount = bestCount;
    *startX = bestX;
    *startY = bestY;
    return bestCount > 0;
}

// Solve the 8x8 linear system of the planar DLT (h33 = 1) by Gaussian
// elimination with partial pivoting.
bool Solve8x8(double matrix[8][9], double solution[8]) {
    for (int column = 0; column < 8; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 8; ++row) {
            if (std::abs(matrix[row][column]) >
                std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < 1e-12) {
            return false;
        }
        if (pivot != column) {
            for (int entry = column; entry < 9; ++entry) {
                std::swap(matrix[column][entry], matrix[pivot][entry]);
            }
        }
        const double divisor = matrix[column][column];
        for (int entry = column; entry < 9; ++entry) {
            matrix[column][entry] /= divisor;
        }
        for (int row = 0; row < 8; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[row][column];
            if (factor == 0.0) {
                continue;
            }
            for (int entry = column; entry < 9; ++entry) {
                matrix[row][entry] -= factor * matrix[column][entry];
            }
        }
    }
    for (int row = 0; row < 8; ++row) {
        solution[row] = matrix[row][8];
    }
    return true;
}

bool SolveHomography(
    const Point2 object[4],
    const Point2 image[4],
    double homography[9],
    std::string* error) {
    double matrix[8][9];
    for (int index = 0; index < 4; ++index) {
        const double objectX = object[index].x;
        const double objectY = object[index].y;
        const double imageX = image[index].x;
        const double imageY = image[index].y;
        double* first = matrix[index * 2];
        double* second = matrix[index * 2 + 1];
        first[0] = objectX;
        first[1] = objectY;
        first[2] = 1.0;
        first[3] = 0.0;
        first[4] = 0.0;
        first[5] = 0.0;
        first[6] = -imageX * objectX;
        first[7] = -imageX * objectY;
        first[8] = imageX;
        second[0] = 0.0;
        second[1] = 0.0;
        second[2] = 0.0;
        second[3] = objectX;
        second[4] = objectY;
        second[5] = 1.0;
        second[6] = -imageY * objectX;
        second[7] = -imageY * objectY;
        second[8] = imageY;
    }
    double solution[8] = {0.0};
    if (!Solve8x8(matrix, solution)) {
        if (error != nullptr) {
            *error = "homography solve failed";
        }
        return false;
    }
    for (int index = 0; index < 8; ++index) {
        homography[index] = solution[index];
    }
    homography[8] = 1.0;
    return true;
}

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

double Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

double Norm(const Vector3& value) {
    return std::sqrt(Dot(value, value));
}

Vector3 Normalize(const Vector3& value) {
    const double norm = Norm(value);
    if (!(norm > 1e-12)) {
        return {0.0, 0.0, 0.0};
    }
    return {value.x / norm, value.y / norm, value.z / norm};
}

// Forward Brown-Conrady distortion on normalized coordinates, matching
// libs/detection_projection::DistortNormalized (k1, k2, k3, p1, p2).
void DistortNormalized(
    const Intrinsics& intrinsics, double x, double y,
    double* distortedX, double* distortedY) {
    const double radius2 = x * x + y * y;
    const double radius4 = radius2 * radius2;
    const double radius6 = radius4 * radius2;
    const double radial =
        1.0 + static_cast<double>(intrinsics.distortion[0]) * radius2 +
        static_cast<double>(intrinsics.distortion[1]) * radius4 +
        static_cast<double>(intrinsics.distortion[2]) * radius6;
    const double p1 = static_cast<double>(intrinsics.distortion[3]);
    const double p2 = static_cast<double>(intrinsics.distortion[4]);
    *distortedX = x * radial + 2.0 * p1 * x * y + p2 * (radius2 + 2.0 * x * x);
    *distortedY = y * radial + p1 * (radius2 + 2.0 * y * y) + 2.0 * p2 * x * y;
}

// Invert the forward model for one distorted pixel (including skew) to
// undistorted normalized coordinates, mirroring UnprojectPixel's Newton
// solve in libs/detection_projection.
bool UndistortToNormalized(
    const Intrinsics& intrinsics,
    double pixelX,
    double pixelY,
    double* normalX,
    double* normalY) {
    const double distortedY =
        (pixelY - static_cast<double>(intrinsics.cy)) /
        static_cast<double>(intrinsics.fy);
    const double distortedX =
        (pixelX - static_cast<double>(intrinsics.cx) -
         static_cast<double>(intrinsics.skew) * distortedY) /
        static_cast<double>(intrinsics.fx);
    double x = distortedX;
    double y = distortedY;
    constexpr int kIterations = 12;
    constexpr double kTolerance = 1.0e-7;
    constexpr double kStep = 1.0e-4;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        double predictedX = 0.0;
        double predictedY = 0.0;
        DistortNormalized(intrinsics, x, y, &predictedX, &predictedY);
        const double errorX = predictedX - distortedX;
        const double errorY = predictedY - distortedY;
        if (std::max(std::abs(errorX), std::abs(errorY)) <= kTolerance) {
            break;
        }
        double xStepX = 0.0;
        double xStepY = 0.0;
        double yStepX = 0.0;
        double yStepY = 0.0;
        DistortNormalized(intrinsics, x + kStep, y, &xStepX, &xStepY);
        DistortNormalized(intrinsics, x, y + kStep, &yStepX, &yStepY);
        const double j00 = (xStepX - predictedX) / kStep;
        const double j10 = (xStepY - predictedY) / kStep;
        const double j01 = (yStepX - predictedX) / kStep;
        const double j11 = (yStepY - predictedY) / kStep;
        const double determinant = j00 * j11 - j01 * j10;
        if (!std::isfinite(determinant) ||
            std::abs(determinant) < 1.0e-9) {
            return false;
        }
        x -= (j11 * errorX - j01 * errorY) / determinant;
        y -= (-j10 * errorX + j00 * errorY) / determinant;
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return false;
        }
    }
    *normalX = x;
    *normalY = y;
    return std::isfinite(x) && std::isfinite(y);
}

void ProjectNormalizedToPixel(
    const Intrinsics& intrinsics,
    double normalX,
    double normalY,
    double* pixelX,
    double* pixelY) {
    double distortedX = 0.0;
    double distortedY = 0.0;
    DistortNormalized(intrinsics, normalX, normalY, &distortedX, &distortedY);
    *pixelX = static_cast<double>(intrinsics.fx) * distortedX +
              static_cast<double>(intrinsics.skew) * distortedY +
              static_cast<double>(intrinsics.cx);
    *pixelY = static_cast<double>(intrinsics.fy) * distortedY +
              static_cast<double>(intrinsics.cy);
}

}  // namespace

bool SymmetricOrthonormalize(
    const double first[3],
    const double second[3],
    double axisX[3],
    double axisY[3],
    double axisZ[3]) {
    if (first == nullptr || second == nullptr || axisX == nullptr ||
        axisY == nullptr || axisZ == nullptr) {
        return false;
    }
    Vector3 a = Normalize({first[0], first[1], first[2]});
    Vector3 b = Normalize({second[0], second[1], second[2]});
    if (Norm(a) < 1e-12 || Norm(b) < 1e-12) {
        return false;
    }
    const Vector3 sum = {a.x + b.x, a.y + b.y, a.z + b.z};
    if (Norm(sum) < 1e-9) {
        return false;  // antiparallel columns
    }
    const Vector3 bisector = Normalize(sum);
    const Vector3 normal = Normalize(Cross(a, b));
    if (Norm(normal) < 1e-12) {
        return false;  // parallel columns
    }
    Vector3 inPlane = Cross(normal, bisector);
    // inPlane ~= b - a direction (leans toward b); negate if it leans
    // toward a so X stays near the first column and Y near the second.
    if (Dot(inPlane, a) > 0.0) {
        inPlane = {-inPlane.x, -inPlane.y, -inPlane.z};
    }
    constexpr double kHalf = 1.0 / 1.4142135623730951;  // 1/sqrt(2)
    Vector3 x = {
        kHalf * (bisector.x - inPlane.x),
        kHalf * (bisector.y - inPlane.y),
        kHalf * (bisector.z - inPlane.z),
    };
    Vector3 y = {
        kHalf * (bisector.x + inPlane.x),
        kHalf * (bisector.y + inPlane.y),
        kHalf * (bisector.z + inPlane.z),
    };
    x = Normalize(x);
    y = Normalize(y);
    if (Norm(x) < 1e-12 || Norm(y) < 1e-12) {
        return false;
    }
    const Vector3 z = Normalize(Cross(x, y));
    axisX[0] = x.x;
    axisX[1] = x.y;
    axisX[2] = x.z;
    axisY[0] = y.x;
    axisY[1] = y.y;
    axisY[2] = y.z;
    axisZ[0] = z.x;
    axisZ[1] = z.y;
    axisZ[2] = z.z;
    return true;
}

bool FindScreenQuadFromMask(
    const uint8_t* mask,
    int width,
    int height,
    Quad* quad,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (mask == nullptr || quad == nullptr || width < 64 || height < 64) {
        return fail("invalid mask or output");
    }
    const double imageArea =
        static_cast<double>(width) * static_cast<double>(height);
    int64_t foregroundArea = 0;
    for (size_t index = 0;
         index < static_cast<size_t>(width) * height; ++index) {
        if (mask[index] != 0U) {
            ++foregroundArea;
        }
    }
    if (foregroundArea < kMinimumForegroundShare * imageArea ||
        foregroundArea > kMaximumForegroundShare * imageArea) {
        return fail("mask foreground outside plausible screen share");
    }
    int64_t componentPixels = 0;
    int startX = 0;
    int startY = 0;
    if (!LargestComponent(
            mask, width, height, &componentPixels, &startX, &startY)) {
        return fail("no mask contour");
    }
    const std::vector<Point2> boundary =
        TraceBoundary(mask, width, height, startX, startY);
    if (boundary.size() < 4U) {
        return fail("mask contour is degenerate");
    }
    const std::vector<Point2> simplified = SimplifyClosed(
        boundary, kSimplifyEpsilonShare * PolygonPerimeter(boundary));
    if (simplified.size() != 4U) {
        return fail("mask contour is not quadrilateral");
    }
    if (!IsConvex(simplified)) {
        return fail("mask contour is not convex");
    }
    float minimumX = simplified[0].x;
    float maximumX = simplified[0].x;
    float minimumY = simplified[0].y;
    float maximumY = simplified[0].y;
    for (const Point2& point : simplified) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    const int boundsWidth =
        static_cast<int>(maximumX - minimumX) + 1;
    const int boundsHeight =
        static_cast<int>(maximumY - minimumY) + 1;
    if (boundsWidth < kMinimumQuadPixels ||
        boundsHeight < kMinimumQuadPixels) {
        return fail("mask quad below minimum size");
    }
    // A screen clipped by the frame edge still yields a clean convex quad
    // whose pose is wrong but looks valid: refuse quads touching the border.
    constexpr float kBorderMarginPixels = 3.0F;
    if (minimumX <= kBorderMarginPixels || minimumY <= kBorderMarginPixels ||
        maximumX >= static_cast<float>(width) - 1.0F - kBorderMarginPixels ||
        maximumY >= static_cast<float>(height) - 1.0F - kBorderMarginPixels) {
        return fail("mask quad touches the image border");
    }
    // Gate on the longer/shorter edge ratio so portrait displays (tablets,
    // doors, frames) pass the same window as landscape ones; the old
    // width/height gate rejected any rolled or portrait screen.
    const double longer =
        static_cast<double>(std::max(boundsWidth, boundsHeight));
    const double shorter =
        static_cast<double>(std::max(1, std::min(boundsWidth, boundsHeight)));
    const double aspect = longer / shorter;
    if (aspect < kMinimumAspect || aspect > kMaximumAspect) {
        return fail("mask quad outside display aspect window");
    }
    std::array<Point2, 4> ordered = {
        simplified[0], simplified[1], simplified[2], simplified[3]};
    if (!OrderCorners(&ordered)) {
        return fail("corner ordering failed");
    }
    quad->corners = ordered;
    quad->score = static_cast<float>(
        PolygonArea({ordered.begin(), ordered.end()}) /
        (foregroundArea > 0 ? static_cast<double>(foregroundArea) : 1.0));
    return true;
}

bool IsCornerPoseReliable(const Quad& quad, float reprojectionPixels) {
    if (!(reprojectionPixels >= 0.0F) ||
        reprojectionPixels > kMaxCornerReprojectionPixels) {
        return false;
    }
    if (quad.score < kMinimumQuadRectangularity) {
        return false;
    }
    const auto diagonal = [](const Point2& a, const Point2& b) {
        const double dx = static_cast<double>(a.x) - b.x;
        const double dy = static_cast<double>(a.y) - b.y;
        return std::sqrt(dx * dx + dy * dy);
    };
    const double extent =
        0.5 *
        (diagonal(quad.corners[0], quad.corners[2]) +
         diagonal(quad.corners[1], quad.corners[3]));
    if (!(extent > 1e-6)) {
        return false;
    }
    return static_cast<double>(reprojectionPixels) <=
           static_cast<double>(kMaxRelativeCornerReprojection) * extent;
}

bool SolveQuadPose(
    const Quad& quad,
    const Intrinsics& intrinsics,
    float monitorWidthMeters,
    float monitorHeightMeters,
    Pose* pose,
    float* reprojectionErrorPixels,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (pose == nullptr || monitorWidthMeters <= 0.0F ||
        monitorHeightMeters <= 0.0F || intrinsics.fx <= 0.0F ||
        intrinsics.fy <= 0.0F) {
        return fail("invalid pose input");
    }
    for (const Point2& corner : quad.corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) {
            return fail("non-finite corner");
        }
    }
    // Object frame: +X right, +Y up, origin at screen centre. Image TL
    // pairs with object top-left (-W/2, +H/2) per the header contract.
    const float halfWidth = monitorWidthMeters * 0.5F;
    const float halfHeight = monitorHeightMeters * 0.5F;
    const Point2 object[4] = {
        {-halfWidth, halfHeight},
        {halfWidth, halfHeight},
        {halfWidth, -halfHeight},
        {-halfWidth, -halfHeight},
    };
    for (const float coefficient : intrinsics.distortion) {
        if (!std::isfinite(coefficient)) {
            return fail("non-finite distortion");
        }
    }
    if (!std::isfinite(intrinsics.skew)) {
        return fail("non-finite skew");
    }
    // Undistort first: the PnP homography is solved in normalized
    // coordinates (K = I) so it shares the Brown-Conrady + skew model with
    // the box-ray UnprojectPixel path instead of a pinhole approximation.
    Point2 normalized[4];
    for (int index = 0; index < 4; ++index) {
        double normalX = 0.0;
        double normalY = 0.0;
        if (!UndistortToNormalized(
                intrinsics, quad.corners[index].x, quad.corners[index].y,
                &normalX, &normalY)) {
            return fail("corner undistortion failed");
        }
        normalized[index] = {
            static_cast<float>(normalX), static_cast<float>(normalY)};
    }
    double homography[9];
    if (!SolveHomography(object, normalized, homography, error)) {
        return false;
    }
    // With K = I the homography is directly s [r1 r2 t].
    const Vector3 first = {homography[0], homography[3], homography[6]};
    const Vector3 second = {homography[1], homography[4], homography[7]};
    const Vector3 third = {homography[2], homography[5], homography[8]};
    const double firstNorm = Norm(first);
    const double secondNorm = Norm(second);
    if (firstNorm < 1e-12 || secondNorm < 1e-12) {
        return fail("homography is degenerate");
    }
    // Column scale from the orthonormality constraint |r1| = |r2| = 1.
    // It scales the translation; the rotation axes below are unit by
    // construction.
    const double scale = 2.0 / (firstNorm + secondNorm);
    Vector3 translation = {
        third.x * scale, third.y * scale, third.z * scale};
    const double firstArray[3] = {first.x, first.y, first.z};
    const double secondArray[3] = {second.x, second.y, second.z};
    double axisXArray[3] = {0.0, 0.0, 0.0};
    double axisYArray[3] = {0.0, 0.0, 0.0};
    double axisZArray[3] = {0.0, 0.0, 0.0};
    if (!SymmetricOrthonormalize(
            firstArray, secondArray, axisXArray, axisYArray, axisZArray)) {
        return fail("homography is degenerate");
    }
    Vector3 axisX = {axisXArray[0], axisXArray[1], axisXArray[2]};
    Vector3 axisY = {axisYArray[0], axisYArray[1], axisYArray[2]};
    Vector3 axisZ = {axisZArray[0], axisZArray[1], axisZArray[2]};
    // The homography fixes one representative; choose the physical branch
    // with the screen in front of the camera. Only X, Y and t flip:
    // (-X)x(-Y) = XxY, so Z is unchanged and det(R) stays +1.
    if (translation.z < 0.0) {
        axisX = {-axisX.x, -axisX.y, -axisX.z};
        axisY = {-axisY.x, -axisY.y, -axisY.z};
        translation = {-translation.x, -translation.y, -translation.z};
    }
    const double rotation[3][3] = {
        {axisX.x, axisY.x, axisZ.x},
        {axisX.y, axisY.y, axisZ.y},
        {axisX.z, axisY.z, axisZ.z},
    };
    // The plane normal (object +Z in camera) must face the camera and not
    // be grazing. This catches mirrored pairings and edge-on views, not
    // the planar two-fold (IPPE) flip: both IPPE solutions face the camera,
    // so that ambiguity stays open (see roadmap).
    {
        const double range = Norm(translation);
        const double facing = Dot(axisZ, translation);
        if (!(range > 1e-9)) {
            return fail("translation is degenerate");
        }
        if (!(facing < 0.0)) {
            return fail("plane normal faces away from the camera");
        }
        constexpr double kGrazingCosine = 0.0872;  // cos(85 deg)
        if (-facing / range < kGrazingCosine) {
            return fail("plane is at grazing incidence");
        }
    }
    // Reprojection RMS through the full distort+K model. The corner pairing
    // stays image TL-first (no rotation search): with 4 points every cyclic
    // rotation fits exactly, so a best-reproj search cannot tell upright
    // from upside-down and flips near-exact poses 180 deg. Roll beyond ~45
    // deg remains open work (gravity-primed start or IMU).
    double squared = 0.0;
    for (int index = 0; index < 4; ++index) {
        const Vector3 camera = {
            rotation[0][0] * object[index].x +
                rotation[0][1] * object[index].y + translation.x,
            rotation[1][0] * object[index].x +
                rotation[1][1] * object[index].y + translation.y,
            rotation[2][0] * object[index].x +
                rotation[2][1] * object[index].y + translation.z,
        };
        if (!(camera.z > 1e-6)) {
            return fail("corner projects behind the camera");
        }
        double imageX = 0.0;
        double imageY = 0.0;
        ProjectNormalizedToPixel(
            intrinsics, camera.x / camera.z, camera.y / camera.z, &imageX,
            &imageY);
        const double deltaX = imageX - quad.corners[index].x;
        const double deltaY = imageY - quad.corners[index].y;
        squared += deltaX * deltaX + deltaY * deltaY;
    }
    if (reprojectionErrorPixels != nullptr) {
        *reprojectionErrorPixels =
            static_cast<float>(std::sqrt(squared / 4.0));
    }
    // Quaternion (x, y, z, w) via Shepperd's method: branch on the
    // largest of trace, R00, R11, R22. The screen convention sits at
    // trace = -1 (w ~= 0 nominal, not an edge case), so the old
    // trace-or-fallback lost all signs near 180 deg and mirrored yaw.
    const double trace =
        rotation[0][0] + rotation[1][1] + rotation[2][2];
    double quaternionX = 0.0;
    double quaternionY = 0.0;
    double quaternionZ = 0.0;
    double quaternionW = 0.0;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        quaternionW = 0.25 * s;
        quaternionX = (rotation[2][1] - rotation[1][2]) / s;
        quaternionY = (rotation[0][2] - rotation[2][0]) / s;
        quaternionZ = (rotation[1][0] - rotation[0][1]) / s;
    } else if (
        rotation[0][0] > rotation[1][1] && rotation[0][0] > rotation[2][2]) {
        const double s =
            std::sqrt(1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]) *
            2.0;
        quaternionW = (rotation[2][1] - rotation[1][2]) / s;
        quaternionX = 0.25 * s;
        quaternionY = (rotation[0][1] + rotation[1][0]) / s;
        quaternionZ = (rotation[0][2] + rotation[2][0]) / s;
    } else if (rotation[1][1] > rotation[2][2]) {
        const double s =
            std::sqrt(1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]) *
            2.0;
        quaternionW = (rotation[0][2] - rotation[2][0]) / s;
        quaternionX = (rotation[0][1] + rotation[1][0]) / s;
        quaternionY = 0.25 * s;
        quaternionZ = (rotation[1][2] + rotation[2][1]) / s;
    } else {
        const double s =
            std::sqrt(1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]) *
            2.0;
        quaternionW = (rotation[1][0] - rotation[0][1]) / s;
        quaternionX = (rotation[0][2] + rotation[2][0]) / s;
        quaternionY = (rotation[1][2] + rotation[2][1]) / s;
        quaternionZ = 0.25 * s;
    }
    const double norm = std::sqrt(
        quaternionX * quaternionX + quaternionY * quaternionY +
        quaternionZ * quaternionZ + quaternionW * quaternionW);
    if (!(norm > 1e-9)) {
        return fail("rotation conversion failed");
    }
    pose->quat = {
        static_cast<float>(quaternionX / norm),
        static_cast<float>(quaternionY / norm),
        static_cast<float>(quaternionZ / norm),
        static_cast<float>(quaternionW / norm),
    };
    pose->position = {
        static_cast<float>(translation.x),
        static_cast<float>(translation.y),
        static_cast<float>(translation.z),
    };
    return true;
}

}  // namespace questlab::corners
