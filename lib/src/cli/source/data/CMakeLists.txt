# The Data layer (ADR 0001): how state and the outside world are reached --
# the contracts every implementor reads, the wire, the stores, the backends.
#
# One static library per module -- apogee_data_<module> -- linked to
# exactly the modules cmake/modules.cmake declares for it (apogee_add_module,
# source/CMakeLists.txt). This file says what each module compiles and which
# third-party code it reaches; which modules it reaches lives in the map.

apogee_add_module(contracts
    contracts/sha256.h
    contracts/sha256.cpp
    contracts/layout.h
    contracts/layout.cpp
    contracts/paths.h
    contracts/paths.cpp
    contracts/config.h
    contracts/config.cpp
    contracts/config_template.cpp
    contracts/config_edit.h
    contracts/config_edit.cpp
    contracts/host.h
    contracts/host.cpp
    contracts/assets.h
    contracts/assets.cpp
    contracts/assets_training.cpp
    contracts/assets_converter.cpp
    contracts/assets_mlx.cpp
    contracts/assets_symphonies.cpp
    contracts/errors.h
    contracts/errors.cpp
    contracts/cancellation.h
    contracts/cancellation.cpp
    contracts/types.h
    contracts/types.cpp
    contracts/behavior.h
    contracts/behavior.cpp
    contracts/provider.h
    contracts/provider.cpp
)
target_link_libraries(apogee_data_contracts PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE yaml-cpp::yaml-cpp)

apogee_add_module(transport
    transport/http_client.h
    transport/http_client.cpp
    transport/sse_parser.h
    transport/sse_parser.cpp
    transport/jsonl_framer.h
    transport/jsonl_framer.cpp
)
target_link_libraries(apogee_data_transport PRIVATE CURL::libcurl)

apogee_add_module(modelstore
    modelstore/gguf_inspect.h
    modelstore/gguf_inspect.cpp
    modelstore/kv_cache.h
    modelstore/kv_cache.cpp
    modelstore/footprint.h
    modelstore/footprint.cpp
    modelstore/mlx_info.h
    modelstore/mlx_info.cpp
    modelstore/sidecar.h
    modelstore/sidecar.cpp
    modelstore/snapshot.h
    modelstore/snapshot.cpp
    modelstore/store.h
    modelstore/store.cpp
    modelstore/hf_ref.h
    modelstore/hf_ref.cpp
)
target_link_libraries(apogee_data_modelstore PRIVATE nlohmann_json::nlohmann_json)

apogee_add_module(logger
    logger/session.h
    logger/session.cpp
    logger/operational.h
    logger/operational.cpp
)
target_link_libraries(apogee_data_logger PRIVATE nlohmann_json::nlohmann_json)

# The credential store and the one key resolver, on the contracts alone
# (tests/scripts/cmake/layering.cmake).
apogee_add_module(secrets
    secrets/secrets.h
    secrets/store.h
    secrets/store.cpp
    secrets/resolve.h
    secrets/resolve.cpp
)
target_link_libraries(apogee_data_secrets PRIVATE nlohmann_json::nlohmann_json)

apogee_add_module(embedstore
    embedstore/embedstore.h
    embedstore/store.h
    embedstore/store.cpp
    embedstore/store_impl.h
    embedstore/graph.h
    embedstore/graph.cpp
    embedstore/graph_search.h
    embedstore/graph_search.cpp
    embedstore/graph_communities.h
    embedstore/graph_communities.cpp
    embedstore/graph_dedupe.h
    embedstore/graph_dedupe.cpp
    embedstore/graph_code.h
    embedstore/graph_code.cpp
    embedstore/fts.h
    embedstore/fts.cpp
    embedstore/chunk.h
    embedstore/chunk.cpp
    embedstore/ingest.h
    embedstore/ingest.cpp
    embedstore/vector.h
    embedstore/vector.cpp
)
target_link_libraries(apogee_data_embedstore PUBLIC apogee_sqlite3
                      PRIVATE nlohmann_json::nlohmann_json)

