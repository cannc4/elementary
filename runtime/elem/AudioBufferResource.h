#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>
#include "SharedResource.h"
#include "Types.h"


namespace elem
{

    class AudioBufferResource : public SharedResource {
    public:
        struct ChannelStorage
        {
            std::vector<std::vector<float>> channels;
        };

        struct Snapshot
        {
            std::shared_ptr<ChannelStorage> storage;

            BufferView<float> getChannelData(size_t channelIndex) const
            {
                if (storage && channelIndex < storage->channels.size())
                {
                    auto& chan = storage->channels[channelIndex];
                    return BufferView<float>(chan.data(), chan.size());
                }

                return BufferView<float>(nullptr, 0);
            }

            size_t numChannels() const
            {
                return storage ? storage->channels.size() : 0;
            }

            size_t numSamples() const
            {
                if (!storage || storage->channels.empty())
                    return 0;
                return storage->channels[0].size();
            }
        };

        AudioBufferResource(float* data, size_t numSamples)
        {
            replace(data, numSamples);
        }

        AudioBufferResource(float** data, size_t numChannels, size_t numSamples)
        {
            replace(data, numChannels, numSamples);
        }

        AudioBufferResource(size_t numChannels, size_t numSamples)
        {
            auto storage = std::make_shared<ChannelStorage>();
            storage->channels.resize(numChannels);
            for (size_t i = 0; i < numChannels; ++i)
                storage->channels[i].resize(numSamples);
            publish(std::move(storage));
        }

        BufferView<float> getChannelData(size_t channelIndex) override
        {
            return snapshot().getChannelData(channelIndex);
        }

        size_t numChannels() override {
            return snapshot().numChannels();
        }

        size_t numSamples() override {
            return snapshot().numSamples();
        }

        Snapshot snapshot() const
        {
            return Snapshot { std::atomic_load(&currentStorage) };
        }

        void replace(float* data, size_t numSamples)
        {
            auto storage = std::make_shared<ChannelStorage>();
            if (data != nullptr)
                storage->channels.push_back(std::vector<float>(data, data + numSamples));
            else
                storage->channels.push_back(std::vector<float>());
            publish(std::move(storage));
        }

        void replace(float** data, size_t numChannels, size_t numSamples)
        {
            auto storage = std::make_shared<ChannelStorage>();
            storage->channels.reserve(numChannels);
            for (size_t i = 0; i < numChannels; ++i)
            {
                if (data != nullptr && data[i] != nullptr)
                    storage->channels.push_back(std::vector<float>(data[i], data[i] + numSamples));
                else
                    storage->channels.push_back(std::vector<float>());
            }
            publish(std::move(storage));
        }

        void clear(size_t numChannels = 2)
        {
            auto storage = std::make_shared<ChannelStorage>();
            storage->channels.resize(numChannels);
            publish(std::move(storage));
        }

        uint64_t getVersion() const
        {
            return version.load(std::memory_order_acquire);
        }

    private:
        void publish(std::shared_ptr<ChannelStorage> nextStorage)
        {
            auto previous = std::atomic_exchange(&currentStorage, std::move(nextStorage));
            if (previous)
            {
                std::lock_guard<std::mutex> lock(retiredMutex);
                retiredStorage.push_back(std::move(previous));
                constexpr size_t kMaxRetiredVersions = 16;
                if (retiredStorage.size() > kMaxRetiredVersions)
                    retiredStorage.erase(retiredStorage.begin(), retiredStorage.begin() + (retiredStorage.size() - kMaxRetiredVersions));
            }
            version.fetch_add(1, std::memory_order_release);
        }

        std::shared_ptr<ChannelStorage> currentStorage;
        std::vector<std::shared_ptr<ChannelStorage>> retiredStorage;
        mutable std::mutex retiredMutex;
        std::atomic<uint64_t> version{0};
    };

} // namespace elem
