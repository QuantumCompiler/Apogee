#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "backends/llama_runtime.h"
#include "modelstore/footprint.h"

/// One base, many roles (53d): the llamacpp providers whose entries run the
/// same base share one loaded `llama_model`, each serving through its own
/// adapters (`LlamaAdapters`), where each used to load the weights again.
///
/// **The registry holds nothing alive.** It maps a base's key to a weak
/// reference; every provider holding its base holds a share of it, and the
/// last share let go -- an unload, an idle clock, the provider's end -- frees
/// the model. So residency stays each provider's own (27e): a base is held
/// while any sharer holds it, by a session's hold or its own recent use, and
/// unloaded when the last lets go. The factory composes one registry for the
/// process and hands it to every llamacpp provider it builds.
namespace apogee::backends {

/// What makes two loads one base: the base as the footprint keys it
/// (`models::BaseKey` -- the file, its projector, the cache its contexts
/// keep), the file's size and modification time -- the prompt cache's
/// identity (26j), so a file replaced while one sharer holds the old one is
/// another base -- and the layers it offloads.
struct SharedBaseKey {
    models::BaseKey base{};
    std::uintmax_t size = 0;
    std::int64_t modified = 0;
    std::int64_t gpu_layers = 0;

    auto operator<=>(const SharedBaseKey&) const = default;
};

/// The key `load` is shared under, or nullopt when its file cannot be read
/// -- such a load is never shared, and fails as it always has.
[[nodiscard]] std::optional<SharedBaseKey> shared_base_key(const ModelLoad& load);

/// One loaded base, and the lock its sharers take turns on.
class SharedBase {
public:
    explicit SharedBase(std::unique_ptr<LlamaModel> model);
    ~SharedBase() = default;
    SharedBase(const SharedBase&) = delete;
    SharedBase& operator=(const SharedBase&) = delete;
    SharedBase(SharedBase&&) = delete;
    SharedBase& operator=(SharedBase&&) = delete;

    [[nodiscard]] LlamaModel& model() const noexcept {
        return *model_;
    }

    /// Held across every use of the model -- a turn, an embedding, a count,
    /// loading or freeing an entry's adapters. One thread drives a model at a
    /// time, as one provider's model always was: llama.cpp supports several
    /// contexts over one model, but what the model itself keeps -- the
    /// adapters registered on it, the embedding context, the parsed templates
    /// -- is not safe from two threads, and nothing here assumes it is.
    [[nodiscard]] std::unique_lock<std::mutex> use() {
        return std::unique_lock<std::mutex>{use_};
    }

private:
    std::unique_ptr<LlamaModel> model_;
    std::mutex use_;
};

/// The shared-base registry: a weak-held map, the last sharer releasing.
class LlamaBases {
public:
    /// Loads the base when no sharer holds it.
    using Loader = std::function<std::unique_ptr<LlamaModel>(std::string& error)>;

    /// The base `key` names: the one a sharer holds, or loaded now through
    /// `load` and held for the next to share. Nullptr with `error` when the
    /// load fails. With no key, loaded for the caller alone. The load runs
    /// with no lock held -- it takes seconds, and another base's must not
    /// wait on it -- and should two land for one key at once, the first is
    /// shared and the second freed.
    [[nodiscard]] std::shared_ptr<SharedBase> acquire(const std::optional<SharedBaseKey>& key,
                                                      const Loader& load, std::string& error);

    /// How many bases some sharer holds now.
    [[nodiscard]] std::size_t resident() const;

private:
    mutable std::mutex mutex_;
    std::map<SharedBaseKey, std::weak_ptr<SharedBase>> bases_;
};

}  // namespace apogee::backends