apogee_add_module(backends
    backends/backends.h
    backends/mock.h
    backends/mock.cpp
    backends/anthropic_wire.h
    backends/anthropic_wire.cpp
    backends/anthropic.h
    backends/anthropic.cpp
    backends/codex_cli_events.h
    backends/codex_cli_events.cpp
    backends/codex_cli.h
    backends/codex_cli.cpp
    backends/gemini_cli_events.h
    backends/gemini_cli_events.cpp
    backends/gemini_cli.h
    backends/gemini_cli.cpp
    backends/ollama_cli_output.h
    backends/ollama_cli_output.cpp
    backends/ollama_cli.h
    backends/ollama_cli.cpp
    backends/openai_wire.h
    backends/openai_wire.cpp
    backends/openai.h
    backends/openai.cpp
    backends/google_wire.h
    backends/google_wire.cpp
    backends/google.h
    backends/google.cpp
    backends/cli_event.h
    backends/claude_cli.h
    backends/claude_cli.cpp
    backends/claude_cli_events.h
    backends/claude_cli_events.cpp
    backends/llama_runtime.h
    backends/llama_real.cpp
    backends/think_filter.h
    backends/think_filter.cpp
    backends/markup_filter.h
    backends/markup_filter.cpp
    backends/native_tool_calls.h
    backends/native_tool_calls.cpp
    backends/model_profile.h
    backends/model_profile.cpp
    backends/sampling.h
    backends/sampling.cpp
    backends/prompt_cache.h
    backends/prompt_cache.cpp
    backends/chat_template.h
    backends/chat_template.cpp
    backends/llamacpp_tokens.h
    backends/llamacpp_tokens.cpp
    backends/embedding_batch.h
    backends/embedding_batch.cpp
    backends/openai_embed.h
    backends/openai_embed.cpp
    backends/google_embed.h
    backends/google_embed.cpp
    backends/llamacpp_embed.h
    backends/llamacpp_embed.cpp
    backends/local_prompt.h
    backends/local_prompt.cpp
    backends/llamacpp.h
    backends/llamacpp.cpp
    backends/mlx_protocol.h
    backends/mlx_protocol.cpp
    backends/mlx_local.h
    backends/mlx_local.cpp
    backends/factory.h
    backends/factory.cpp
)
target_link_libraries(apogee_data_backends PUBLIC nlohmann_json::nlohmann_json)

# Local inference is opt-in at build time. When it is off, backends/llama_real.cpp
# compiles to the "not built in" answer and never includes llama.h -- so the
# provider, its tokenizer arithmetic, and every test over them build and run on
# the merge-blocking target, which has no llama.cpp in it.
if(APOGEE_ENABLE_LLAMA)
    # `mtmd` is linked as well as `llama`, and not only for the symbols: it
    # carries the include directory for its own headers. Without it a
    # `#include <mtmd.h>` falls through to whatever the system has -- on a
    # machine with llama.cpp installed via Homebrew that is a DIFFERENT
    # llama.cpp, and the build fails with a wall of ggml redefinitions that
    # names neither the real cause nor this file.
    #
    # `apogee_llama_chat` is llama.cpp's chat layer behind an interface of
    # standard types (backends/llama_chat.h): a library of its own so that
    # `llama-common`'s include directories -- which carry its own copy of
    # nlohmann/json, at another version -- reach that one translation unit and
    # no other. Linked PRIVATE, so they never propagate past the backends.
    add_library(apogee_llama_chat STATIC
        backends/llama_chat.h
        backends/llama_chat.cpp
    )
    target_include_directories(apogee_llama_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(apogee_llama_chat PRIVATE llama llama-common)
    target_compile_definitions(apogee_llama_chat PRIVATE APOGEE_ENABLE_LLAMA=1)
    apogee_set_target_options(apogee_llama_chat)

    target_link_libraries(apogee_data_backends PRIVATE llama mtmd apogee_llama_chat)
    target_compile_definitions(apogee_data_backends PRIVATE APOGEE_ENABLE_LLAMA=1)
endif()

apogee_add_layer()
