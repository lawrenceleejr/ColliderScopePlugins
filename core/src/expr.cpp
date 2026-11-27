#include "cscore/expr.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace cs {

namespace {
enum Code : std::uint8_t {
  kNumber, kX, kY, kT, kR, kTheta,
  kAdd, kSub, kMul, kDiv, kPow, kNeg,
  kSin, kCos, kTan, kAsin, kAcos, kAtan, kSinh, kCosh, kTanh, kExp, kLog, kSqrt, kAbs, kSign, kFloor, kSinc, kGauss,
  kAtan2, kMin, kMax, kMod,
};
struct Function {
  const char* name;
  Code code;
  int arguments;
};
constexpr Function kFunctions[] = {
    {"sin", kSin, 1},   {"cos", kCos, 1},     {"tan", kTan, 1},     {"asin", kAsin, 1},   {"acos", kAcos, 1},
    {"atan", kAtan, 1}, {"sinh", kSinh, 1},   {"cosh", kCosh, 1},   {"tanh", kTanh, 1},   {"exp", kExp, 1},
    {"log", kLog, 1},   {"ln", kLog, 1},      {"sqrt", kSqrt, 1},   {"abs", kAbs, 1},     {"sign", kSign, 1},
    {"floor", kFloor, 1}, {"sinc", kSinc, 1}, {"gauss", kGauss, 1}, {"atan2", kAtan2, 2}, {"min", kMin, 2},
    {"max", kMax, 2},   {"pow", kPow, 2},     {"mod", kMod, 2},
};
} // namespace

class ExpressionParser {
public:
  ExpressionParser(std::string_view source, Expression& out) : s_(source), out_(out) {}

  bool run(std::string* error) {
    out_.ops_.clear();
    out_.usesTime_ = false;
    sum();
    skip();
    if (ok_ && i_ < s_.size())
      fail("unexpected '" + std::string(1, s_[i_]) + "'");
    if (ok_ && depth_ > 30)
      fail("too deeply nested");
    if (!ok_) {
      out_.ops_.clear();
      if (error)
        *error = message_;
    }
    return ok_;
  }

private:
  void fail(const std::string& message) {
    if (ok_)
      message_ = message;
    ok_ = false;
  }
  void skip() {
    while (i_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == '\\' || s_[i_] == '$'))
      ++i_; // LaTeX habits (\sin, $...$) are forgiven
  }
  bool take(char c) {
    skip();
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }
  void emit(Code code, double value = 0.0) { out_.ops_.push_back({code, value}); }
  // Track the evaluation stack so eval() can use a fixed one.
  void push() { depth_ = std::max(depth_, ++height_); }
  void pop(int n = 1) { height_ -= n; }

  void sum() {
    product();
    while (ok_) {
      if (take('+')) {
        product();
        emit(kAdd);
        pop();
      } else if (take('-')) {
        product();
        emit(kSub);
        pop();
      } else
        break;
    }
  }
  void product() {
    unary();
    while (ok_) {
      if (take('*')) {
        unary();
        emit(kMul);
        pop();
      } else if (take('/')) {
        unary();
        emit(kDiv);
        pop();
      } else
        break;
    }
  }
  void unary() {
    if (take('-')) {
      unary();
      emit(kNeg);
    } else if (take('+')) {
      unary();
    } else {
      power();
    }
  }
  void power() {
    primary();
    if (ok_ && take('^')) {
      unary(); // right-associative, and -x^2 is -(x^2)
      emit(kPow);
      pop();
    }
  }
  void primary() {
    skip();
    if (i_ >= s_.size())
      return fail("the expression stops short");
    const char c = s_[i_];
    if (c == '(' || c == '{') {
      ++i_;
      sum();
      if (!take(c == '(' ? ')' : '}'))
        fail("a bracket is not closed");
      return;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      const std::string rest(s_.substr(i_));
      char* end = nullptr;
      const double value = std::strtod(rest.c_str(), &end);
      if (end == rest.c_str())
        return fail("not a number");
      i_ += static_cast<std::size_t>(end - rest.c_str());
      emit(kNumber, value);
      push();
      return;
    }
    if (std::isalpha(static_cast<unsigned char>(c))) {
      const std::size_t begin = i_;
      while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_]))))
        ++i_;
      const std::string_view name = s_.substr(begin, i_ - begin);
      for (const Function& f : kFunctions) {
        if (name != f.name)
          continue;
        if (!take('('))
          return fail(std::string(name) + " needs brackets");
        sum();
        if (f.arguments == 2) {
          if (!take(','))
            return fail(std::string(name) + " takes two values");
          sum();
          pop();
        }
        if (!take(')'))
          return fail("a bracket is not closed");
        emit(f.code);
        return;
      }
      push();
      if (name == "x")
        return emit(kX);
      if (name == "y")
        return emit(kY);
      if (name == "t") {
        out_.usesTime_ = true;
        return emit(kT);
      }
      if (name == "r")
        return emit(kR);
      if (name == "theta" || name == "phi")
        return emit(kTheta);
      if (name == "pi")
        return emit(kNumber, std::numbers::pi);
      if (name == "e")
        return emit(kNumber, std::numbers::e);
      return fail("unknown name '" + std::string(name) + "'");
    }
    fail("unexpected '" + std::string(1, c) + "'");
  }

  std::string_view s_;
  Expression& out_;
  std::size_t i_ = 0;
  bool ok_ = true;
  std::string message_;
  int height_ = 0, depth_ = 0;
};

