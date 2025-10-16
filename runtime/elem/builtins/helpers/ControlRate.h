#pragma once

#include <cstddef>
#include <algorithm>
#include <functional>
#include <atomic>

namespace elem
{
    namespace kr
    {
        struct ControlRateConfig {
            std::atomic<uint32_t> stepSamples{1};
            std::atomic<bool> linear{false};
        };

        inline void forEachStep(size_t numSamples, uint32_t step, const std::function<void(size_t, size_t)>& fn) {
            if (step <= 1) {
                fn(0, numSamples);
                return;
            }

            size_t s = 0;
            while (s < numSamples) {
                size_t e = std::min(numSamples, s + step);
                fn(s, e);
                s = e;
            }
        }

        inline uint32_t getKrDiv(uint32_t nodeOverride, void* userData, uint32_t defaultValue = 16) {
            if (nodeOverride > 0) {
                return nodeOverride;
            }
            if (userData) {
#ifdef ELEM_KR_USERDATA_ACCESSOR
                return ELEM_KR_USERDATA_ACCESSOR(userData);
#else
                return defaultValue;
#endif
            }
            return defaultValue;
        }
    }
}

