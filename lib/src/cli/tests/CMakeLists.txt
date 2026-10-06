# The CLI application's tests. Directory layout mirrors source/'s packages.
#
# Note what these targets link: the modules and Catch2 -- NOT the `apogee`
# executable. That is the library-first split doing its job, and
# apogee_assert_link_policy() in the top-level CMakeLists fails the configure
# step if it ever stops being true.
#
# Tests must stay hermetic: no network, no models, no writes outside the test's
# own temp directory. A test that needs a live provider is not a unit test.

include(Catch)

# The suites, one OBJECT library per layer (Architecture A4, ADR 0004): a
# layer's tests link that layer's modules and the layers below, so a test that
# includes a header from above its layer does not compile -- a behavior that
# spans layers is tested in the highest layer it touches. The support files
# each sit with the lowest layer whose headers they include.
#
# One executable still runs them all, under its old name and with every test
# name unchanged: the libraries are how the suites COMPILE, not how they run.

add_library(apogee_tests_infrastructure OBJECT
    infrastructure/platform/platform_test.cpp
    infrastructure/platform/pid_lock_test.cpp
    infrastructure/platform/child_process_test.cpp
    infrastructure/platform/ffmpeg_test.cpp
    infrastructure/ansi/ansi_test.cpp
    infrastructure/events/bus_test.cpp
    support/env_guard.h
    support/env_guard.cpp
    support/file_time.h
    support/file_time.cpp
    support/fake_child.h
    support/fake_mlx_driver.h
    support/fake_ffmpeg.h
    support/gguf_builder.h
    support/mlx_model.h
    support/terminal_model.h
    support/terminal_model.cpp
    support/embedding_fixtures.h
)
target_link_libraries(apogee_tests_infrastructure PRIVATE apogee_infrastructure Catch2::Catch2)

add_library(apogee_tests_data OBJECT
    data/contracts/config_test.cpp
    data/contracts/config_edit_test.cpp
    data/contracts/host_test.cpp
    data/contracts/paths_test.cpp
    data/contracts/types_test.cpp
    support/channel_guard.h
    support/channel_guard.cpp
    data/backends/mock_test.cpp
    data/transport/sse_parser_test.cpp
    data/transport/http_client_test.cpp
    data/backends/anthropic_test.cpp
    data/backends/factory_test.cpp
    data/backends/openai_test.cpp
    data/backends/google_test.cpp
    data/backends/openai_embed_test.cpp
    data/backends/google_embed_test.cpp
    data/backends/embedding_batch_test.cpp
    data/backends/llamacpp_embed_test.cpp
    data/backends/llamacpp_test.cpp
    data/backends/llamacpp_prompt_cache_test.cpp
    data/backends/prompt_cache_test.cpp
    data/backends/sampling_test.cpp
    data/backends/claude_cli_events_test.cpp
    data/backends/claude_cli_test.cpp
    data/backends/ollama_cli_test.cpp
    data/backends/codex_cli_test.cpp
    data/backends/gemini_cli_test.cpp
    data/backends/mlx_protocol_test.cpp
    data/backends/mlx_local_test.cpp
    data/backends/mlx_driver_test.cpp
    data/modelstore/gguf_inspect_test.cpp
    data/modelstore/kv_cache_test.cpp
    data/modelstore/footprint_test.cpp
    data/modelstore/mlx_info_test.cpp
    data/contracts/sha256_test.cpp
    data/modelstore/store_test.cpp
    data/modelstore/sidecar_test.cpp
    data/transport/jsonl_framer_test.cpp
    data/backends/think_filter_test.cpp
    data/backends/markup_filter_test.cpp
    data/backends/native_tool_calls_test.cpp
    data/backends/model_profile_test.cpp
    data/backends/chat_template_test.cpp
    data/embedstore/store_test.cpp
    data/embedstore/vector_test.cpp
    data/embedstore/fts_query_test.cpp
    data/embedstore/chunk_test.cpp
    data/embedstore/ingest_test.cpp
    data/logger/session_test.cpp
    data/secrets/store_test.cpp
    data/secrets/resolve_test.cpp
    data/contracts/assets_test.cpp
    data/embedstore/metadata_test.cpp
    data/embedstore/graph_test.cpp
    data/embedstore/graph_search_test.cpp
    data/embedstore/graph_communities_test.cpp
    data/embedstore/graph_dedupe_test.cpp
    data/embedstore/graph_multi_test.cpp
    data/embedstore/graph_code_test.cpp
    data/embedstore/graph_navigate_test.cpp
    support/graph_fixture.h
    support/graph_fixture.cpp
    support/fake_transport.h
    support/fake_llama.h
    support/fake_transport.cpp
    support/media_fakes.h
)
target_link_libraries(apogee_tests_data PRIVATE apogee_data apogee_infrastructure Catch2::Catch2)

