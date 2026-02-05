#pragma once

#include <algorithm>
#include <list>
#include <unordered_map>

#include "DefaultNodeTypes.h"
#include "BlockEventsBufferPool.h"
#include "FloatBufferPool.h"
#include "Types.h"


namespace elem
{

    //==============================================================================
    // Returns the number of output channels given a set of outlet connections
    inline size_t getRequiredOutputChannels(std::vector<OutletConnection> const& outlets) {
        size_t numOuts = 1;

        for (auto const& connection : outlets) {
            // Outlet channels are zero indexed, hence the +1 for required channel count
            numOuts = std::max(numOuts, connection.outletChannel + 1);
        }

        return numOuts;
    }

    template <typename FloatType>
    class BufferAllocator
    {
    public:
        BufferAllocator(size_t blockSize)
            : blockSize(blockSize)
        {
            // We allocate buffer storage in chunks of 32 blocks
            storage.push_back(std::vector<FloatType>(32u * blockSize));
        }

        void reset()
        {
            nextChunk = 0;
            chunkOffset = 0;
        }

        FloatType* next()
        {
            if (nextChunk >= storage.size()) {
                storage.push_back(std::vector<FloatType>(32u * blockSize));
            }

            auto it = storage.begin();
            std::advance(it, nextChunk);

            auto& chunk = *it;
            auto* result = chunk.data() + chunkOffset;

            chunkOffset += blockSize;

            if (chunkOffset >= chunk.size()) {
                nextChunk++;
                chunkOffset = 0;
            }

            return result;
        }

    private:
        std::list<std::vector<FloatType>> storage;

        size_t blockSize = 0;
        size_t nextChunk = 0;
        size_t chunkOffset = 0;
    };

    struct BufferMapKeyHash {
        template <typename T1, typename T2>
        std::size_t operator() (std::pair<T1, T2> const& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);

