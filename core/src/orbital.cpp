#include "cscore/orbital.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

namespace {
constexpr double kPi = std::numbers::pi;
}

double hydrogenRadial(int n, int l, double r) noexcept {
  // R_nl = rho^l e^(-rho / 2) L_(n-l-1)^(2l+1)(rho), rho = 2 r / n.
  const double rho = 2.0 * r / n;
  const int k = n - l - 1;
  const double alpha = 2.0 * l + 1.0;
  double previous = 1.0, laguerre = 1.0;
  if (k >= 1) {
    laguerre = 1.0 + alpha - rho;
    for (int j = 1; j < k; ++j) {
      const double next = ((2.0 * j + 1.0 + alpha - rho) * laguerre - (j + alpha) * previous) / (j + 1.0);
      previous = laguerre;
      laguerre = next;
    }
  }
  return std::pow(rho, l) * std::exp(-0.5 * rho) * laguerre;
}

double legendre(int l, int m, double x) noexcept {
  double pmm = 1.0;
  const double s = std::sqrt(std::max(0.0, 1.0 - x * x));
  for (int i = 1; i <= m; ++i)
    pmm *= -(2.0 * i - 1.0) * s;
  if (l == m)
    return pmm;
  double pm1 = x * (2.0 * m + 1.0) * pmm;
  for (int ll = m + 2; ll <= l; ++ll) {
    const double next = (x * (2.0 * ll - 1.0) * pm1 - (ll + m - 1.0) * pmm) / (ll - m);
    pmm = pm1;
    pm1 = next;
  }
  return pm1;
}

void OrbitalShape::set(int n, int l, int m) {
  if (n == n_ && l == l_ && m == m_ && !radial_.empty())
    return;
  n_ = n;
  l_ = l;
  m_ = m;
  // |R| on a grid out to where it has all but vanished.
  const double reach = 4.0 * n * n + 12.0;
  const std::size_t count = 2048;
  dr_ = reach / static_cast<double>(count - 1);
  radial_.resize(count);
  std::vector<double> sign(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double value = hydrogenRadial(n, l, dr_ * static_cast<double>(i));
    radial_[i] = std::abs(value);
    sign[i] = value;
  }
  // Lobes lie between the radial nodes (sign changes).
  lobes_.clear();
  std::size_t begin = 0;
  auto close = [&](std::size_t end) {
    Lobe lobe;
    lobe.peak = begin;
    for (std::size_t i = begin; i < end; ++i)
      if (radial_[i] > radial_[lobe.peak])
        lobe.peak = i;
    lobe.end = end - 1;
    lobe.peakValue = radial_[lobe.peak];
    if (lobe.peakValue > 0.0)
      lobes_.push_back(lobe);
  };
  for (std::size_t i = 1; i < count; ++i) {
    if (sign[i] * sign[i - 1] < 0.0) {
      close(i);
      begin = i;
    }
  }
  close(count);
  // Largest |P_l^m| over the sphere.
  angularPeak_ = 0.0;
  for (int i = 0; i <= 720; ++i)
    angularPeak_ = std::max(angularPeak_, std::abs(legendre(l, m, std::cos(kPi * i / 720.0))));
}

double OrbitalShape::crossing(const Lobe& lobe, double value) const noexcept {
  // |R| falls monotonically from the lobe's peak to its end: bisect.
  if (value >= lobe.peakValue)
    return 0.0;
  if (radial_[lobe.end] >= value)
    return dr_ * static_cast<double>(lobe.end);
  std::size_t lo = lobe.peak, hi = lobe.end;
  while (hi - lo > 1) {
    const std::size_t mid = (lo + hi) / 2;
    (radial_[mid] >= value ? lo : hi) = mid;
  }
  const double a = radial_[lo], b = radial_[hi];
  return dr_ * (static_cast<double>(lo) + (a > b ? (a - value) / (a - b) : 0.0));
}

double OrbitalShape::radius(int shell, double level, double theta, double phi) const noexcept {
  if (lobes_.empty() || shell < 0 || shell >= shells())
    return 0.0;
  const double angular = std::abs(legendre(l_, m_, std::cos(theta)) * std::cos(m_ * phi)) / angularPeak_;
  if (angular <= level)
    return 0.0; // too close to an angular node: the surface does not reach this direction
  const Lobe& lobe = lobes_[static_cast<std::size_t>(shell)];
  const double full = crossing(lobes_.back(), level * lobes_.back().peakValue);
  return full > 0.0 ? crossing(lobe, level * lobe.peakValue / angular) / full : 0.0;
}

