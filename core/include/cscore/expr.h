// A small expression language for functions of x, y and t, as typed into
// the Function instrument:   sin(3*r - 2*t) / (1 + r^2)
//
//   numbers      1  2.5  1e-3
//   variables    x  y  t (seconds since the note began)
//                r = sqrt(x^2 + y^2)   theta = atan2(y, x)   pi   e
//   operators    + - * / ^   and unary minus; ^ binds tightest and to the right
//   functions    sin cos tan asin acos atan sinh cosh tanh exp log ln sqrt abs sign floor
//                sinc(u) = sin(u) / u   gauss(u) = exp(-u^2)
//                atan2(a, b)  min(a, b)  max(a, b)  pow(a, b)  mod(a, b)
//
// Compiled once to a short program; evaluating it allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cs {

class Expression {
public:
  // Returns false, with a message, if the source does not parse.
  bool compile(std::string_view source, std::string* error = nullptr);
  bool valid() const noexcept { return !ops_.empty(); }
  // Whether the value changes with time.
  bool usesTime() const noexcept { return usesTime_; }
  // The value at (x, y, t). Anything not finite comes back as 0.
  double eval(double x, double y, double t) const noexcept;

private:
  struct Op {
    std::uint8_t code = 0;
    double value = 0.0;
  };
  std::vector<Op> ops_;
  bool usesTime_ = false;
  friend class ExpressionParser;
};

} // namespace cs