add_library(apogee_tests_business OBJECT
    business/harness/harness_test.cpp
    business/harness/factory_harness_test.cpp
    business/harness/provider_capability_test.cpp
    business/harness/llamacpp_harness_test.cpp
    business/harness/roles_test.cpp
    business/harness/residency_test.cpp
    business/models/lineage_test.cpp
    business/models/acquire_test.cpp
    business/models/convert_test.cpp
    business/models/migrate_test.cpp
    business/models/quantize_test.cpp
    business/models/source_ollama_test.cpp
    business/models/source_hf_test.cpp
    business/agentloop/rag_test.cpp
    business/agentloop/retrieval_test.cpp
    business/agentloop/rerank_test.cpp
    business/agentloop/query_rewrite_test.cpp
    business/agentloop/tool_selection_test.cpp
    business/agentloop/tool_summary_test.cpp
    business/agentloop/conformance_test.cpp
    business/agent/tool_test.cpp
    business/agent/fetch_url_test.cpp
    business/agent/web_search_test.cpp
    business/agent/readable_test.cpp
    business/agentloop/loop_test.cpp
    business/agentloop/anthropic_loop_test.cpp
    business/agentloop/content_test.cpp
    business/agentloop/budget_test.cpp
    business/agentloop/attachments_test.cpp
    business/agentloop/media_test.cpp
    business/agentloop/question_test.cpp
    business/agentloop/thinking_test.cpp
    business/agentloop/recall_test.cpp
    business/agentloop/side_call_test.cpp
    business/agentloop/member_call_test.cpp
    business/agentloop/validate_test.cpp
    business/tools/fs_test.cpp
    business/tools/shell_test.cpp
    business/tools/notes_test.cpp
    business/tools/git_test.cpp
    business/tools/rag_query_test.cpp
    business/tools/gate_test.cpp
    business/tools/environment_test.cpp
    business/tools/toolsets_test.cpp
    business/tools/consult_test.cpp
    business/tools/graph_nav_test.cpp
    business/tools/tool_args_validation_test.cpp
    business/mcp/types_test.cpp
    business/mcp/transport_test.cpp
    business/mcp/client_test.cpp
    business/mcp/registry_test.cpp
    business/mcp/serve_stdio_test.cpp
    business/scaffold/mcp_server_test.cpp
    business/scaffold/agent_test.cpp
    business/agentloop/structured_test.cpp
    business/agentloop/review_context_test.cpp
    business/knowledge/record_test.cpp
    business/knowledge/clerk_test.cpp
    business/knowledge/store_test.cpp
    business/knowledge/query_test.cpp
    business/knowledge/refine_test.cpp
    business/knowledge/export_test.cpp
    business/graph/extract_test.cpp
    business/graph/build_test.cpp
    business/graph/records_test.cpp
    business/agentloop/graph_context_test.cpp
    business/graph/communities_test.cpp
    business/graph/graph_multi_test.cpp
    business/graph/code_extract_test.cpp
    business/graph/code_parser_test.cpp
    business/graph/code_resolve_test.cpp
    business/graph/code_build_test.cpp
    business/graph/navigate_test.cpp
    business/graph/report_test.cpp
    business/graph/export_test.cpp
    business/agentloop/graph_named_test.cpp
    business/training/script_runner_test.cpp
    business/training/python_env_test.cpp
    business/training/kit_test.cpp
    business/training/synth_test.cpp
    business/training/datasets_test.cpp
    business/training/prepare_script_test.cpp
    business/training/trainer_test.cpp
    business/training/progress_contract_test.cpp
    business/training/manifest_test.cpp
    business/training/store_test.cpp
    business/training/eval_test.cpp
    business/training/promote_test.cpp
    business/training/convert_test.cpp
    business/training/mlx_convert_test.cpp
    business/training/pipeline_test.cpp
    business/training/regime_test.cpp
    business/training/cycle_test.cpp
    business/tasks/task_test.cpp
    business/tasks/ledger_test.cpp
    business/tasks/policy_test.cpp
    business/tasks/unattended_test.cpp
    business/tasks/runner_test.cpp
    business/tasks/view_test.cpp
    support/fake_mcp_server.cpp
    support/fake_mcp_server.h
)
target_link_libraries(apogee_tests_business PRIVATE apogee_business apogee_data
                      apogee_infrastructure Catch2::Catch2)

