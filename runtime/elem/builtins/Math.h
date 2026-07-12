#pragma once

#include "../GraphNode.h"


namespace elem
{

    template <typename FloatType, FloatType op(FloatType)>
    struct UnaryOperationNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (numChannels < 1)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = op(inputData[0][i]);
            }
        }
    };

    // Variant of UnaryOperationNode for expensive scalar ops (libm calls like
    // exp/pow/log/tanh): when the input is constant across the block — the
    // common case for parameter-derived curves like db2gain/tau2pole — the
    // per-sample call collapses to one call + fill. Bit-exact: op is pure, so
    // op(x0) is the value the loop would write to every sample. Audio-rate
    // inputs bail out of the constness scan within a couple of samples.
    template <typename FloatType, FloatType op(FloatType)>
    struct ExpensiveUnaryOperationNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (numChannels < 1)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            if (numSamples > 0) {
                auto const* in = inputData[0];
                auto const x0 = in[0];

                bool constant = true;
                for (size_t i = 1; i < numSamples; ++i) {
                    if (in[i] != x0) { constant = false; break; }
                }

                if (constant)
                    return (void) std::fill_n(outputData, numSamples, op(x0));
            }

            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = op(inputData[0][i]);
            }
        }
    };

    template <typename FloatType, typename BinaryOp>
    struct BinaryOperationNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (numChannels < 2)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            // Copy the first input to the output buffer
            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = inputData[0][i];
            }

            // Then walk the second channel with the operator
            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = op(outputData[i], inputData[1][i]);
            }
        }

        BinaryOp op;
    };

    // BinaryOperationNode counterpart of ExpensiveUnaryOperationNode — used
    // only for libm-priced ops (pow). Both inputs constant across the block
    // (db2gain: pow(10, db/20) on a settled fader) → one call + fill, bit-exact.
    template <typename FloatType, typename BinaryOp>
    struct ExpensiveBinaryOperationNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (numChannels < 2)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            if (numSamples > 0) {
                auto const* a = inputData[0];
                auto const* b = inputData[1];
                auto const a0 = a[0];
                auto const b0 = b[0];

                bool constant = true;
                for (size_t i = 1; i < numSamples; ++i) {
                    if (a[i] != a0 || b[i] != b0) { constant = false; break; }
                }

                if (constant)
                    return (void) std::fill_n(outputData, numSamples, op(a0, b0));
            }

            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = op(inputData[0][i], inputData[1][i]);
            }
        }

        BinaryOp op;
    };

    template <typename FloatType, typename BinaryOp>
    struct BinaryReducingNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (numChannels < 1)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            // Copy the first input to the output buffer
            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = inputData[0][i];
            }

            // Then for each remaining channel, perform the arithmetic operation
            // into the output buffer.
            for (size_t i = 1; i < numChannels; ++i) {
                for (size_t j = 0; j < numSamples; ++j) {
                    outputData[j] = op(outputData[j], inputData[i][j]);
                }
            }
        }

        BinaryOp op;
    };

    template <typename FloatType>
    struct IdentityNode : public GraphNode<FloatType> {
        using GraphNode<FloatType>::GraphNode;

        int setProperty(std::string const& key, js::Value const& val) override
        {
            if (key == "channel") {
                if (!val.isNumber())
                    return ReturnCode::InvalidPropertyType();

                channel.store(static_cast<int>((js::Number) val));
            }

            return GraphNode<FloatType>::setProperty(key, val);
        }

        void process (BlockContext<FloatType> const& ctx) override {
            auto** inputData = ctx.inputData;
            auto* outputData = ctx.outputData[0];
            auto numChannels = ctx.numInputChannels;
            auto numSamples = ctx.numSamples;

            auto const ch = static_cast<size_t>(channel.load());

            // If we don't have the inputs we need, we bail here and zero the buffer
            // hoping to prevent unexpected signals.
            if (ch < 0 || ch >= numChannels)
                return (void) std::fill_n(outputData, numSamples, FloatType(0));

            for (size_t i = 0; i < numSamples; ++i) {
                outputData[i] = inputData[ch][i];
            }
        }

        std::atomic<int> channel = 0;
    };

    template <typename FloatType>
    struct Modulus {
        FloatType operator() (FloatType x, FloatType y) {
            return std::fmod(x, y);
        }
    };

    template <typename FloatType>
    struct SafeDivides {
        FloatType operator() (FloatType x, FloatType y) {
            return (y == FloatType(0)) ? 0 : x / y;
        }
    };

    template <typename FloatType>
    struct Eq {
        FloatType operator() (FloatType x, FloatType y) {
            return std::abs(x - y) <= std::numeric_limits<FloatType>::epsilon();
        }
    };

    template <typename FloatType>
    struct BinaryAnd {
        FloatType operator() (FloatType x, FloatType y) {
            return FloatType(std::abs(FloatType(1) - x) <= std::numeric_limits<FloatType>::epsilon()
                && std::abs(FloatType(1) - y) <= std::numeric_limits<FloatType>::epsilon());
        }
    };

    template <typename FloatType>
    struct BinaryOr {
        FloatType operator() (FloatType x, FloatType y) {
            return FloatType(std::abs(FloatType(1) - x) <= std::numeric_limits<FloatType>::epsilon()
                || std::abs(FloatType(1) - y) <= std::numeric_limits<FloatType>::epsilon());
        }
    };

    template <typename FloatType>
    struct Min {
        FloatType operator() (FloatType x, FloatType y) {
            return std::min(x, y);
        }
    };

    template <typename FloatType>
    struct Max {
        FloatType operator() (FloatType x, FloatType y) {
            return std::max(x, y);
        }
    };

    template <typename FloatType>
    struct SafePow {
        FloatType operator() (FloatType x, FloatType y) {
            // Catch the case of a negative base and a non-integer exponent
            if (x < FloatType(0) && y != std::floor(y))
                return FloatType(0);

            return std::pow(x, y);
        }
    };

} // namespace elem
