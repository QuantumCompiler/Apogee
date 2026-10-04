# The CLI application.
#
# Two targets, and the split between them is load-bearing:
#
#   apogee_core -- EVERY capability. Tests link this. The future HTTP server
#                  links this. It is the whole product minus an entry point.
#   apogee      -- a thin face: argv in, exit code out, nothing else.
#
# Parity across surfaces is structural because of that split (SPEC.md ->
# Principles: "one source of truth per concern"). cmake/ApogeeLinkPolicy.cmake
# fails the configure step if anything ever links `apogee`.

# Stamped into the binary at configure time. GIT_COMMIT is best-effort: a
# tarball export with no .git still builds, it just reports "unknown".
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE APOGEE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT APOGEE_GIT_COMMIT)
    set(APOGEE_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP APOGEE_BUILD_DATE "%Y-%m-%d" UTC)

add_library(apogee_core STATIC
    infrastructure/version/version.h
    infrastructure/version/version.cpp
    infrastructure/platform/child_process.h
    infrastructure/platform/child_process.cpp
    infrastructure/platform/ffmpeg.h
    infrastructure/platform/ffmpeg.cpp
    infrastructure/platform/platform.h
    infrastructure/platform/platform.cpp
    presentation/cli/command.h
    data/modelstore/gguf_inspect.h
    data/modelstore/gguf_inspect.cpp
    data/modelstore/kv_cache.h
    data/modelstore/kv_cache.cpp
    business/models/lineage.h
    business/models/lineage.cpp
    data/contracts/sha256.h
    data/contracts/sha256.cpp
    data/modelstore/sidecar.h
    data/modelstore/sidecar.cpp
    business/models/acquire.h
    business/models/acquire.cpp
    business/models/source_ollama.h
    business/models/source_ollama.cpp
    business/models/source_hf.h
    business/models/source_hf.cpp
    data/modelstore/snapshot.h
    data/modelstore/snapshot.cpp

    business/training/python_env.h
    business/training/python_env.cpp
    business/training/script_runner.h
    business/training/script_runner.cpp
    business/training/kit.h
    business/training/kit.cpp
    business/training/synth.h
    business/training/synth.cpp
    business/training/datasets.h
    business/training/datasets.cpp
    business/training/trainer.h
    business/training/trainer.cpp
    business/training/script_trainer.h
    business/training/script_trainer.cpp
    business/training/mlx_trainer.h
    business/training/mlx_trainer.cpp
    business/training/peft_trainer.h
    business/training/peft_trainer.cpp
    business/training/mock_trainer.h
    business/training/mock_trainer.cpp
    business/training/eval.h
    business/training/eval.cpp
    business/training/manifest.h
    business/training/manifest.cpp
    business/training/store.h
    business/training/store.cpp
    business/training/promote.h
    business/training/promote.cpp
    business/training/convert.h
    business/training/convert.cpp
    business/training/pipeline.h
    business/training/pipeline.cpp
    business/training/regime.h
    business/training/regime.cpp
    business/training/cycle.h
    business/training/cycle.cpp
    business/models/quantize.h
    business/models/quantize.cpp
    business/models/convert.h
    business/models/convert.cpp
    data/modelstore/store.h
    data/modelstore/store.cpp
    data/modelstore/hf_ref.h
    data/modelstore/hf_ref.cpp
    business/models/migrate.h
    business/models/migrate.cpp
    presentation/cli/model_chain.h
    presentation/cli/model_chain.cpp
    presentation/cli/models_pull.h
    presentation/cli/models_pull.cpp
    presentation/cli/models_migrate.h
    presentation/cli/models_migrate.cpp
    presentation/cli/embed.h
    presentation/cli/embed.cpp
    presentation/cli/models.h
    presentation/cli/models.cpp
    presentation/cli/registry.h
    presentation/cli/registry.cpp
    presentation/cli/root.h
    presentation/cli/root.cpp
    presentation/cli/version_command.h
    presentation/cli/version_command.cpp
    presentation/cli/config_cmd.h
    presentation/cli/config_cmd.cpp
    presentation/cli/check.h
    presentation/cli/complete_protocol.h
    presentation/cli/complete_protocol.cpp
    presentation/cli/complete_sources.cpp
    presentation/cli/uninstall.h
    presentation/cli/uninstall.cpp
    presentation/cli/check.cpp
    presentation/cli/complete.h
    presentation/cli/complete.cpp
    presentation/cli/helpers.h
    presentation/cli/helpers.cpp
    presentation/cli/tool_vectors.h
    presentation/cli/tool_vectors.cpp
    presentation/views/ask_prompt.h
    presentation/views/ask_prompt.cpp
    presentation/views/terminal.h
    presentation/views/terminal.cpp
    presentation/views/status_line.h
    presentation/views/status_line.cpp
    presentation/views/download_progress.h
    presentation/views/download_progress.cpp
    presentation/cli/interrupt.h
    presentation/cli/interrupt.cpp
    presentation/views/thinking_view.h
    presentation/views/thinking_view.cpp
    presentation/machine/json_reporter.h
    presentation/machine/json_reporter.cpp
    presentation/views/answer_view.h
    presentation/views/answer_view.cpp
    presentation/views/cli_reporter.h
    presentation/views/cli_reporter.cpp

    presentation/cli/serve_cmd.h
    presentation/cli/serve_cmd.cpp
    presentation/cli/mcp_cmd.h
    presentation/cli/mcp_cmd.cpp
    presentation/cli/permissions.h
    presentation/cli/permissions.cpp
    presentation/cli/auth_cmd.h
    presentation/cli/auth_cmd.cpp
    presentation/cli/analyze.h
    presentation/cli/analyze.cpp
    presentation/cli/agents_cmd.h
    presentation/cli/agents_cmd.cpp
    presentation/cli/knowledge_core.h
    presentation/cli/knowledge_core.cpp
    presentation/cli/graph.h
    presentation/cli/graph.cpp
    presentation/cli/datasets.h
    presentation/cli/datasets.cpp
    presentation/cli/train.h
    presentation/cli/train.cpp
    presentation/cli/knowledge.h
    presentation/cli/knowledge.cpp
    presentation/cli/chat.h
    presentation/cli/chat.cpp
    presentation/cli/chat_completer.h
    presentation/cli/chat_completer.cpp
    presentation/cli/chat_attachments.h
    presentation/cli/chat_attachments.cpp
    presentation/cli/chat_history.h
    presentation/cli/chat_history.cpp
    presentation/views/input_gate.h
    presentation/views/input_gate.cpp
    presentation/views/line_reader.h
    presentation/views/line_reader.cpp

    infrastructure/ansi/ansi.h
    infrastructure/ansi/ansi.cpp
    infrastructure/ansi/text_width.h
    infrastructure/ansi/text_width.cpp
    presentation/markdown/types.h
    presentation/markdown/inline.h
    presentation/markdown/inline.cpp
    presentation/markdown/layout.h
    presentation/markdown/layout.cpp
    presentation/markdown/stream_renderer.h
    presentation/markdown/stream_renderer.cpp

    data/logger/session.h
    data/logger/session.cpp
    data/logger/operational.h
    data/logger/operational.cpp

    data/contracts/layout.h
    data/contracts/layout.cpp
    data/contracts/paths.h
    data/contracts/paths.cpp
    business/harness/roles.h
    business/harness/roles.cpp
    data/contracts/config.h
    data/contracts/config.cpp
    data/contracts/config_template.cpp
    data/contracts/config_edit.h
    data/contracts/config_edit.cpp
    data/contracts/host.h
    data/contracts/host.cpp
    data/contracts/assets.h
    data/contracts/assets.cpp
    data/contracts/assets_training.cpp
    data/contracts/assets_converter.cpp
    data/contracts/errors.h
    data/contracts/errors.cpp
    data/contracts/cancellation.h
    data/contracts/cancellation.cpp
    data/contracts/types.h
    data/contracts/types.cpp
    data/contracts/behavior.h
    data/contracts/behavior.cpp
    data/contracts/provider.h
    data/contracts/provider.cpp
    business/harness/context_windows.h
    business/harness/context_windows.cpp
    business/harness/harness.h
    business/harness/harness.cpp

    data/backends/backends.h
    data/backends/mock.h
    data/backends/mock.cpp
    data/transport/http_client.h
    data/transport/http_client.cpp
    data/transport/sse_parser.h
    data/transport/sse_parser.cpp
    data/backends/anthropic_wire.h
    data/backends/anthropic_wire.cpp
    data/backends/anthropic.h
    data/backends/anthropic.cpp
    data/backends/codex_cli_events.h
    data/backends/codex_cli_events.cpp
    data/backends/codex_cli.h
    data/backends/codex_cli.cpp
    data/backends/gemini_cli_events.h
    data/backends/gemini_cli_events.cpp
    data/backends/gemini_cli.h
    data/backends/gemini_cli.cpp
    data/backends/ollama_cli_output.h
    data/backends/ollama_cli_output.cpp
    data/backends/ollama_cli.h
    data/backends/ollama_cli.cpp
    data/backends/openai_wire.h
    data/backends/openai_wire.cpp
    data/backends/openai.h
    data/backends/openai.cpp
    data/backends/google_wire.h
    data/backends/google_wire.cpp
    data/backends/google.h
    data/backends/google.cpp
    data/backends/cli_event.h
    data/backends/claude_cli.h
    data/backends/claude_cli.cpp
    data/transport/jsonl_framer.h
    data/transport/jsonl_framer.cpp
    data/backends/claude_cli_events.h
    data/backends/claude_cli_events.cpp
    data/backends/llama_runtime.h
    data/backends/llama_real.cpp
    data/backends/think_filter.h
    data/backends/think_filter.cpp
    data/backends/markup_filter.h
    data/backends/markup_filter.cpp
    data/backends/native_tool_calls.h
    data/backends/native_tool_calls.cpp
    data/backends/model_profile.h
    data/backends/model_profile.cpp
    data/backends/chat_template.h
    data/backends/chat_template.cpp
    data/backends/llamacpp_tokens.h
    data/backends/llamacpp_tokens.cpp
    data/backends/embedding_batch.h
    data/backends/embedding_batch.cpp
    data/backends/openai_embed.h
    data/backends/openai_embed.cpp
    data/backends/google_embed.h
    data/backends/google_embed.cpp
    data/backends/llamacpp_embed.h
    data/backends/llamacpp_embed.cpp
    data/backends/llamacpp.h
    data/backends/llamacpp.cpp
    data/backends/factory.h
    data/backends/factory.cpp

    business/agent/tool.h
    business/agent/tool.cpp
    business/agent/fetch_url.h
    business/agent/fetch_url.cpp
    business/agent/readable.h
    business/agent/readable.cpp
    business/agent/web_search.h
    business/agent/web_search.cpp

    business/agentloop/agentloop.h
    business/agentloop/reporter.h
    business/agentloop/embed_func.h
    business/agentloop/embed_func.cpp
    business/agentloop/retriever.h
    business/agentloop/retriever.cpp
    business/agentloop/rerank.h
    business/agentloop/rerank.cpp
    business/agentloop/query_rewrite.h
    business/agentloop/query_rewrite.cpp
    business/agentloop/tool_selection.h
    business/agentloop/tool_selection.cpp
    business/agentloop/tool_summary.h
    business/agentloop/tool_summary.cpp
    business/agentloop/rag.h
    business/agentloop/rag.cpp
    business/agentloop/graph_context.h
    business/agentloop/graph_context.cpp
    business/agentloop/loop.h
    business/agentloop/loop.cpp
    business/agentloop/content.h
    business/agentloop/content.cpp
    business/agentloop/budget.h
    business/agentloop/budget.cpp
    business/agentloop/attachments.h
    business/agentloop/attachments.cpp
    business/agentloop/media.h
    business/agentloop/media.cpp
    business/agentloop/question.h
    business/agentloop/question.cpp
    business/agentloop/structured.h
    business/agentloop/structured.cpp
    business/agentloop/review_context.h
    business/agentloop/review_context.cpp
    presentation/render/json_report.h
    presentation/render/json_report.cpp

    # A leaf: includes nothing else from the project, so anything may publish.
    infrastructure/events/bus.h
    infrastructure/events/bus.cpp

    # The credential store and the one key resolver. Beside the harness:
    # includes contracts/config.h and nothing from backends/.
    data/secrets/secrets.h
    data/secrets/store.h
    data/secrets/store.cpp
    data/secrets/resolve.h
    data/secrets/resolve.cpp
    business/tools/tools.h
    business/tools/args.h
    business/tools/args.cpp
    business/tools/process.h
    business/tools/process.cpp
    business/tools/fs.h
    business/tools/fs.cpp
    business/tools/shell.h
    business/tools/shell.cpp
    business/tools/git.h
    business/tools/git.cpp
    business/tools/notes.h
    business/tools/notes.cpp
    business/tools/rag_query.h
    business/tools/rag_query.cpp
    business/tools/environment.h
    business/tools/environment.cpp
    business/tools/toolsets.h
    business/tools/toolsets.cpp
    business/mcp/types.h
    business/mcp/types.cpp
    business/mcp/transport.h
    business/mcp/transport.cpp
    business/mcp/client.h
    business/mcp/client.cpp
    business/mcp/registry.h
    business/mcp/registry.cpp
    business/mcp/serve_stdio.h
    business/mcp/serve_stdio.cpp
    business/scaffold/mcp_server.h
    business/scaffold/mcp_server.cpp
    business/scaffold/agent.h
    business/scaffold/agent.cpp

    # The knowledge layer: records, the capture clerk, and the store over the
    # chunk store. Includes embedstore/, agentloop/, harness/ and platform/
    # only -- never backends/ or commands/ (tests/layering.cmake).
    business/knowledge/record.h
    business/knowledge/record.cpp
    business/knowledge/clerk.h
    business/knowledge/clerk.cpp
    business/knowledge/store.h
    business/knowledge/store.cpp
    business/knowledge/query.h
    business/knowledge/query.cpp
    business/knowledge/refine.h
    business/knowledge/refine.cpp
    business/knowledge/export.h
    business/knowledge/export.cpp

    # The knowledge-graph layer over collections: the extraction contract
    # (prompt and schema compiled in, validation in host code) and the
    # resumable build with the record pass. Model-free: generation and
    # embedding arrive as closures. Includes embedstore/, knowledge/,
    # agentloop/, harness/ and platform/ only (tests/layering.cmake).
    business/graph/extract.h
    business/graph/extract.cpp
    business/graph/build.h
    business/graph/build.cpp
    business/graph/communities.h
    business/graph/communities.cpp

    # Package headers reserved for the items that fill them. They carry no code
    # yet -- they exist so every later backlog item has one obvious home, and
    # so `#include "embedstore/embedstore.h"` compiles from the first day.
    data/embedstore/embedstore.h
    data/embedstore/store.h
    data/embedstore/store.cpp
    data/embedstore/store_impl.h
    data/embedstore/graph.h
    data/embedstore/graph.cpp
    data/embedstore/graph_search.h
    data/embedstore/graph_search.cpp
    data/embedstore/graph_communities.h
    data/embedstore/graph_communities.cpp
    data/embedstore/graph_dedupe.h
    data/embedstore/graph_dedupe.cpp
    data/embedstore/fts.h
    data/embedstore/fts.cpp
    data/embedstore/chunk.h
    data/embedstore/chunk.cpp
    data/embedstore/ingest.h
    data/embedstore/ingest.cpp
    data/embedstore/vector.h
    data/embedstore/vector.cpp
    presentation/httpserver/httpserver.h
    presentation/httpserver/http_types.h
    presentation/httpserver/http_types.cpp
    presentation/httpserver/sse_writer.h
    presentation/httpserver/sse_writer.cpp
    presentation/httpserver/sse_reporter.h
    presentation/httpserver/sse_reporter.cpp
    presentation/httpserver/session.h
    presentation/httpserver/session.cpp
    presentation/httpserver/handler.h
    presentation/httpserver/handler.cpp
    presentation/httpserver/mux.h
    presentation/httpserver/mux.cpp
    presentation/httpserver/shutdown.h
    presentation/httpserver/shutdown.cpp
    presentation/httpserver/admin_auth.h
    presentation/httpserver/admin_auth.cpp
    presentation/httpserver/jobs.h
    presentation/httpserver/jobs.cpp
    presentation/httpserver/admin_events.h
    presentation/httpserver/admin_events.cpp
    presentation/httpserver/admin_config.h
    presentation/httpserver/admin_config.cpp
    presentation/httpserver/admin_agents.h
    presentation/httpserver/admin_agents.cpp
    presentation/httpserver/admin_knowledge.h
    presentation/httpserver/admin_knowledge.cpp
    presentation/httpserver/admin_graph.h
    presentation/httpserver/admin_graph.cpp
    presentation/httpserver/admin_graphs.h
    presentation/httpserver/admin_graphs.cpp
    presentation/httpserver/admin_datasets.h
    presentation/httpserver/admin_datasets.cpp
    presentation/httpserver/admin_training.h
    presentation/httpserver/admin_training.cpp
    presentation/httpserver/admin.h
    presentation/httpserver/admin.cpp
    presentation/httpserver/admin_auth_routes.h
    presentation/httpserver/admin_auth_routes.cpp
    # The ONE translation unit that may reference listen()/accept(). The
    # no-listen symbol check allow-lists exactly this object and requires it to
    # be the only one -- see tests/no_listen_symbols.cmake.
    presentation/httpserver/serve.h
    presentation/httpserver/serve.cpp
    business/mcp/mcp.h
)

