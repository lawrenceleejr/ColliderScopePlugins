#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("MIDI note to frequency", "[pitch]") {
  // The acceptance-test notes: A1 to A5.
  CHECK_THAT(cs::midiToHz(33), WithinAbs(55.0, 1e-9));
  CHECK_THAT(cs::midiToHz(45), WithinAbs(110.0, 1e-9));
  CHECK_THAT(cs::midiToHz(57), WithinAbs(220.0, 1e-9));
  CHECK_THAT(cs::midiToHz(69), WithinAbs(440.0, 1e-9));
  CHECK_THAT(cs::midiToHz(81), WithinAbs(880.0, 1e-9));
}

TEST_CASE("Cents", "[pitch]") {
  CHECK_THAT(cs::cents(880.0, 440.0), WithinAbs(1200.0, 1e-9));
  CHECK_THAT(cs::cents(cs::midiToHz(70), cs::midiToHz(69)), WithinAbs(100.0, 1e-9));
  // Spec: rounding the 880 Hz period (54.5 samples at 48 kHz, to 3 s.f.) to 55 is 14 cents flat.
  CHECK_THAT(cs::cents(48000.0 / 55.0, 880.0), WithinAbs(-14.4, 0.1));
}

TEST_CASE("Version string", "[core]") {
  CHECK(cs::version()[0] != '\0');
}
