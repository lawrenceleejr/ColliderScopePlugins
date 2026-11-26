// A camera for the 3D instruments. Model space has z up. The camera looks
// at the origin from `azimuth` degrees round the z axis and `elevation`
// degrees above the horizon; `perspective` runs from 0 (orthographic, as a
// drawing) to 1 (a close, wide lens).
#pragma once

#include <cmath>
#include <numbers>

#include "cscore/path.h"

namespace cs {

struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
};

struct Camera {
  double azimuth = 30.0;   // degrees
  double elevation = 25.0; // degrees
  double roll = 0.0;       // degrees, about the line of sight
  double perspective = 0.0; // 0-1
};

class View {
public:
  // `reach`: how far the model extends from the origin. The nearest point of
  // a model that size is drawn at its true size and the rest smaller, so
  // perspective never pushes the picture off the screen.
  explicit View(const Camera& camera, double reach = 1.0) noexcept
      : perspective_(camera.perspective), near_(1.0 - 0.4 * camera.perspective * reach) {
    const double k = std::numbers::pi / 180.0;
    ca_ = std::cos(camera.azimuth * k);
    sa_ = std::sin(camera.azimuth * k);
    ce_ = std::cos(camera.elevation * k);
    se_ = std::sin(camera.elevation * k);
    cr_ = std::cos(camera.roll * k);
    sr_ = std::sin(camera.roll * k);
  }

  // Screen position, and depth toward the viewer (larger is nearer).
  Vec3 project(Vec3 p) const noexcept {
    const double x = p.x * ca_ + p.y * sa_, y = -p.x * sa_ + p.y * ca_;
    const double depth = -y * ce_ + p.z * se_;
    double sx = x, sy = y * se_ + p.z * ce_;
    const double scale = near_ / (1.0 - 0.4 * perspective_ * depth); // nearer is larger
    sx *= scale;
    sy *= scale;
    return {sx * cr_ - sy * sr_, sx * sr_ + sy * cr_, depth};
  }

  // Depth of a direction (for telling which way a face points).
  double depthOf(Vec3 n) const noexcept { return -(-n.x * sa_ + n.y * ca_) * ce_ + n.z * se_; }

private:
  double ca_, sa_, ce_, se_, cr_, sr_, perspective_, near_;
};

} // namespace cs