bool Expression::compile(std::string_view source, std::string* error) {
  return ExpressionParser(source, *this).run(error);
}

double Expression::eval(double x, double y, double t) const noexcept {
  double stack[32];
  int n = 0;
  for (const Op& op : ops_) {
    switch (op.code) {
    case kNumber: stack[n++] = op.value; break;
    case kX: stack[n++] = x; break;
    case kY: stack[n++] = y; break;
    case kT: stack[n++] = t; break;
    case kR: stack[n++] = std::hypot(x, y); break;
    case kTheta: stack[n++] = std::atan2(y, x); break;
    case kAdd: --n; stack[n - 1] += stack[n]; break;
    case kSub: --n; stack[n - 1] -= stack[n]; break;
    case kMul: --n; stack[n - 1] *= stack[n]; break;
    case kDiv: --n; stack[n - 1] /= stack[n]; break;
    case kPow: --n; stack[n - 1] = std::pow(stack[n - 1], stack[n]); break;
    case kNeg: stack[n - 1] = -stack[n - 1]; break;
    case kSin: stack[n - 1] = std::sin(stack[n - 1]); break;
    case kCos: stack[n - 1] = std::cos(stack[n - 1]); break;
    case kTan: stack[n - 1] = std::tan(stack[n - 1]); break;
    case kAsin: stack[n - 1] = std::asin(stack[n - 1]); break;
    case kAcos: stack[n - 1] = std::acos(stack[n - 1]); break;
    case kAtan: stack[n - 1] = std::atan(stack[n - 1]); break;
    case kSinh: stack[n - 1] = std::sinh(stack[n - 1]); break;
    case kCosh: stack[n - 1] = std::cosh(stack[n - 1]); break;
    case kTanh: stack[n - 1] = std::tanh(stack[n - 1]); break;
    case kExp: stack[n - 1] = std::exp(stack[n - 1]); break;
    case kLog: stack[n - 1] = std::log(stack[n - 1]); break;
    case kSqrt: stack[n - 1] = std::sqrt(stack[n - 1]); break;
    case kAbs: stack[n - 1] = std::abs(stack[n - 1]); break;
    case kSign: stack[n - 1] = stack[n - 1] > 0.0 ? 1.0 : stack[n - 1] < 0.0 ? -1.0 : 0.0; break;
    case kFloor: stack[n - 1] = std::floor(stack[n - 1]); break;
    case kSinc: stack[n - 1] = std::abs(stack[n - 1]) < 1e-9 ? 1.0 : std::sin(stack[n - 1]) / stack[n - 1]; break;
    case kGauss: stack[n - 1] = std::exp(-stack[n - 1] * stack[n - 1]); break;
    case kAtan2: --n; stack[n - 1] = std::atan2(stack[n - 1], stack[n]); break;
    case kMin: --n; stack[n - 1] = std::min(stack[n - 1], stack[n]); break;
    case kMax: --n; stack[n - 1] = std::max(stack[n - 1], stack[n]); break;
    case kMod: --n; stack[n - 1] = std::fmod(stack[n - 1], stack[n]); break;
    default: break;
    }
  }
  const double value = n > 0 ? stack[0] : 0.0;
  return std::isfinite(value) ? value : 0.0;
}

} // namespace cs
