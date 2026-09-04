#ifndef STACKCHAN_SHAKE_DETECTOR_H
#define STACKCHAN_SHAKE_DETECTOR_H

#include <stdint.h>
#include <math.h>

struct ShakeUpdate {
    bool peak = false;
    bool reversal = false;
    bool triggered = false;
    int8_t axis = -1;
    float value = 0.0f;
    float threshold = 0.0f;
    uint8_t reversals = 0;
};

// Detects an intentional back-and-forth shake rather than raw movement energy.
// A shake starts with one dominant-axis peak and fires after two sign reversals
// inside the window. Each new peak must be re-armed by passing near zero first.
class ShakeDetector {
public:
    explicit ShakeDetector(int sensitivity = 6) { setSensitivity(sensitivity); }

    void setSensitivity(int sensitivity) {
        if (sensitivity < 1) sensitivity = 1;
        if (sensitivity > 10) sensitivity = 10;
        _sensitivity = sensitivity;
    }

    int sensitivity() const { return _sensitivity; }

    float peakThreshold() const {
        float threshold = 340.0f - 25.0f * static_cast<float>(_sensitivity);
        return threshold < 140.0f ? 140.0f : threshold;
    }

    bool active() const { return _axis >= 0; }
    uint8_t reversalCount() const { return _reversals; }

    void reset() {
        _axis = -1;
        _lastSign = 0;
        _reversals = 0;
        _windowStartMs = 0;
        _armed = true;
    }

    ShakeUpdate update(uint32_t nowMs, float gx, float gy, float gz) {
        ShakeUpdate result;
        result.threshold = peakThreshold();

        if (active() && static_cast<uint32_t>(nowMs - _windowStartMs) > kWindowMs) {
            reset();
        }

        const float values[3] = {gx, gy, gz};
        if (!active()) {
            int8_t dominant = 0;
            if (fabsf(values[1]) > fabsf(values[dominant])) dominant = 1;
            if (fabsf(values[2]) > fabsf(values[dominant])) dominant = 2;
            if (fabsf(values[dominant]) < result.threshold) return result;

            _axis = dominant;
            _lastSign = sign(values[_axis]);
            _windowStartMs = nowMs;
            _armed = false;
            result.peak = true;
        } else {
            const float axisValue = values[_axis];
            const float absolute = fabsf(axisValue);
            if (absolute <= result.threshold * kReleaseRatio) {
                _armed = true;
            } else if (_armed && absolute >= result.threshold) {
                const int8_t currentSign = sign(axisValue);
                result.peak = true;
                _armed = false;
                if (currentSign != 0 && currentSign != _lastSign) {
                    _lastSign = currentSign;
                    if (_reversals < 255) ++_reversals;
                    result.reversal = true;
                    if (_reversals >= kRequiredReversals) result.triggered = true;
                }
            }
        }

        result.axis = _axis;
        result.value = active() ? values[_axis] : 0.0f;
        result.reversals = _reversals;
        if (result.triggered) reset();
        return result;
    }

    static constexpr uint32_t kWindowMs = 800;
    static constexpr float kReleaseRatio = 0.45f;
    static constexpr uint8_t kRequiredReversals = 2;

private:
    static int8_t sign(float value) {
        return value > 0.0f ? 1 : (value < 0.0f ? -1 : 0);
    }

    int _sensitivity = 6;
    int8_t _axis = -1;
    int8_t _lastSign = 0;
    uint8_t _reversals = 0;
    uint32_t _windowStartMs = 0;
    bool _armed = true;
};

#endif  // STACKCHAN_SHAKE_DETECTOR_H