add_library(apogee_tests_presentation OBJECT
    presentation/cli/registry_test.cpp
    presentation/cli/root_test.cpp
    presentation/cli/models_pull_test.cpp
    presentation/cli/models_migrate_test.cpp
    presentation/cli/models_convert_test.cpp
    presentation/cli/models_mlx_test.cpp
    presentation/cli/model_chain_test.cpp
    presentation/cli/complete_sources_test.cpp
    presentation/cli/models_test.cpp
    presentation/cli/suite_residency_test.cpp
    presentation/cli/config_cmd_test.cpp
    presentation/cli/attachment_guard_test.cpp
    presentation/operations/retrieval_test.cpp
    presentation/operations/suites_test.cpp
    presentation/cli/helpers_test.cpp
    presentation/cli/tool_vectors_test.cpp
    presentation/views/thinking_view_test.cpp
    presentation/views/status_line_test.cpp
    presentation/views/download_progress_test.cpp
    presentation/views/cli_reporter_test.cpp
    presentation/views/answer_view_test.cpp
    presentation/markdown/inline_test.cpp
    presentation/markdown/stream_renderer_test.cpp
    presentation/markdown/corpus_test.cpp
    presentation/cli/chat_test.cpp
    presentation/cli/check_test.cpp
    presentation/cli/lifecycle_test.cpp
    presentation/cli/reset_test.cpp
    presentation/machine/machine_mode_test.cpp
    presentation/views/line_reader_test.cpp
    presentation/cli/chat_attachments_test.cpp
    presentation/cli/chat_completer_test.cpp
    presentation/httpserver/handler_test.cpp
    presentation/httpserver/sse_test.cpp
    presentation/httpserver/session_test.cpp
    presentation/httpserver/serve_test.cpp
    presentation/httpserver/admin_auth_test.cpp
    presentation/httpserver/admin_config_test.cpp
    presentation/httpserver/admin_events_test.cpp
    presentation/httpserver/jobs_test.cpp
    presentation/httpserver/parity_test.cpp
    presentation/cli/leak_test.cpp
    presentation/httpserver/admin_auth_routes_test.cpp
    presentation/cli/permissions_test.cpp
    presentation/render/json_report_test.cpp
    presentation/render/graph_report_test.cpp
    presentation/cli/analyze_test.cpp
    presentation/cli/agents_test.cpp
    presentation/httpserver/admin_agents_test.cpp
    presentation/cli/knowledge_test.cpp
    presentation/httpserver/admin_knowledge_test.cpp
    presentation/cli/graph_test.cpp
    presentation/cli/graph_code_test.cpp
    presentation/cli/graph_navigate_test.cpp
    presentation/cli/graph_artifacts_test.cpp
    presentation/httpserver/admin_graph_test.cpp
    presentation/httpserver/admin_graphs_test.cpp
    presentation/httpserver/admin_graph_navigate_test.cpp
    presentation/httpserver/admin_suites_test.cpp
    presentation/cli/datasets_test.cpp
    presentation/cli/train_test.cpp
    presentation/cli/task_test.cpp
    presentation/httpserver/admin_datasets_test.cpp
    presentation/httpserver/admin_training_test.cpp
    presentation/httpserver/admin_tasks_test.cpp
    support/fake_command.h
    support/fake_command.cpp
    support/cli_home.h
    support/cli_home.cpp
)
target_link_libraries(apogee_tests_presentation PRIVATE apogee_presentation apogee_business
                      apogee_data apogee_infrastructure Catch2::Catch2)

# The cross-cutting suites in crosscutting/ -- the package headers, the smoke
# test, the bundled agents -- read the whole product, so they compile against
# all of it.
add_executable(apogee_tests
    crosscutting/smoke_test.cpp
    crosscutting/packages_test.cpp
    crosscutting/bundled_agents_test.cpp
)

set(APOGEE_TEST_TARGETS apogee_tests_infrastructure apogee_tests_data apogee_tests_business
                        apogee_tests_presentation apogee_tests)
foreach(target IN LISTS APOGEE_TEST_TARGETS)
    # The template-drift test reads the checked-in sample and compares it byte
    # for byte with the template compiled into the binary. It needs to find the
    # asset without depending on the working directory ctest happens to use.
    target_compile_definitions(${target} PRIVATE
        APOGEE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
        # Recorded vendor-CLI JSONL, replayed by the framing suite. Same reason
        # as above: found by absolute path, not by whatever directory ctest ran in.
        APOGEE_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        # The cross-surface guard scan reads the command sources themselves: the
        # assertion that survives is "every surface calls it", which needs the tree.
        APOGEE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../source"
        # The training protocol's golden fixture and the stub Python modules the
        # shipped drivers are run under.
        APOGEE_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        # The vendored converter, byte-matched to its compiled-in copy.
        APOGEE_THIRD_PARTY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../third_party"
    )

    # So tests can `#include "support/fake_command.h"` the same way source files
    # include across packages.
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    apogee_set_target_options(${target})
endforeach()