int OrbitalSource::autoOrbital(int note, int velocity) noexcept {
  // s, p, d, f as the note gets harder; the key turns the orbital.
  const int l = velocity <= 40 ? 0 : velocity <= 80 ? 1 : velocity <= 110 ? 2 : 3;
  const int m = note % (l + 1);
  for (std::size_t i = 0; i < kOrbitals.size(); ++i)
    if (kOrbitals[i].n == l + 1 && kOrbitals[i].l == l && kOrbitals[i].m == m)
      return static_cast<int>(i) + 1;
  // n = l + 1 has only m = 0 .. l; take the same l and m in the next shell.
  for (std::size_t i = 0; i < kOrbitals.size(); ++i)
    if (kOrbitals[i].l == l && kOrbitals[i].m == m)
      return static_cast<int>(i) + 1;
  return 1;
}

void OrbitalSource::birth(const Params& params, int note, int velocity, std::uint32_t) {
  chosen_ = params.orbital >= 1 && params.orbital <= static_cast<int>(kOrbitals.size()) ? params.orbital
                                                                                         : autoOrbital(note, velocity);
}

void OrbitalSource::build(const Params& params, int, double age, std::span<XY> table) {
  // A fixed choice follows the control; Auto keeps what the note was born with.
  if (params.orbital >= 1 && params.orbital <= static_cast<int>(kOrbitals.size()))
    chosen_ = params.orbital;
  const OrbitalId& id = kOrbitals[static_cast<std::size_t>(chosen_ - 1)];
  shape_.set(id.n, id.l, id.m);

  // Bloom: the surface starts at the lobe's core and grows out to its level.
  double level = std::clamp(params.level, 0.02, 0.9);
  if (params.bloomMs > 0.0 && age < params.bloomMs * 1e-3) {
    const double u = 1.0 - age / (params.bloomMs * 1e-3);
    level += (0.97 - level) * u * u;
  }

  const double spin = params.spin * age * kPi / 180.0;
  const double tilt = params.tilt * kPi / 180.0;
  const double cosTilt = std::cos(tilt), sinTilt = std::sin(tilt);
  const int shells = shape_.shells();
  constexpr int kPoints = 72;

  strokes_.clear();
  Stroke run;
  auto flush = [&] {
    if (run.size() >= 2)
      strokes_.push_back(run);
    run.clear();
  };
  auto add = [&](int shell, double theta, double phi) {
    const double r = shape_.radius(shell, level, theta, phi) * kSize;
    if (r <= 0.0) {
      flush(); // an angular node: the wire stops here
      return;
    }
    // Orbital frame: z is the orbital's axis. On screen the axis points up,
    // then leans toward the viewer by the tilt.
    const double a = phi + spin;
    const double x = r * std::sin(theta) * std::cos(a), y = r * std::sin(theta) * std::sin(a), z = r * std::cos(theta);
    run.push_back({x, z * cosTilt - y * sinTilt});
  };

  for (int shell = shells - 1; shell >= 0; --shell) {
    // Inner shells are small: half the wires.
    const int lines = std::max(2, std::clamp(params.lines, 2, 24) / (shell == shells - 1 ? 1 : 2));
    const int meridians = std::max(1, lines / 2), parallels = lines - meridians;
    for (int k = 0; k < meridians; ++k) { // great circles through the poles
      const double phi = kPi * k / meridians;
      for (int i = 0; i <= kPoints; ++i) {
        const double t = 2.0 * kPi * i / kPoints;
        if (t <= kPi)
          add(shell, t, phi);
        else
          add(shell, 2.0 * kPi - t, phi + kPi);
      }
      flush();
    }
    for (int j = 0; j < parallels; ++j) { // circles of latitude
      const double theta = kPi * (j + 0.5) / parallels;
      for (int i = 0; i <= kPoints; ++i)
        add(shell, theta, 2.0 * kPi * i / kPoints);
      flush();
    }
  }
  const Path path = strokesToPath(orderStrokes(std::move(strokes_), {}));
  strokes_.clear();
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs
