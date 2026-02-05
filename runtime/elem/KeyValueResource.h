#pragma once

#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>

#include "SharedResource.h"
#include "Value.h"

namespace elem {

    class KeyValueResource : public SharedResource {
    public:
        using MapType = std::unordered_map<std::string, elem::js::Value>;

        KeyValueResource()
            : current(std::make_shared<MapType>()) {}

        BufferView<float> getChannelData(size_t /*channelIndex*/) override {
            static float kDummy = 0.0f;
            return BufferView<float>(&kDummy, 0);
        }

        size_t numChannels() override { return 0; }
        size_t numSamples() override { return 0; }

        void setMap(MapType&& map) {
            auto next = std::make_shared<MapType>(std::move(map));
            std::atomic_store_explicit(&current, next, std::memory_order_release);
            version_.fetch_add(1, std::memory_order_release);
        }

        std::shared_ptr<const MapType> snapshot() const {
            return std::atomic_load_explicit(&current, std::memory_order_acquire);
        }

        uint64_t version() const {
            return version_.load(std::memory_order_acquire);
        }

        elem::js::Value get(std::string const& key) const {
            auto snap = snapshot();
            auto it = snap->find(key);
            if (it != snap->end()) return it->second;
            return elem::js::Value();
        }

    private:
        std::shared_ptr<MapType> current;
        std::atomic<uint64_t> version_{0};
    };

} // namespace elem