# The migration test builds a schema-v2 database by hand, through SQLite
# itself, so the v3 migration runs against a real predecessor -- the same
# amalgamation the store uses, which `embedstore` links publicly, so it
# reaches the tests with it and is never linked twice.
target_link_libraries(apogee_tests PRIVATE
    apogee_tests_infrastructure apogee_tests_data apogee_tests_business apogee_tests_presentation
    apogee_core Catch2::Catch2WithMain)

catch_discover_tests(apogee_tests)

# The binary's own contract, exercised the way a user meets it. These run the
# built executable as a subprocess; running a target is not linking it, so the
# link policy above still holds.
add_test(NAME cli.bare_prints_help COMMAND apogee)
set_tests_properties(cli.bare_prints_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: apogee")

add_test(NAME cli.version_flag COMMAND apogee --version)
set_tests_properties(cli.version_flag PROPERTIES
    PASS_REGULAR_EXPRESSION "apogee [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli.version_subcommand COMMAND apogee version)
set_tests_properties(cli.version_subcommand PROPERTIES
    PASS_REGULAR_EXPRESSION "apogee [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli.help_lists_subcommands COMMAND apogee --help)
set_tests_properties(cli.help_lists_subcommands PROPERTIES
    PASS_REGULAR_EXPRESSION "version")

add_test(NAME cli.unknown_subcommand_fails COMMAND apogee no-such-command)
set_tests_properties(cli.unknown_subcommand_fails PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.help_lists_config COMMAND apogee --help)
set_tests_properties(cli.help_lists_config PROPERTIES PASS_REGULAR_EXPRESSION "config")

# The layering scan: the module map held to the includes in both directions,
# and the named rules finer than a layer (harness never includes backends).
add_test(NAME harness.layering
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../source
        -DAPOGEE_MODULE_MAP=${CMAKE_SOURCE_DIR}/cmake/modules.cmake
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/layering.cmake"
)

# The law next to the code, held true (A5): the eight layer cards present and
# within budget, the test tree mirroring the modules, the ADR index whole, and
# ADR 0001's and ADR 0008's statement of the law equal to the module map.
add_test(NAME harness.layer_context
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_CLI_DIR=${CMAKE_SOURCE_DIR}
        -DAPOGEE_ADR_DIR=${CMAKE_SOURCE_DIR}/../../documentation/adrs/cli
        -DAPOGEE_MODULE_MAP=${CMAKE_SOURCE_DIR}/cmake/modules.cmake
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/layer_context.cmake"
)

# `apogee complete` end to end -- the walking skeleton, fully offline against
# the mock backend. The pattern every later CLI test reuses.
add_test(NAME cli.complete_lifecycle
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_BIN=$<TARGET_FILE:apogee>
        -DAPOGEE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/complete-e2e-home
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/complete_e2e.cmake"
)

# The interactive-never-listens invariant (SPEC.md -> Principles), test-locked
# the day the first interactive command exists. TWO checks, because neither is
# sufficient on its own -- see the header comments in each for why.
#
#   symbols: deterministic; catches code calling listen/accept, ours and any
#            static dependency's, attributed to the object that made the call.
#   runtime: catches a spawned child process holding a port.
#
# Every library the executable links THAT THIS BUILD PRODUCES is scanned --
# static archives and shared libraries alike (llama.cpp is shared under the
# llama preset, and a shared library's imports never show in the executable's
# own symbol table) -- collected by walking the link graph, so a new in-process
# dependency is covered without anyone remembering to list it. Imported system
# libraries are the platform's, not Apogee's code, and are left out. Exactly
# one object may reference listen/accept: httpserver/serve.cpp, the
# `apogee serve` listener, allow-listed by name.
function(apogee_collect_static_archives target visited_var files_var)
    set(visited ${${visited_var}})
    set(files ${${files_var}})
    if(NOT TARGET "${target}")
        return()
    endif()
    get_target_property(aliased "${target}" ALIASED_TARGET)
    if(aliased)
        set(target "${aliased}")
    endif()
    if("${target}" IN_LIST visited)
        return()
    endif()
    list(APPEND visited "${target}")
    get_target_property(type "${target}" TYPE)
    get_target_property(imported "${target}" IMPORTED)
    if(type STREQUAL "STATIC_LIBRARY")
        list(APPEND files "$<TARGET_FILE:${target}>")
    elseif((type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY") AND NOT imported)
        list(APPEND files "$<TARGET_FILE:${target}>")
    endif()
    set(deps "")
    # What the final link line actually carries. A static library has no link
    # step of its own: what reaches the executable is its
    # INTERFACE_LINK_LIBRARIES, where CMake records even a PRIVATE dependency
    # (as $<LINK_ONLY:...>). Its LINK_LIBRARIES are compile-time usage only,
    # and walking them scanned archives the linker never sees -- llama.cpp's
    # vendored httplib, cut from `llama-common`'s interface (third_party/
    # CMakeLists.txt, 25b), is the case. An executable or a shared library
    # links its LINK_LIBRARIES into itself, so both lists are walked there.
    set(link_properties LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    if(type STREQUAL "STATIC_LIBRARY")
        set(link_properties INTERFACE_LINK_LIBRARIES)
    endif()
    foreach(property IN LISTS link_properties)
        get_target_property(value "${target}" ${property})
        if(value)
            list(APPEND deps ${value})
        endif()
    endforeach()
    foreach(dep IN LISTS deps)
        if(dep MATCHES "^\\$<LINK_ONLY:(.+)>$")
            set(dep "${CMAKE_MATCH_1}")
        elseif(dep MATCHES "^\\$<" OR dep MATCHES "^::@")
            continue()
        endif()
        apogee_collect_static_archives("${dep}" visited files)
    endforeach()
    set(${visited_var} "${visited}" PARENT_SCOPE)
    set(${files_var} "${files}" PARENT_SCOPE)
endfunction()

set(APOGEE_LINKED_VISITED "")
set(APOGEE_LINKED_ARCHIVES "")
apogee_collect_static_archives(apogee APOGEE_LINKED_VISITED APOGEE_LINKED_ARCHIVES)
string(JOIN "|" APOGEE_LINKED_ARCHIVE_LIST ${APOGEE_LINKED_ARCHIVES})
add_test(NAME cli.no_listen_symbols
    COMMAND "${CMAKE_COMMAND}"
        "-DAPOGEE_ARCHIVES=${APOGEE_LINKED_ARCHIVE_LIST}"
        -DAPOGEE_LISTENER_OBJECT=serve.cpp${CMAKE_CXX_OUTPUT_EXTENSION}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/no_listen_symbols.cmake"
)

# `apogee serve` exists, and its help must say what a server deployment needs
# to know: the bind policy.
add_test(NAME cli.serve_help COMMAND apogee serve --help)
set_tests_properties(cli.serve_help PROPERTIES PASS_REGULAR_EXPRESSION "allow-remote")

# The HTTP API reference, pinned to the route table in both directions.
add_test(NAME cli.http_api_conformance
    COMMAND "${CMAKE_COMMAND}"
        -DMUX_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../source/presentation/httpserver/mux.cpp
        -DAPI_DOC=${CMAKE_CURRENT_SOURCE_DIR}/../../../documentation/reference/http-api.md
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/http_api_conformance.cmake"
)

add_test(NAME cli.install_is_ours_only
    COMMAND "${CMAKE_COMMAND}"
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-only
        -DMAKEFILE=${CMAKE_CURRENT_SOURCE_DIR}/../Makefile
        -DEXE_NAME=$<TARGET_FILE_NAME:apogee>
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/install_is_ours_only.cmake"
)

add_test(NAME cli.no_vendor_credentials
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../source
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/no_vendor_credentials.cmake"
)

# One role-resolution chain, and only one. See the file for the bug class.
add_test(NAME cli.one_role_resolver
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../source
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/one_role_resolver.cmake"
)

# One key-resolution chain, and only one: the same bug class, for secrets.
add_test(NAME cli.one_key_resolver
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../source
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/one_key_resolver.cmake"
)

add_test(NAME cli.test_names
    COMMAND "${CMAKE_COMMAND}"
        -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/test_names.cmake"
)

if(UNIX)
    # A dev-channel build of the executable, for the parity case below (M10):
    # main.cpp over the same apogee_core, stamped through the one function
    # `apogee` is (apogee_channel_executable) -- so it is what `make install
    # MODE=dev` builds, under the name it installs as. Built on demand by the
    # fixture, never by a plain build: what CI builds stays the release
    # executable alone, and a channel costs the suite one compile and a link.
    add_executable(apogee_channel_probe EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/source/main.cpp")
    target_link_libraries(apogee_channel_probe PRIVATE apogee_core)
    apogee_set_target_options(apogee_channel_probe)
    apogee_channel_executable(apogee_channel_probe dev)
    set_target_properties(apogee_channel_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/channel-probe")
    add_test(NAME cli.channel_probe_build
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target apogee_channel_probe)
    set_tests_properties(cli.channel_probe_build PROPERTIES
        FIXTURES_SETUP channel_probe TIMEOUT 900)

    # The install-parity gate. See tests/scripts/sh/install_parity.sh for why an exception
    # here is where the next drift hides -- and, since M10, a channel install held to the
    # same tree under its own root.
    add_test(NAME cli.install_parity
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/install_parity.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/install-parity
            $<TARGET_FILE:apogee_channel_probe>
    )
    set_tests_properties(cli.install_parity PROPERTIES
        TIMEOUT 60 FIXTURES_REQUIRED channel_probe)

    # The completion stubs in the real shells (bash always; zsh and fish when
    # present) against the real binary -- what a stub SENDS `__complete`, which
    # the protocol's own tests cannot see. See the script for the two zsh bugs
    # that hid nested verbs there.
    add_test(NAME cli.shell_completion
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/shell_completion_check.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/shell-completion
    )
    set_tests_properties(cli.shell_completion PROPERTIES TIMEOUT 60)

    add_test(NAME cli.machine_mode
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/machine_mode_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/machine-mode
    )
    set_tests_properties(cli.machine_mode PROPERTIES TIMEOUT 120)

    # auto_rag's two shell-only properties: `--rag ""` as the off switch (CMake's
    # execute_process cannot pass an EMPTY argument at all), and the key being
    # re-read at turn build rather than at startup.
    add_test(NAME cli.auto_rag
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/auto_rag_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/auto-rag
    )
    set_tests_properties(cli.auto_rag PROPERTIES TIMEOUT 60)

    # Suites (27d) on the real binary: add-suite through the one editor, a
    # chat whose utility resolves through the suite rung (the helper titles
    # it), /suite switching the bundle and off restoring the pointers, both
    # surviving resume.
    add_test(NAME cli.suites_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/suites_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/suites
    )
    set_tests_properties(cli.suites_lifecycle PROPERTIES TIMEOUT 120)

    # The task runner (27h) on the real binary, scripted mocks and no input:
    # goal -> plan -> an incomplete round -> the corrective round -> done,
    # with the ledger's transition sequence; status and list; an ask-level
    # tool denied and recorded with stdin held open (a prompt would hang);
    # the budget and the breaker each naming the unpassed checks; the lock
    # refusing a second run by name; a task killed mid-round resumed to done
    # with no turn said twice; cancel and halt across processes; and chats
    # delete refusing a live task's conversation. Since 27i the autonomy
    # policy too: a granted write run unprompted and recorded with its
    # target, a declared answer consumed and recorded (and without one, the
    # task failing naming the question), and a grant wider than the config
    # or the agent refused naming the rule, no task made. Since 27j the
    # surfaces: a run under --output-format stream-json whose task events --
    # task_started's history, then one per transition -- are the ledger's
    # transition sequence one for one, a driver's reconstruction from the
    # stream equal to `task status --output-format json`, and a halted task
    # resumed in machine mode opening with its whole history.
    add_test(NAME cli.task_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/task_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/task-e2e
    )
    set_tests_properties(cli.task_lifecycle PROPERTIES TIMEOUT 180)

    add_test(NAME cli.complete_opens_no_listening_socket
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/no_listen_check.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/no-listen-home
            # The mlx backend's phase (27a): its real driver under stubs, the
            # whole process tree sampled -- and since 27c a served mlx turn,
            # serve's own pid the only listener, its driver never one.
            ${CMAKE_CURRENT_SOURCE_DIR}/data/backends/scripts
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mlx/model
    )
    set_tests_properties(cli.complete_opens_no_listening_socket PROPERTIES TIMEOUT 60)

    # The mlx backend (27a) on the real binary with its real driver under stub
    # mlx packages: check, a byte-clean complete, a tool-using chat on one
    # driver, /exit, and a refusal naming its fix -- and since 27c a vision
    # model's picture read as it is through a stub mlx-vlm (and, without it,
    # the doctor's warning and the refusal), a text model's picture described
    # by the vision role, and a run promoted with --target mlx then asked.
    # Apple silicon only.
    add_test(NAME cli.mlx_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/mlx_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/mlx-e2e-home
            ${CMAKE_CURRENT_SOURCE_DIR}/data/backends/scripts
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mlx/model
    )
    set_tests_properties(cli.mlx_lifecycle PROPERTIES TIMEOUT 120)

    # MLX model operations (27b) on the real binary: `models convert --mlx`
    # through the seeded driver under the stub mlx packages -- the set landed
    # by its hash, listed and checked, a REAL Ctrl-C mid-conversion leaving the
    # store as it was, and the set deleted whole. Apple silicon only.
    add_test(NAME cli.mlx_models_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/mlx_models_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/mlx-models-e2e-home
            ${CMAKE_CURRENT_SOURCE_DIR}/data/backends/scripts
    )
    set_tests_properties(cli.mlx_models_lifecycle PROPERTIES TIMEOUT 120)

    # The other half of that boundary: serve DOES hold a port, fails closed on a
    # non-loopback bind, streams to a real client, mints sessions the terminal
    # can resume, serves the task reads as the CLI prints them while no task
    # control answers (27j), and stops on SIGTERM. Properties of the process,
    # not a class.
    add_test(NAME cli.serve_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/serve_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/serve-e2e-home
    )
    set_tests_properties(cli.serve_lifecycle PROPERTIES TIMEOUT 120)

    # The MCP client on the real binary: a scaffolded server created, tested
    # through the production client, connected by a tool-using run, a dying
    # one skipped with its stderr tail, and the in-binary server as one more
    # server. Needs python3 for the scaffold (skips itself without one).
    add_test(NAME cli.mcp_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/mcp_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/mcp-e2e-home
    )
    set_tests_properties(cli.mcp_lifecycle PROPERTIES TIMEOUT 120)

    # `analyze` on the real binary: the bundled agents on the mock backend
    # against a fixture repository -- a ref reviewed without being checked out,
    # the refs decided by flags, the report rendered and saved, the validator's
    # retry and its honest flag, a vendor CLI refused, and chat's --branch.
    add_test(NAME cli.analyze_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/analyze_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/analyze-e2e
    )
    set_tests_properties(cli.analyze_lifecycle PROPERTIES TIMEOUT 120)

    # The knowledge layer on the real binary: capture on the scripted mock,
    # --dry-run's zero footprint, overrides, a supersede, the raw archive
    # present and the index text free of the attribution, the doctor's rows,
    # a vendor CLI refused, and chat's /capture through the same core.
    add_test(NAME cli.knowledge_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/knowledge_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/knowledge-e2e
    )
    set_tests_properties(cli.knowledge_lifecycle PROPERTIES TIMEOUT 120)

    # The knowledge graph on the real binary: a build over an ingested
    # collection on the scripted mock as extractor, --dry-run's zero
    # footprint, the enable write through the one editor, a --rag turn whose
    # status line counts the graph entities while the answer carries the
    # entity of a chunk the query never matched, a captured record as a
    # decision node, `knowledge query --graph`, and delete.
    add_test(NAME cli.graph_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/graph_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/graph-e2e
    )
    set_tests_properties(cli.graph_lifecycle PROPERTIES TIMEOUT 120)

    # The code graph on the real binary (27k): the model-free pass on a
    # sandbox with zero backends, no key and the network pointed at a closed
    # port -- build over a source tree (a skipped file named, a vendored one
    # left out), stats' origin split, show across files to the line, an edit
    # re-parsed alone and converging on a fresh build, communities clustered
    # with the summaries absent, dedupe's per-layer report -- then expansion
    # over code beside a collection on a scripted mock.
    add_test(NAME cli.code_graph_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/code_graph_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/code-graph-e2e
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    set_tests_properties(cli.code_graph_lifecycle PROPERTIES TIMEOUT 120)

    # Graph navigation on the real binary (27l), with zero backends: path,
    # explain, neighbors and query over a code graph built with no model; an
    # external MCP client over stdio listing the four graph tools from
    # `__mcp-tools`, read-only, each answering with the CLI's own JSON byte
    # for byte; a tool-using turn on a pipe calling graph_path ungated; and
    # the graph's rows unchanged by all of it. Needs python3 (skips without).
    add_test(NAME cli.graph_navigation_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/graph_navigation_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/graph-navigation-e2e
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/mcp_graph_client.py"
    )
    set_tests_properties(cli.graph_navigation_lifecycle PROPERTIES TIMEOUT 120)

    # Graph artifacts on the real binary (27m), with zero backends: the
    # report printed and the same bytes at --out, its one JSON document; the
    # HTML page under its default name in the working directory with its data
    # block, its cap note and no external reference -- its own script driven
    # through layout, click-to-explain, search and the legend where node is
    # installed; GraphML read back by Python's stdlib parser and by networkx
    # where installed; a Mermaid call flow every line of which the grammar
    # reads; and a canary in the source's comments and the private layout rows
    # in no artifact, the data directory unchanged. python3 and node optional.
    add_test(NAME cli.graph_artifacts_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/graph_artifacts_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/graph-artifacts-e2e
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/js/graph_html_check.js"
    )
    set_tests_properties(cli.graph_artifacts_lifecycle PROPERTIES TIMEOUT 120)

    # The datasets floor on the real binary: the seeded kits, create, synth
    # through the scripted mock, the Python boundary (the refusal on a pipe,
    # `train setup` against the host's python3, `prepare` through the seeded
    # script under the environment's interpreter), list, info, delete. Skips
    # (77) where the host has no python3.
    add_test(NAME cli.datasets_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/datasets_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/datasets-e2e
    )
    set_tests_properties(cli.datasets_lifecycle PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

    # The run on the real binary with the mock trainer -- no Python at all:
    # a template dataset, `train run` writing a manifest with the status
    # line seen, a GGUF and a backend name refused, `train eval --suite`
    # with substring items passing against the mock's echo, `promote --as`
    # registering the entry byte-exactly and writing v1.gguf, a second
    # promote to v2 and `rollback` to v1 deleting nothing, retention,
    # `versions`, `status`, `check` green throughout, and no training route
    # on the served admin plane beyond the reads.
    add_test(NAME cli.train_lifecycle
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sh/train_e2e.sh"
            $<TARGET_FILE:apogee>
            ${CMAKE_CURRENT_BINARY_DIR}/train-e2e
    )
    set_tests_properties(cli.train_lifecycle PROPERTIES TIMEOUT 180)
endif()

# Startup output discipline, under a real pseudo-terminal.
#
# A pipe-based test proves nothing here: `apogee` asks whether stdout is a
# terminal before rendering anything, so piping exercises the branch that
# deliberately emits nothing. The PTY is the only way to see what the user sees.
# POSIX only -- `pty` has no Windows equivalent (a recorded per-item skip).
if(UNIX)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cli.startup_speaks_on_one_line
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_startup_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.startup_speaks_on_one_line PROPERTIES TIMEOUT 60)

        # Typeahead and crash-safety. The first needs a real terminal (tcflush
        # applies to a terminal input queue, and on a pipe the buffered bytes
        # ARE the input); the second needs a real SIGKILL. Neither reproduces
        # in a unit test.
        # A noisy, dying MCP server under a PTY: its stderr never paints the
        # terminal, the failure carries its tail, and startup does not wait
        # out the connect timeout.
        add_test(NAME cli.mcp_server_stderr_stays_off_the_terminal
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_mcp_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.mcp_server_stderr_stays_off_the_terminal PROPERTIES TIMEOUT 120)

        # ffmpeg under a PTY (26e): audio and video are decoded by a child
        # whose banner, mapping and progress never reach the terminal.
        add_test(NAME cli.ffmpeg_stays_off_the_terminal
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_ffmpeg_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.ffmpeg_stays_off_the_terminal PROPERTIES TIMEOUT 120)

        # The busy line (M1): a slow sweep -- held by a named pipe, never a
        # seam in the product -- speaks on one repainted stderr line that is
        # gone before the results, and stdout stays the answer on a pipe.
        add_test(NAME cli.busy_line
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_busy_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.busy_line PROPERTIES TIMEOUT 120)

        add_test(NAME cli.chat_typeahead_and_crash_safety
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_chat_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.chat_typeahead_and_crash_safety PROPERTIES TIMEOUT 180)

        # Line editing. replxx engages only when stdin AND stdout are both
        # terminals, so none of this reproduces on a pipe -- which is the point:
        # before it was wired, an arrow key arrived in the message as a literal
        # escape sequence. Since item 24 it also holds the suggestion rows to
        # the screen: drawn, taken by Tab, never at the last column, never left
        # behind -- and a pipe given none of it.
        add_test(NAME cli.chat_line_editing
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/pty_lineedit_check.py"
                $<TARGET_FILE:apogee>
        )
        set_tests_properties(cli.chat_line_editing PROPERTIES TIMEOUT 120)

        # The reference driver: machine mode's worked example AND its
        # test-lock. It reconstructs the conversation from the JSONL and
        # checks it against what the terminal renderer printed, so the two
        # surfaces cannot drift apart unnoticed.
        add_test(NAME cli.reference_driver
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/reference_driver.py"
                $<TARGET_FILE:apogee>
                ${CMAKE_CURRENT_BINARY_DIR}/reference-driver-home
        )
        set_tests_properties(cli.reference_driver PROPERTIES TIMEOUT 120)

        # The protocol document, pinned to the code that implements it. A GUI
        # author trusts that document and debugs Apogee for faults that are
        # actually in the prose, so it is not allowed to drift.
        add_test(NAME cli.machine_schema_conformance
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/py/schema_conformance.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/../source/presentation/machine/json_reporter.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../../documentation/reference/machine-mode.md"
        )
        set_tests_properties(cli.machine_schema_conformance PROPERTIES TIMEOUT 60)
    endif()
endif()

# `apogee config` end to end against the real binary, under a throwaway
# APOGEE_HOME. Driven by cmake -P so it runs on every target, Windows included.
add_test(NAME cli.config_lifecycle
    COMMAND "${CMAKE_COMMAND}"
        -DAPOGEE_BIN=$<TARGET_FILE:apogee>
        -DAPOGEE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/config-e2e-home
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/config_e2e.cmake"
)
