#pragma once

#include "../BlockEvents.h"
#include "../GraphNode.h"
#include "helpers/ControlRate.h"
#include <cmath>

namespace elem
{

    // Emits audio rate signals carrying the current value of the parameter
    // identified by the given parameter index.
    template <typename FloatType>
    struct ParameterValueNode : public elem::GraphNode<FloatType> {
        using elem::GraphNode<FloatType>::GraphNode;

        int setProperty(std::string const& key, elem::js::Value const& val) override
        {
            if (key == "index") {
                if (!val.isNumber())
                    return elem::ReturnCode::InvalidPropertyType();

                if (0 > (elem::js::Number) val)
                    return elem::ReturnCode::InvalidPropertyValue();

                index.store(static_cast<size_t>((elem::js::Number) val));
            }

            if (key == "krDiv") {
                if (!val.isNumber())
                    return elem::ReturnCode::InvalidPropertyType();

                auto div = static_cast<uint32_t>((elem::js::Number) val);
                auto bs = static_cast<uint32_t>(elem::GraphNode<FloatType>::getBlockSize());
                krDiv.store(std::max(uint32_t(1), std::min(div, bs)));
            }

            if (key == "krHz") {
                if (!val.isNumber())
                    return elem::ReturnCode::InvalidPropertyType();

                auto hz = static_cast<float>((elem::js::Number) val);
                if (hz > 0.0f) {
                    auto sr = static_cast<float>(elem::GraphNode<FloatType>::getSampleRate());
                    auto div = static_cast<uint32_t>(std::round(sr / hz));
                    auto bs = static_cast<uint32_t>(elem::GraphNode<FloatType>::getBlockSize());
                    krDiv.store(std::max(uint32_t(1), std::min(div, bs)));
                }
            }

            return elem::GraphNode<FloatType>::setProperty(key, val);
        }

        void process (elem::BlockContext<FloatType> const& ctx) override {
            auto const i = index.load();
            auto const kDiv = krDiv.load();

            if (kDiv <= 1) {
                size_t framesProcessed = 0;

                ctx.inputEvents.template processEventsOfType<ParamValueEvent>(
                    [this, &i, &framesProcessed, &ctx](size_t time, ParamValueEvent const& evt) {
                        if (evt.paramIndex == i) {
                            auto framesRemaining = ctx.numSamples - framesProcessed;
                            std::fill_n(ctx.outputData[0] + framesProcessed, framesRemaining, value);

                            value = evt.value;
                            framesProcessed = time;
                        }
                    }
                );

                auto framesRemaining = ctx.numSamples - framesProcessed;
                std::fill_n(ctx.outputData[0] + framesProcessed, framesRemaining, value);
            } else {
                kr::forEachStep(ctx.numSamples, kDiv, [this, &i, &ctx](size_t stepStart, size_t stepEnd) {
                    ctx.inputEvents.template processEventsOfType<ParamValueEvent>(
                        [this, &i, stepStart, stepEnd](size_t time, ParamValueEvent const& evt) {
                            if (evt.paramIndex == i && time >= stepStart && time < stepEnd) {
                                value = evt.value;
                            }
                        }
                    );

                    std::fill_n(ctx.outputData[0] + stepStart, stepEnd - stepStart, value);
                });
            }
        }

        std::atomic<size_t> index = 0;
        std::atomic<uint32_t> krDiv = 1;
        FloatType value = 0;
    };

} // namespace elem
