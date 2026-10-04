#include "backends/llamacpp_embed.h"

#include <stdexcept>

#include "backends/embedding_batch.h"

namespace apogee::backends {

std::vector<std::vector<float>> embed_with_llama(LlamaModel& model,
                                                 const std::vector<std::string>& inputs,
                                                 const harness::CancellationToken& cancellation) {
    std::vector<std::vector<float>> vectors;
    vectors.reserve(inputs.size());

    for (const auto& [begin, end] : batch_ranges(inputs.size(), kTextsPerLlamaBatch)) {
        cancellation.throw_if_cancelled();
        const std::vector<std::string> slice(inputs.begin() + static_cast<std::ptrdiff_t>(begin),
                                             inputs.begin() + static_cast<std::ptrdiff_t>(end));
        std::string error;
        std::vector<std::vector<float>> got = model.embed_batch(slice, error);
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
        if (got.size() != slice.size()) {
            throw std::runtime_error("the runtime returned " + std::to_string(got.size()) +
                                     " vector(s) for " + std::to_string(slice.size()) +
                                     " input(s)");
        }
        for (std::vector<float>& vector : got) {
            vectors.push_back(std::move(vector));
        }
    }
    return vectors;
}

}  // namespace apogee::backends
