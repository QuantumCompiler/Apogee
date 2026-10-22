#include "backends/llama_bases.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "backends/prompt_cache.h"

namespace apogee::backends {

std::optional<SharedBaseKey> shared_base_key(const ModelLoad& load) {
    // The file as the prompt cache knows it: a base replaced on disk is
    // another base, never the one a sharer still runs.
    const std::optional<ModelFingerprint> file = fingerprint_of(std::filesystem::path{load.path});
    if (!file.has_value()) {
        return std::nullopt;
    }
    return SharedBaseKey{.base = models::base_key(load.path, load.mmproj_path, load.cache_type,
                                                  load.cache_type_named),
                         .size = file->size,
                         .modified = file->modified,
                         .gpu_layers = load.gpu_layers};
}

SharedBase::SharedBase(std::unique_ptr<LlamaModel> model) : model_{std::move(model)} {}

std::shared_ptr<SharedBase> LlamaBases::acquire(const std::optional<SharedBaseKey>& key,
                                                const Loader& load, std::string& error) {
    if (key.has_value()) {
        const std::lock_guard<std::mutex> lock{mutex_};
        std::erase_if(bases_, [](const auto& entry) { return entry.second.expired(); });
        if (const auto found = bases_.find(*key); found != bases_.end()) {
            if (std::shared_ptr<SharedBase> held = found->second.lock(); held != nullptr) {
                return held;
            }
        }
    }
    std::unique_ptr<LlamaModel> model = load(error);
    if (model == nullptr) {
        return nullptr;
    }
    auto loaded = std::make_shared<SharedBase>(std::move(model));
    if (!key.has_value()) {
        return loaded;
    }
    const std::lock_guard<std::mutex> lock{mutex_};
    std::weak_ptr<SharedBase>& slot = bases_[*key];
    if (std::shared_ptr<SharedBase> first = slot.lock(); first != nullptr) {
        // Another sharer loaded it meanwhile: theirs is shared, and this copy
        // is freed once the lock is let go.
        return first;
    }
    slot = loaded;
    return loaded;
}

std::size_t LlamaBases::resident() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return static_cast<std::size_t>(
        std::ranges::count_if(bases_, [](const auto& entry) { return !entry.second.expired(); }));
}

}  // namespace apogee::backends
