#pragma once

#include <array>
#include <atomic>

namespace theythem
{
struct MeterReadings
{
    std::array<float, 2> input {};
    std::array<float, 2> output {};
    float gainReductionDb = 0.0f;
    bool hasAudio = false;
    bool bypassed = false;
};

// One audio producer and one editor consumer. Retain short peaks between GUI
// refreshes without sharing buffers, taking locks, or allocating in the callback.
class MeterBridge
{
public:
    void push (const MeterReadings& reading) noexcept
    {
        for (size_t channel = 0; channel < 2; ++channel)
        {
            accumulate (input[channel], reading.input[channel]);
            accumulate (output[channel], reading.output[channel]);
        }
        accumulate (reduction, reading.gainReductionDb);
        bypassed.store (reading.bypassed, std::memory_order_relaxed);
        active.store (true, std::memory_order_relaxed);
    }

    MeterReadings take() noexcept
    {
        MeterReadings reading;
        for (size_t channel = 0; channel < 2; ++channel)
        {
            reading.input[channel] = input[channel].exchange (0.0f, std::memory_order_relaxed);
            reading.output[channel] = output[channel].exchange (0.0f, std::memory_order_relaxed);
        }
        reading.gainReductionDb = reduction.exchange (0.0f, std::memory_order_relaxed);
        reading.hasAudio = active.exchange (false, std::memory_order_relaxed);
        reading.bypassed = bypassed.load (std::memory_order_relaxed);
        return reading;
    }

    void reset() noexcept { (void) take(); }

private:
    static_assert (std::atomic<float>::is_always_lock_free);
    static void accumulate (std::atomic<float>& destination, float value) noexcept
    {
        auto previous = destination.load (std::memory_order_relaxed);
        while (previous < value && ! destination.compare_exchange_weak (
                   previous, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    std::array<std::atomic<float>, 2> input {}, output {};
    std::atomic<float> reduction { 0.0f };
    std::atomic<bool> active { false }, bypassed { false };
};
} // namespace theythem