# The layer directories are the include roots (Architecture A2): a package is
# included by its short name -- `#include "agentloop/loop.h"` -- whichever layer
# holds it, so moving a package between layers changes no include line. The
# flat source/ root is not one: a stale path cannot resolve two ways.
target_include_directories(apogee_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/presentation"
    "${CMAKE_CURRENT_SOURCE_DIR}/business"
    "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${CMAKE_CURRENT_SOURCE_DIR}/infrastructure")

target_compile_definitions(apogee_core PRIVATE
    APOGEE_VERSION="${PROJECT_VERSION}"
    APOGEE_GIT_COMMIT="${APOGEE_GIT_COMMIT}"
    APOGEE_BUILD_DATE="${APOGEE_BUILD_DATE}"
)

target_link_libraries(apogee_core PUBLIC CLI11::CLI11 nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp
                      PRIVATE CURL::libcurl replxx::replxx apogee_sqlite3 httplib::httplib
                              nlohmann_json_schema_validator::validator)

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
    # no other. Linked PRIVATE, so they never propagate into apogee_core.
    add_library(apogee_llama_chat STATIC
        data/backends/llama_chat.h
        data/backends/llama_chat.cpp
    )
    target_include_directories(apogee_llama_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/data")
    target_link_libraries(apogee_llama_chat PRIVATE llama llama-common)
    target_compile_definitions(apogee_llama_chat PRIVATE APOGEE_ENABLE_LLAMA=1)
    apogee_set_target_options(apogee_llama_chat)

    target_link_libraries(apogee_core PRIVATE llama mtmd apogee_llama_chat)
    target_compile_definitions(apogee_core PRIVATE APOGEE_ENABLE_LLAMA=1)
endif()

apogee_set_target_options(apogee_core)

add_executable(apogee main.cpp)
target_link_libraries(apogee PRIVATE apogee_core)
apogee_set_target_options(apogee)

# The install path: the binary, and nothing else. `make install` is a thin
# caller into this, so there is exactly one install path -- the same shape as
# the build (one CMake definition, a Makefile wrapper that adds no steps).
#
# apogee_core is deliberately NOT installed: it is a static library and an
# implementation detail of the executable, and installing it would invite
# something outside this project to link the whole product.
#
# This rule seeds no ~/.apogee/ assets on purpose. The full install contract --
# the data-directory layout, the config template, shell completions, and the
# byte-identical parity between this path and scripts/install.sh validated by
# `apogee check` -- is owned by the install-check-lifecycle backlog item.
# SPEC.md -> "No silent install drift" requires any asset landing under
# ~/.apogee/ to join EVERY install path in the same commit, so this one starts
# empty rather than shipping half a contract that check cannot yet verify.
# COMPONENT is load-bearing, not decoration.
#
# `FetchContent_MakeAvailable` adds each dependency's own `install()` rules to
# THIS project, so a plain `cmake --install` installs replxx's headers, static
# library, and CMake package config alongside our binary. That is not merely
# untidy: the installed replxx config declares `Threads::Threads` without a
# `find_package(Threads)`, and our `FIND_PACKAGE_ARGS` then PREFERS that
# installed copy on the next configure -- so `make install` broke every
# subsequent build of the project. Found in the wild, 2026-09-06.
#
# Naming a component and installing only that component makes the install
# exactly what we declare, whatever a dependency declares for itself.
install(TARGETS apogee RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT apogee)