            h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            return h1;
        }
    };

    template <typename FloatType>
    class RootRenderSequence
    {
    public:
        RootRenderSequence(FloatBufferPool<FloatType>& pool, BlockEventsBufferPool& eventsPool, std::shared_ptr<RootNode<FloatType>>& root)
            : rootPtr(root)
            , m_bufferPool(pool)
            , m_eventsBufferPool(eventsPool)
        {}

        void push(std::shared_ptr<GraphNode<FloatType>>& node, std::vector<OutletConnection> const& outlets)
        {
            nodeList.push_back(node);

            if (auto tap = std::dynamic_pointer_cast<TapOutNode<FloatType>>(node)) {
                tapList.push_back(tap);
            }

            auto outputChannels = m_bufferPool.produce(node->getId(), outlets);
            auto& outputEvents = m_eventsBufferPool.produce(node->getId(), outlets);

            renderOps.push_back(RenderOp {
                node.get(),
                std::move(outputChannels),
                {},
                &outputEvents,
                {},
                false
            });
        }

        void push(std::shared_ptr<GraphNode<FloatType>>& node, std::vector<InletConnection> const& inlets, std::vector<OutletConnection> const& outlets)
        {
            if (inlets.size() == 0) {
                return push(node, outlets);
            }

            nodeList.push_back(node);

            if (auto tap = std::dynamic_pointer_cast<TapOutNode<FloatType>>(node)) {
                tapList.push_back(tap);
            }

            node->setProperty("_internal:numChildren", elem::js::Number(inlets.size()));

            auto outputChannels = m_bufferPool.produce(node->getId(), outlets);
            auto inputChannels = m_bufferPool.consume(inlets);

            auto& outputEvents = m_eventsBufferPool.produce(node->getId(), outlets);
            auto inputEvents = m_eventsBufferPool.consume(inlets);

            renderOps.push_back(RenderOp {
                node.get(),
                std::move(outputChannels),
                std::move(inputChannels),
                &outputEvents,
                std::move(inputEvents),
                true
            });
        }

        void processQueuedEvents(std::function<void(std::string const&, js::Value)>& evtCallback)
        {
            // We don't process events if our root is inactive
            if (rootPtr->getPropertyWithDefault("active", false))
            {
                for (auto& n : nodeList) {
                    n->processEvents(evtCallback);
                }
            }
        }

        void promoteTapBuffers(size_t numSamples)
        {
            // Don't promote if our RootRenderSequence represents a RootNode that has become
            // inactive, even if it's still fading out
            if (!rootPtr->active())
                return;

            for (auto& n : tapList) {
                n->promoteTapBuffers(numSamples);
            }
        }

        void process(BlockContext<FloatType> const& hostCtx)
        {
            size_t const outChan = rootPtr->getChannelNumber();

            // Nothing to do if this root has stopped running or if it's aimed at
            // an invalid output channel
            if (!rootPtr->stillRunning() || outChan < 0u || outChan >= hostCtx.numOutputChannels)
            {
                if (needsReset)
                {
                    for (size_t i = 0; i < nodeList.size(); ++i)
                    {
                        nodeList[i]->reset();
                    }

                    needsReset = false;
                }

                return;
            }

            needsReset = true;

            // Run the subsequence
            for (size_t i = 0; i < renderOps.size(); ++i) {
                auto& op = renderOps[i];
                op.outputEvents->clear();

                if (op.hasInlets) {
                    aggregateEvents.storage.clear();
                    for (auto& evts : op.inputEvents) {
                        for (auto& e : evts->storage) {
                            aggregateEvents.storage.push_back(e);
                        }
                    }
                    aggregateEvents.sort();

                    op.node->process(BlockContext<FloatType> {
                        const_cast<const FloatType**>(op.inputChannels.data()),
                        op.inputChannels.size(),
                        op.outputChannels.data(),
                        op.outputChannels.size(),
                        hostCtx.numSamples,
                        hostCtx.userData,
                        rootPtr->active(),
                        aggregateEvents,
                        *op.outputEvents,
                    });
                } else {
                    op.node->process(BlockContext<FloatType> {
                        hostCtx.inputData,
                        hostCtx.numInputChannels,
                        op.outputChannels.data(),
                        op.outputChannels.size(),
                        hostCtx.numSamples,
                        hostCtx.userData,
                        rootPtr->active(),
                        hostCtx.inputEvents,
                        *op.outputEvents,
                    });
                }
            }

            // Sum into the output buffer
            auto* data = m_bufferPool.peek(rootPtr->getId(), 0);

            for (size_t j = 0; j < hostCtx.numSamples; ++j) {
                hostCtx.outputData[outChan][j] += data[j];
            }
        }

    private:
        std::shared_ptr<RootNode<FloatType>> rootPtr;
        std::vector<std::shared_ptr<GraphNode<FloatType>>> nodeList;
        std::vector<std::shared_ptr<TapOutNode<FloatType>>> tapList;
        FloatBufferPool<FloatType>& m_bufferPool;
        BlockEventsBufferPool& m_eventsBufferPool;

        struct RenderOp {
            GraphNode<FloatType>* node;
            ChannelData<FloatType> outputChannels;
            ChannelData<FloatType> inputChannels;
            BlockEvents* outputEvents;
            choc::SmallVector<choc::ObjectPointer<BlockEvents>, 16> inputEvents;
            bool hasInlets;
        };
        std::vector<RenderOp> renderOps;

        BlockEvents aggregateEvents;
        bool needsReset{true};
    };

    template <typename FloatType>
    class GraphRenderSequence
    {
    public:
        GraphRenderSequence(size_t blockSize)
        : bufferPool(blockSize)
        {
        }

        void reset()
        {
            subseqs.clear();
            bufferPool.clear();
            eventsBufferPool.clear();
        }

        void push(RootRenderSequence<FloatType>&& sq)
        {
            subseqs.push_back(std::move(sq));
        }

        void processQueuedEvents(std::function<void(std::string const&, js::Value)>&& evtCallback)
        {
            std::for_each(subseqs.begin(), subseqs.end(), [&](RootRenderSequence<FloatType>& sq) {
                sq.processQueuedEvents(evtCallback);
            });
        }

        void process(BlockContext<FloatType> const& hostCtx)
        {
            // Clear the output channels
            for (size_t i = 0; i < hostCtx.numOutputChannels; ++i) {
                for (size_t j = 0; j < hostCtx.numSamples; ++j) {
                    hostCtx.outputData[i][j] = FloatType(0);
                }
            }

            // Process subsequences
            for (auto& sq : subseqs) {
                sq.process(hostCtx);
            }

            // Promote tap buffers.
            //
            // This step follows the processing step because we want read-then-write behavior
            // through the tap table for any feedback cycles. That means that, if we have a running
            // graph, and transition to a new graph, the tapIn node gets to read from the tap table,
            // which likely propagates to a corresponding tapOut node which can fill its internal delay
            // line. This then gets promoted in the subsequent step. If we went write-then-read, then
            // the new tapOut node would clobber whatever's in the tap table because it promotes before
            // it gets a chance to see what its corresponding tapIn is providing.
            for (auto& sq : subseqs) {
                sq.promoteTapBuffers(hostCtx.numSamples);
            }
        }

        FloatBufferPool<FloatType> bufferPool;
        BlockEventsBufferPool eventsBufferPool;

    private:
        std::vector<RootRenderSequence<FloatType>> subseqs;
    };

} // namespace elem
