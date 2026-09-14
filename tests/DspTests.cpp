#include "VocalEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Count C++ heap use only on the processing thread. Test-fixture construction,
// diagnostic formatting, and timing-vector growth happen outside this region.
namespace allocationProbe {
thread_local bool active = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;
void* allocate(std::size_t size, std::size_t alignment = 0) {
    if (active) ++allocations;
    void* result = nullptr;
    if (alignment != 0) {
        if (posix_memalign(&result, alignment, std::max<std::size_t>(size, 1)) != 0)
            result = nullptr;
    } else {
        result = std::malloc(std::max<std::size_t>(size, 1));
    }
    if (!result) throw std::bad_alloc();
    return result;
}
void release(void* pointer) noexcept {
    if (active && pointer) ++deallocations;
    std::free(pointer);
}
struct Scope {
    Scope() { allocations = deallocations = 0; active = true; }
    ~Scope() { active = false; }
};
}
void* operator new(std::size_t n) { return allocationProbe::allocate(n); }
void* operator new[](std::size_t n) { return allocationProbe::allocate(n); }
void operator delete(void* p) noexcept { allocationProbe::release(p); }
void operator delete[](void* p) noexcept { allocationProbe::release(p); }
void operator delete(void* p, std::size_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::size_t) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, std::align_val_t a) { return allocationProbe::allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocationProbe::allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(n, a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new[](n, a); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocationProbe::release(p); }

namespace {
using theythem::Parameters;
using theythem::VocalEngine;
constexpr int expectedLatency = 768;
constexpr double pi = 3.14159265358979323846;
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(double actual, double expected, double tolerance = 1.0e-5) {
    return std::abs(actual - expected) <= tolerance;
}
Parameters neutral() {
    Parameters p;
    p.pitchSemitones = 0;
    p.formantSemitones = 0;
    p.character = 0;
    p.mix = 1;
    p.inputGainDb = p.outputGainDb = 0;
    p.highPassEnabled = p.compressorEnabled = false;
    return p;
}
std::vector<float> sine(double sr, double hz, double amplitude = 0.1, double seconds = 1.0) {
    std::vector<float> data(static_cast<std::size_t>(sr * seconds));
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<float>(amplitude * std::sin(2 * pi * hz * i / sr));
    return data;
}
std::vector<float> noise(std::size_t count) {
    std::vector<float> data(count);
    unsigned state = 0x12345678;
    for (auto& sample : data) {
        state = state * 1664525U + 1013904223U;
        sample = (static_cast<float>(state >> 8) / 16777216.0f - 0.5f) * 0.08f;
    }
    return data;
}
std::vector<float> render(const std::vector<float>& input, double sr, int block, const Parameters& p, int preparedBlock = 0) {
    VocalEngine engine;
    engine.prepare(sr, preparedBlock ? preparedBlock : block, 1);
    auto output = input;
    for (std::size_t i = 0; i < output.size(); i += static_cast<std::size_t>(block)) {
        const int count = static_cast<int>(std::min<std::size_t>(block, output.size() - i));
        float* pointers[] {output.data() + i};
        engine.process(pointers, 1, count, p);
    }
    return output;
}
double rms(const std::vector<float>& data, std::size_t start = 0) {
    double sum = 0;
    for (std::size_t i = start; i < data.size(); ++i) sum += static_cast<double>(data[i]) * data[i];
    return std::sqrt(sum / static_cast<double>(data.size() - start));
}
void requireFinite(const std::vector<float>& data, const char* message) {
    require(std::all_of(data.begin(), data.end(), [](float x) { return std::isfinite(x); }), message);
}
void parameters() {
    const Parameters defaults;
    require(near(defaults.pitchSemitones, 12), "Default pitch must be +12 semitones");
    require(near(defaults.formantSemitones, 0), "Default formant must be neutral");
    require(near(defaults.character, 0.65), "Default character must be 65 percent");
    require(near(defaults.mix, 1), "Default mix must be fully wet");
    require(near(defaults.inputGainDb, 0) && near(defaults.outputGainDb, 0), "Default gains must be zero dB");
    Parameters p;
    p.pitchSemitones = 100; p.formantSemitones = -100; p.character = 3; p.mix = -2;
    p.inputGainDb = 100; p.outputGainDb = -100;
    auto limited = VocalEngine::sanitise(p);
    require(near(limited.pitchSemitones, 12) && near(limited.formantSemitones, -12), "Pitch/formant bounds incorrect");
    require(near(limited.character, 1) && near(limited.mix, 0), "Normalized parameter bounds incorrect");
    require(near(limited.inputGainDb, 24) && near(limited.outputGainDb, -48), "Gain bounds incorrect");
    p.pitchSemitones = -100; p.formantSemitones = 100; p.character = -3; p.mix = 2;
    p.inputGainDb = -100; p.outputGainDb = 100;
    limited = VocalEngine::sanitise(p);
    require(near(limited.pitchSemitones, -12) && near(limited.formantSemitones, 12), "Opposite pitch/formant bounds incorrect");
    require(near(limited.character, 0) && near(limited.mix, 1), "Opposite normalized bounds incorrect");
    require(near(limited.inputGainDb, -24) && near(limited.outputGainDb, 12), "Opposite gain bounds incorrect");
    p.pitchSemitones = 3.14159f; p.formantSemitones = -1.2345f;
    limited = VocalEngine::sanitise(p);
    require(near(limited.pitchSemitones, p.pitchSemitones) && near(limited.formantSemitones, p.formantSemitones), "Continuous controls must not quantize semitones");
    p.pitchSemitones = std::numeric_limits<float>::quiet_NaN();
    p.formantSemitones = std::numeric_limits<float>::infinity();
    p.character = p.mix = p.inputGainDb = p.outputGainDb = p.pitchSemitones;
    limited = VocalEngine::sanitise(p);
    require(std::isfinite(limited.pitchSemitones) && std::isfinite(limited.formantSemitones)
        && std::isfinite(limited.character) && std::isfinite(limited.mix)
        && std::isfinite(limited.inputGainDb) && std::isfinite(limited.outputGainDb), "Nonfinite parameter sanitation failed");
}
void mixingAndUtilities(double sr) {
    auto p = neutral();
    p.transformEnabled = false;
    const auto input = noise(static_cast<std::size_t>(sr));
    for (float mix : {0.0f, 0.25f, 0.5f, 1.0f}) {
        p.mix = mix;
        const auto output = render(input, sr, 128, p);
        double error = 0;
        for (std::size_t i = expectedLatency; i < output.size(); ++i)
            error = std::max(error, std::abs(static_cast<double>(output[i] - input[i - expectedLatency])));
        require(error < 2.0e-5, "Dry/wet must remain unity when both branches are equivalent");
    }
    p.mix = 1;
    p.inputGainDb = 6; p.outputGainDb = -3;
    const auto gained = render(input, sr, 128, p);
    const double expectedGain = std::pow(10.0, 3.0 / 20.0);
    double gainError = 0;
    for (std::size_t i = expectedLatency; i < gained.size(); ++i)
        gainError = std::max(gainError, std::abs(gained[i] - input[i - expectedLatency] * expectedGain));
    require(gainError < 3.0e-5, "Input/output gain dB math incorrect");
    p.bypassed = true; p.inputGainDb = 24; p.outputGainDb = 12;
    p.highPassEnabled = p.compressorEnabled = p.transformEnabled = true;
    p.pitchSemitones = 12; p.formantSemitones = 12; p.character = 1;
    const auto bypassed = render(input, sr, 128, p);
    double bypassError = 0;
    for (std::size_t i = expectedLatency; i < bypassed.size(); ++i)
        bypassError = std::max(bypassError, std::abs(static_cast<double>(bypassed[i] - input[i - expectedLatency])));
    require(bypassError < 2.0e-5, "Overall bypass must ignore effects/gains and preserve latency");
    p = neutral(); p.transformEnabled = false;
    const auto lowInput = sine(sr, 20, 0.1);
    const auto lowDry = render(lowInput, sr, 128, p);
    p.highPassEnabled = true;
    const auto lowFiltered = render(lowInput, sr, 128, p);
    require(rms(lowFiltered, lowFiltered.size() / 2) < 0.3 * rms(lowDry, lowDry.size() / 2), "High-pass bypass does not distinguish 20Hz attenuation");
    p.highPassEnabled = false;
    const auto loud = sine(sr, 700, 0.7);
    const auto uncompressed = render(loud, sr, 128, p);
    p.compressorEnabled = true;
    const auto compressed = render(loud, sr, 128, p);
    require(rms(compressed, compressed.size() / 2) < 0.85 * rms(uncompressed, uncompressed.size() / 2), "Compressor bypass does not distinguish gain reduction");
    p = neutral(); p.pitchSemitones = 12;
    const auto quiet = sine(sr, 200, 0.03);
    p.mix = 0; const auto dry = render(quiet, sr, 128, p);
    p.mix = 1; const auto wet = render(quiet, sr, 128, p);
    p.mix = 0.25f; const auto mixed = render(quiet, sr, 128, p);
    double mixError = 0;
    for (std::size_t i = mixed.size() / 2; i < mixed.size(); ++i)
        mixError = std::max(mixError, std::abs(mixed[i] - (dry[i] * 0.75 + wet[i] * 0.25)));
    require(mixError < 2.0e-4, "Actual dry/wet branches must obey linear amplitude mixing");
}
void initializeAndLatency(double sr, int block) {
    VocalEngine engine;
    engine.prepare(sr, block, 2);
    require(near(engine.sampleRate(), sr), "Prepared sample rate incorrect");
    require(engine.latencySamples() == expectedLatency, "Reported latency changed unexpectedly");
    auto p = neutral();
    std::vector<float> data(static_cast<std::size_t>(block));
    std::vector<float> right(static_cast<std::size_t>(block));
    float* pointers[] {data.data(), right.data()};
    for (int i = 0; i < 12; ++i) {
        engine.process(pointers, 2, block, p);
        require(rms(data) < 1.0e-12 && rms(right) < 1.0e-12, "Silence must stay silent");
    }
    engine.reset();
    const int impulsePosition = 1024;
    int peakPosition = -1;
    double peak = 0;
    for (int offset = 0; offset < 4096; offset += block) {
        std::fill(data.begin(), data.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        if (offset <= impulsePosition && impulsePosition < offset + block)
            data[static_cast<std::size_t>(impulsePosition - offset)] = 0.125f;
        engine.process(pointers, 2, block, p);
        requireFinite(data, "Impulse output must be finite");
        require(rms(right) < 1.0e-12, "Left-channel signal leaked into silent right channel");
        for (int i = 0; i < block; ++i) if (std::abs(data[static_cast<std::size_t>(i)]) > peak) {
            peak = std::abs(data[static_cast<std::size_t>(i)]);
            peakPosition = offset + i;
        }
    }
    require(peakPosition - impulsePosition == engine.latencySamples(), "Measured impulse delay disagrees with reported latency");
    std::printf("latency sr=%.0f block=%d reported=%d measured=%d samples (%.6f ms); neutral impulse peak=%.8f (input0.125)\n", sr, block, engine.latencySamples(), peakPosition - impulsePosition, 1000.0 * engine.latencySamples() / sr, peak);
    // The asymmetric STFT's phase correction has measured +0.877% neutral
    // impulse reconstruction gain. Keep this below 1%; bypass/dry gain math is
    // still checked separately at much tighter tolerance.
    require(near(peak, 0.125, 0.125 * 0.01), "Neutral asymmetric transformation exceeded one-percent impulse gain error");
    engine.reset();
    std::fill(data.begin(), data.end(), 0.0f); std::fill(right.begin(), right.end(), 0.0f);
    for (int i = 0; i < 20; ++i) {
        engine.process(pointers, 2, block, p);
        require(rms(data) < 1.0e-12 && rms(right) < 1.0e-12, "Reset must remove previous signal history");
    }
    // Include nonfinite samples and controls, then require healthy-signal recovery.
    p = Parameters{};
    data[0] = std::numeric_limits<float>::quiet_NaN();
    if (block > 1) right[1] = std::numeric_limits<float>::infinity();
    engine.process(pointers, 2, block, p);
    requireFinite(data, "NaN input escaped sanitation"); requireFinite(right, "Inf input escaped sanitation");
    double recovered = 0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        for (int i = 0; i < block; ++i)
            data[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] = static_cast<float>(0.1 * std::sin((iteration * block + i) * 2 * pi * 200 / sr));
        engine.process(pointers, 2, block, p);
        requireFinite(data, "NaN poisoned persistent engine state");
        if (iteration > 80) recovered += rms(data);
    }
    require(recovered > 0.01, "Engine did not recover audio after invalid input");
    // Max host blocks are a preparation hint; larger callbacks must be chunked safely.
    const auto oversized = render(sine(sr, 200), sr, 1025, neutral(), block);
    requireFinite(oversized, "Oversized host callback produced invalid audio");
    require(rms(oversized, oversized.size() / 2) > 0.01, "Oversized callback lost audio");
}
void nearUnityAutomation(double sr) {
    VocalEngine engine;
    engine.prepare(sr, 64, 1);
    auto p = neutral();
    std::array<float, 64> block {};
    float* channels[] {block.data()};
    for (float semitones : {0.00001f, 0.001f, -0.00001f, -0.001f, 0.02f}) {
        p.pitchSemitones = p.formantSemitones = semitones;
        bool finite = true;
        // Ratios infinitesimally above unity round the spectral resampler's
        // target bin count back to its source count. This is valid automation
        // and also exercises the documented vendor invariant fix in Debug.
        for (int iteration = 0; iteration < 32; ++iteration) {
            for (std::size_t i = 0; i < block.size(); ++i)
                block[i] = static_cast<float>(0.03 * std::sin((iteration * 64 + i) * 2 * pi * 200 / sr));
            engine.process(channels, 1, 64, p);
            for (float sample : block) finite = finite && std::isfinite(sample);
        }
        require(finite, "Near-unity pitch/formant automation must remain valid and finite");
    }
}

void blockSizeIndependence(double sr) {
    auto p = neutral();
    p.pitchSemitones = 7.25f;
    p.formantSemitones = -2.0f;
    p.character = 0.3f;
    const auto input = sine(sr, 230, 0.04);
    const auto reference = render(input, sr, 64, p);
    for (int block : {128, 256, 127, 1025}) {
        const auto candidate = render(input, sr, block, p, 64);
        double error = 0;
        for (std::size_t i = 0; i < candidate.size(); ++i)
            error = std::max(error, std::abs(static_cast<double>(candidate[i] - reference[i])));
        require(error < 2.0e-4, "Host block boundaries changed steady DSP output");
    }
}

void callbackAllocationAndTiming(double sr, int block) {
    VocalEngine engine;
    engine.prepare(sr, block, 2);
    auto left = sine(sr, 135, 0.15, static_cast<double>(block) / sr + 0.01);
    auto right = left;
    float* pointers[] {left.data(), right.data()};
    Parameters p;
    {
        allocationProbe::Scope guard;
        engine.process(pointers, 2, block, p);
    }
    require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0,
            "First callback must not lazily allocate/free DSP state");
    for (int i = 0; i < 20; ++i) engine.process(pointers, 2, block, p);
    bool finite = true;
    bool boundedOutput = true;
    {
        allocationProbe::Scope guard;
        for (int j = 0; j < 500; ++j) {
            p.pitchSemitones = static_cast<float>(12 * std::sin(j * 0.03));
            p.formantSemitones = static_cast<float>(12 * std::cos(j * 0.025));
            p.character = static_cast<float>((j % 101) / 100.0);
            p.mix = static_cast<float>((j % 99) / 98.0);
            p.inputGainDb = static_cast<float>(-24 + j % 49);
            p.outputGainDb = static_cast<float>(-48 + j % 61);
            p.highPassEnabled = j % 3 != 0; p.compressorEnabled = j % 4 != 0;
            p.transformEnabled = j % 5 != 0; p.bypassed = j % 13 == 0;
            for (int i = 0; i < block; ++i)
                left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] = static_cast<float>(0.04 * std::sin((j * block + i) * 2 * pi * 135 / sr));
            engine.process(pointers, 2, block, p);
            for (int i = 0; i < block; ++i) {
                finite = finite && std::isfinite(left[static_cast<std::size_t>(i)]) && std::isfinite(right[static_cast<std::size_t>(i)]);
                boundedOutput = boundedOutput && std::abs(left[static_cast<std::size_t>(i)]) <= 1.0f && std::abs(right[static_cast<std::size_t>(i)]) <= 1.0f;
            }
        }
    }
    require(allocationProbe::allocations == 0, "Audio callback allocated C++ heap memory after preparation");
    require(allocationProbe::deallocations == 0, "Audio callback freed C++ heap memory after preparation");
    require(finite, "Automated controls produced NaN/Inf");
    require(boundedOutput, "Automated gain extremes escaped final sample bounds");
    p = Parameters{};
    std::array<double, 1000> timings {};
    std::array<double, 1000> cpuTimings {};
    const auto threadNanoseconds = [] {
        timespec now {};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return 0LL;
        return static_cast<long long>(now.tv_sec) * 1000000000LL + now.tv_nsec;
    };
    require(threadNanoseconds() > 0, "Linux per-thread CPU clock unavailable for benchmark");
    for (std::size_t j = 0; j < timings.size(); ++j) {
        for (int i = 0; i < block; ++i)
            left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] = static_cast<float>(0.04 * std::sin((j * block + i) * 2 * pi * 135 / sr));
        const auto begin = std::chrono::steady_clock::now();
        const auto cpuBegin = threadNanoseconds();
        engine.process(pointers, 2, block, p);
        cpuTimings[j] = (threadNanoseconds() - cpuBegin) / 1000.0;
        timings[j] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    }
    std::sort(timings.begin(), timings.end());
    std::sort(cpuTimings.begin(), cpuTimings.end());
    std::printf("offline bench (not realtime scheduling): sr=%.0f block=%d stereo wall p50/p99/max=%.1f/%.1f/%.1fus; thread CPU p50/p99/max=%.1f/%.1f/%.1fus; block deadline=%.1fus; callback new/delete=0/0\n", sr, block, timings[500], timings[990], timings.back(), cpuTimings[500], cpuTimings[990], cpuTimings.back(), 1.0e6 * block / sr);
}
std::vector<float> vowel(double sr) {
    auto data = sine(sr, 100, 0, 1.5);
    for (int harmonic = 1; harmonic <= 70; ++harmonic) {
        const double frequency = 100.0 * harmonic;
        const auto bell = [frequency](double centre, double width) { const double d = (frequency - centre) / width; return std::exp(-0.5 * d * d); };
        const double amplitude = 0.004 * (0.2 + 2.0 * bell(600, 160) + 1.6 * bell(1600, 230) + bell(2700, 300)) / std::pow(harmonic, 0.25);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] += static_cast<float>(amplitude * std::sin(2 * pi * frequency * i / sr + harmonic * 0.137));
    }
    return data;
}
double fundamental(const std::vector<float>& data, double sr) {
    const int start = static_cast<int>(data.size()) - 16000;
    const int minLag = static_cast<int>(sr / 500);
    const int maxLag = static_cast<int>(sr / 80);
    std::vector<double> correlation(static_cast<std::size_t>(maxLag + 2));
    double largest = 0;
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        double dot = 0, firstEnergy = 0, secondEnergy = 0;
        for (int i = start; i < static_cast<int>(data.size()) - maxLag - 1; ++i) {
            const double a = data[static_cast<std::size_t>(i)], b = data[static_cast<std::size_t>(i + lag)];
            dot += a * b; firstEnergy += a * a; secondEnergy += b * b;
        }
        correlation[static_cast<std::size_t>(lag)] = dot / std::sqrt(firstEnergy * secondEnergy + 1.0e-30);
        largest = std::max(largest, correlation[static_cast<std::size_t>(lag)]);
    }
    for (int lag = minLag; lag < maxLag; ++lag) {
        const double previous = correlation[static_cast<std::size_t>(lag - 1)];
        const double current = correlation[static_cast<std::size_t>(lag)];
        const double next = correlation[static_cast<std::size_t>(lag + 1)];
        if (current >= 0.95 * largest && current > previous && current >= next) {
            const double interpolation = 0.5 * (previous - next) / (previous - 2 * current + next);
            return sr / (lag + interpolation);
        }
    }
    return 0;
}
double envelopeCentroid(const std::vector<float>& data, double sr, double f0) {
    const std::size_t count = static_cast<std::size_t>(sr); // Last complete second.
    const std::size_t start = data.size() - count;
    std::vector<double> windowed(count);
    for (std::size_t i = 0; i < count; ++i)
        windowed[i] = data[start + i] * (0.5 - 0.5 * std::cos(2 * pi * i / (count - 1)));
    double weighted = 0, total = 0;
    for (double frequency = f0; frequency <= 7000; frequency += f0) {
        const double coefficient = 2 * std::cos(2 * pi * frequency / sr);
        double a = 0, b = 0;
        for (double sample : windowed) { const double next = sample + coefficient * a - b; b = a; a = next; }
        const double magnitude = std::sqrt(std::max(0.0, a * a + b * b - coefficient * a * b));
        // Spectral amplitude centroid measures envelope movement, not a vocal identity.
        weighted += frequency * magnitude; total += magnitude;
    }
    return weighted / (total + 1.0e-30);
}
void pitchAndFormant(double sr) {
    struct PitchCase { double tone; float semitones; };
    for (const auto test : {PitchCase{100.0, 12.0f}, PitchCase{220.0, 12.0f},
                           PitchCase{220.0, -12.0f}, PitchCase{220.0, 3.5f}}) {
        auto toneParameters = neutral();
        toneParameters.pitchSemitones = test.semitones;
        const double expected = test.tone * std::pow(2.0, test.semitones / 12.0);
        const double outputF0 = fundamental(render(sine(sr, test.tone, 0.04, 1.5), sr, 128, toneParameters), sr);
        std::printf("pure tone sr=%.0f input=%.1f pitch=%+.2fst measured=%.3f Hz expected=%.3f Hz\n", sr, test.tone, test.semitones, outputF0, expected);
        require(std::abs(outputF0 - expected) < 0.02 * expected, "Pure-tone pitch control exceeds two-percent frequency error");
    }
    const auto input = vowel(sr);
    auto p = neutral();
    const auto base = render(input, sr, 128, p);
    p.pitchSemitones = 12;
    const auto octave = render(input, sr, 128, p);
    p.formantSemitones = 6;
    const auto octaveFormantUp = render(input, sr, 128, p);
    p.pitchSemitones = 0; p.formantSemitones = 6;
    const auto formantUp = render(input, sr, 128, p);
    p.formantSemitones = -6;
    const auto formantDown = render(input, sr, 128, p);
    const double baseF0 = fundamental(base, sr), octaveF0 = fundamental(octave, sr);
    const double upF0 = fundamental(formantUp, sr), downF0 = fundamental(formantDown, sr);
    const double combinedF0 = fundamental(octaveFormantUp, sr);
    const double baselineEnvelope = envelopeCentroid(base, sr, 100);
    const double octaveEnvelope = envelopeCentroid(octave, sr, 200);
    const double combinedEnvelope = envelopeCentroid(octaveFormantUp, sr, 200);
    const double upEnvelope = envelopeCentroid(formantUp, sr, 100);
    const double downEnvelope = envelopeCentroid(formantDown, sr, 100);
    std::printf("synthetic vowel sr=%.0f F0 base/up12/formant+6/formant-6 = %.3f/%.3f/%.3f/%.3f Hz; harmonic-amplitude centroid = %.1f/%.1f/%.1f/%.1f Hz\n", sr, baseF0, octaveF0, upF0, downF0, baselineEnvelope, octaveEnvelope, upEnvelope, downEnvelope);
    std::printf("combined pitch+12/formant+6 sr=%.0f F0=%.3f Hz; harmonic-amplitude centroid=%.1f Hz\n", sr, combinedF0, combinedEnvelope);
    require(std::abs(baseF0 - 100) < 2, "Fixture's neutral F0 not retained");
    require(std::abs(octaveF0 - 200) < 4, "+12 semitone control did not raise F0 approximately one octave");
    require(std::abs(upF0 - baseF0) < 2 && std::abs(downF0 - baseF0) < 2, "Formant control shifted the musical fundamental");
    require(upEnvelope > 1.12 * baselineEnvelope && downEnvelope < 0.9 * baselineEnvelope, "Independent formant controls did not materially move spectral envelope both ways");
    require(std::abs(combinedF0 - 200) < 4 && combinedEnvelope > 1.10 * octaveEnvelope, "Combined octave/formant controls are not independent");
    require(octaveEnvelope > 0.65 * baselineEnvelope && octaveEnvelope < 1.45 * baselineEnvelope, "Pitch compensation allowed the envelope to follow the octave shift");
}
}
int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // Stream CTest measurements as each case finishes.
    try {
        parameters();
        for (double sr : {44100.0, 48000.0}) {
            mixingAndUtilities(sr);
            blockSizeIndependence(sr);
            nearUnityAutomation(sr);
            for (int block : {64, 128, 256}) {
                initializeAndLatency(sr, block);
                callbackAllocationAndTiming(sr, block);
            }
            pitchAndFormant(sr);
        }
        std::printf("PASS: %d checks. These are deterministic signal/offline timing tests, not microphone, host, listening, or round-trip latency acceptance.\n", checks);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        allocationProbe::active = false;
        std::fprintf(stderr, "FAIL after %d checks: %s\n", checks, error.what());
        return EXIT_FAILURE;
    }
}
