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

// Order 4 unordered quad vertices as TL, TR, BR, BL (y down): TL has the
// smallest x+y, BR the largest; of the other two, TR has the largest x-y.
bool OrderCorners(std::array<Point2, 4>* points) {
    if (points == nullptr) {
        return false;
    }
    std::array<Point2, 4>& p = *points;
    size_t topLeft = 0;
    size_t bottomRight = 0;
    for (size_t index = 1; index < 4U; ++index) {
        if (p[index].x + p[index].y < p[topLeft].x + p[topLeft].y) {
            topLeft = index;
        }
        if (p[index].x + p[index].y > p[bottomRight].x + p[bottomRight].y) {
            bottomRight = index;
        }
    }
    size_t rest[2] = {0U, 0U};
    size_t cursor = 0;
    for (size_t index = 0; index < 4U; ++index) {
        if (index != topLeft && index != bottomRight) {
            rest[cursor++] = index;
        }
    }
    const size_t topRight =
        (p[rest[0]].x - p[rest[0]].y) > (p[rest[1]].x - p[rest[1]].y)
            ? rest[0]
            : rest[1];
    const size_t bottomLeft = topRight == rest[0] ? rest[1] : rest[0];
    const std::array<Point2, 4> ordered = {
        p[topLeft], p[topRight], p[bottomRight], p[bottomLeft]};
    p = ordered;
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

}  // namespace

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
    const double aspect =
        static_cast<double>(boundsWidth) / boundsHeight;
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
    double homography[9];
    if (!SolveHomography(object, quad.corners.data(), homography, error)) {
        return false;
    }
    // A = K^-1 H maps the object plane (X, Y, 0) to normalized camera
    // coordinates: A = s [r1 r2 t].
    const double inverseFx = 1.0 / intrinsics.fx;
    const double inverseFy = 1.0 / intrinsics.fy;
    const double row0[3] = {
        (homography[0] - intrinsics.cx * homography[6]) * inverseFx,
        (homography[1] - intrinsics.cx * homography[7]) * inverseFx,
        (homography[2] - intrinsics.cx * homography[8]) * inverseFx,
    };
    const double row1[3] = {
        (homography[3] - intrinsics.cy * homography[6]) * inverseFy,
        (homography[4] - intrinsics.cy * homography[7]) * inverseFy,
        (homography[5] - intrinsics.cy * homography[8]) * inverseFy,
    };
    const double row2[3] = {
        homography[6], homography[7], homography[8]};
    const Vector3 first = {row0[0], row1[0], row2[0]};
    const Vector3 second = {row0[1], row1[1], row2[1]};
    const Vector3 third = {row0[2], row1[2], row2[2]};
    const double firstNorm = Norm(first);
    const double secondNorm = Norm(second);
    if (firstNorm < 1e-12 || secondNorm < 1e-12) {
        return fail("homography is degenerate");
    }
    // Column scale from the orthonormality constraint |r1| = |r2| = 1.
    double scale = 2.0 / (firstNorm + secondNorm);
    Vector3 axisX = {first.x * scale, first.y * scale, first.z * scale};
    Vector3 axisY = {second.x * scale, second.y * scale, second.z * scale};
    Vector3 translation = {
        third.x * scale, third.y * scale, third.z * scale};
    // The homography fixes one representative; choose the physical branch
    // with the screen in front of the camera.
    if (translation.z < 0.0) {
        axisX = {-axisX.x, -axisX.y, -axisX.z};
        axisY = {-axisY.x, -axisY.y, -axisY.z};
        translation = {-translation.x, -translation.y, -translation.z};
    }
    // Gram-Schmidt orthonormalization for noisy corners.
    axisX = Normalize(axisX);
    const double projection = Dot(axisX, axisY);
    axisY = Normalize(
        {axisY.x - projection * axisX.x, axisY.y - projection * axisX.y,
         axisY.z - projection * axisX.z});
    const Vector3 axisZ = Cross(axisX, axisY);
    const double rotation[3][3] = {
        {axisX.x, axisY.x, axisZ.x},
        {axisX.y, axisY.y, axisZ.y},
        {axisX.z, axisY.z, axisZ.z},
    };
    // Reprojection RMS of the 4 corners.
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
        const double imageX =
            intrinsics.fx * camera.x / camera.z + intrinsics.cx;
        const double imageY =
            intrinsics.fy * camera.y / camera.z + intrinsics.cy;
        const double deltaX = imageX - quad.corners[index].x;
        const double deltaY = imageY - quad.corners[index].y;
        squared += deltaX * deltaX + deltaY * deltaY;
    }
    if (reprojectionErrorPixels != nullptr) {
        *reprojectionErrorPixels =
            static_cast<float>(std::sqrt(squared / 4.0));
    }
    // Quaternion (x, y, z, w) from the rotation matrix trace, as in the
    // host implementation.
    const double trace =
        rotation[0][0] + rotation[1][1] + rotation[2][2];
    double quaternionW = std::sqrt(std::max(0.0, 1.0 + trace)) * 0.5;
    double quaternionX = 0.0;
    double quaternionY = 0.0;
    double quaternionZ = 0.0;
    if (quaternionW > 1e-6) {
        quaternionX =
            (rotation[2][1] - rotation[1][2]) / (4.0 * quaternionW);
        quaternionY =
            (rotation[0][2] - rotation[2][0]) / (4.0 * quaternionW);
        quaternionZ =
            (rotation[1][0] - rotation[0][1]) / (4.0 * quaternionW);
    } else {
        quaternionX = std::sqrt(
                          std::max(
                              0.0, 1.0 + rotation[0][0] - rotation[1][1] -
                                       rotation[2][2])) *
                      0.5;
        quaternionY = std::sqrt(
                          std::max(
                              0.0, 1.0 - rotation[0][0] + rotation[1][1] -
                                       rotation[2][2])) *
                      0.5;
        quaternionZ = std::sqrt(
                          std::max(
                              0.0, 1.0 - rotation[0][0] - rotation[1][1] +
                                       rotation[2][2])) *
                      0.5;
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
