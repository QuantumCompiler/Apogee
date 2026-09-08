#include "agentloop/rag.h"

#include <algorithm>
#include <system_error>

#include "embedstore/store.h"

namespace apogee::agentloop {

std::string render_rag_context(const std::vector<std::string>& chunks) {
    if (chunks.empty()) {
        return {};
    }

    // Framed as retrieved excerpts, explicitly. Without that framing a model
    // reads an injected document as something the user said and answers about
    // the document rather than about the question -- and the more relevant the
    // retrieval, the more confidently it does so.
    std::string out =
        "The following excerpts were retrieved from the user's documents and may be relevant. "
        "Use them if they help; ignore them if they do not, and do not mention them unless they "
        "informed your answer.\n";

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        out += "\n--- excerpt " + std::to_string(index + 1) + " ---\n";
        out += chunks[index];
        out += "\n";
    }
    return out;
}

RagResult build_rag_prefix(const std::filesystem::path& store_path, const std::string& question,
                           int limit) {
    RagResult result;

    std::error_code code;
    if (!std::filesystem::exists(store_path, code)) {
        // Not an error that should stop the turn: answering without retrieved
        // context is far better than refusing to answer.
        result.error = "no collection at " + store_path.string();
        return result;
    }

    try {
        const embedstore::Store store{store_path};
        const std::vector<embedstore::SearchHit> hits = store.search(question, limit);
        if (hits.empty()) {
            // A normal outcome. A corpus that has nothing to say about this
            // question is not a fault.
            result.retriever = "lexical";
            return result;
        }

        std::vector<std::string> texts;
        texts.reserve(hits.size());
        for (const embedstore::SearchHit& hit : hits) {
            texts.push_back(hit.chunk.text);
        }

        result.chunks = static_cast<std::int64_t>(hits.size());
        result.top_score = hits.front().score;
        result.retriever = hits.front().retriever;
        result.prefix.push_back(harness::ChatMessage::system(render_rag_context(texts)));
    } catch (const std::exception& e) {
        result.prefix.clear();
        result.chunks = 0;
        result.error = e.what();
    }
    return result;
}

}  // namespace apogee::agentloop
