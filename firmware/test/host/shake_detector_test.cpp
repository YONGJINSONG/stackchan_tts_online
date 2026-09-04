#include <cassert>
#include <cmath>
#include <iostream>

#include "../../src/ShakeDetector.h"

static ShakeUpdate feed(ShakeDetector& detector, uint32_t t,
                        float x, float y = 0.0f, float z = 0.0f) {
    return detector.update(t, x, y, z);
}

int main() {
    ShakeDetector detector(6);
    assert(std::fabs(detector.peakThreshold() - 190.0f) < 0.01f);

    // Ordinary lifting and a single-direction turn must not fire.
    assert(!feed(detector, 0, 120.0f).triggered);
    assert(!feed(detector, 40, 220.0f).triggered);
    assert(!feed(detector, 80, 70.0f).triggered);
    assert(!feed(detector, 120, 230.0f).triggered);
    assert(!feed(detector, 160, 40.0f).triggered);
    assert(detector.active());

    // Let the incomplete gesture expire before the intended shake.
    assert(!feed(detector, 1000, 0.0f).triggered);
    assert(!detector.active());

    // Three alternating peaks (two reversals), with a near-zero release
    // between each, are a deliberate light shake at sensitivity 6.
    assert(!feed(detector, 1100, 220.0f).triggered);
    assert(!feed(detector, 1140, 50.0f).triggered);
    ShakeUpdate firstReverse = feed(detector, 1220, -230.0f);
    assert(firstReverse.reversal && firstReverse.reversals == 1);
    assert(!feed(detector, 1260, -50.0f).triggered);
    ShakeUpdate fired = feed(detector, 1340, 210.0f);
    assert(fired.triggered && fired.reversals == 2);
    assert(!detector.active());

    // Sensitivity is clamped and maps to predictable peak thresholds.
    detector.setSensitivity(0);
    assert(detector.sensitivity() == 1);
    assert(std::fabs(detector.peakThreshold() - 315.0f) < 0.01f);
    detector.setSensitivity(99);
    assert(detector.sensitivity() == 10);
    assert(std::fabs(detector.peakThreshold() - 140.0f) < 0.01f);

    std::cout << "shake detector tests passed\n";
    return 0;
}
