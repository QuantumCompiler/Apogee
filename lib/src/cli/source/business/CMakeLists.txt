# The Business layer (ADR 0001): what Apogee does -- the harness, the loop,
# tools, knowledge, the graph, models, training.
#
# One static library per module -- apogee_business_<module> -- linked to
# exactly the modules cmake/modules.cmake declares for it (apogee_add_module,
# source/CMakeLists.txt). This file says what each module compiles and which
# third-party code it reaches; which modules it reaches lives in the map.

apogee_add_module(harness
    harness/roles.h
    harness/roles.cpp
    harness/context_windows.h
    harness/context_windows.cpp
    harness/harness.h
    harness/harness.cpp
)

apogee_add_module(agent
    agent/tool.h
    agent/tool.cpp
    agent/fetch_url.h
    agent/fetch_url.cpp
    agent/readable.h
    agent/readable.cpp
    agent/web_search.h
    agent/web_search.cpp
)
target_link_libraries(apogee_business_agent PRIVATE nlohmann_json::nlohmann_json)

apogee_add_module(agentloop
    agentloop/agentloop.h
    agentloop/reporter.h
    agentloop/embed_func.h
    agentloop/embed_func.cpp
    agentloop/retriever.h
    agentloop/retriever.cpp
    agentloop/rerank.h
    agentloop/rerank.cpp
    agentloop/query_rewrite.h
    agentloop/query_rewrite.cpp
    agentloop/tool_selection.h
    agentloop/tool_selection.cpp
    agentloop/tool_summary.h
    agentloop/tool_summary.cpp
    agentloop/rag.h
    agentloop/rag.cpp
    agentloop/graph_context.h
    agentloop/graph_context.cpp
    agentloop/loop.h
    agentloop/loop.cpp
    agentloop/content.h
    agentloop/content.cpp
    agentloop/budget.h
    agentloop/budget.cpp
    agentloop/attachments.h
    agentloop/attachments.cpp
    agentloop/media.h
    agentloop/media.cpp
    agentloop/question.h
    agentloop/question.cpp
    agentloop/structured.h
    agentloop/structured.cpp
    agentloop/review_context.h
    agentloop/review_context.cpp
)
target_link_libraries(apogee_business_agentloop PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE nlohmann_json_schema_validator::validator)

# The knowledge layer: records, the capture clerk, and the store over the
# chunk store. Never a surface: its own rule is in tests/scripts/cmake/layering.cmake.
apogee_add_module(knowledge
    knowledge/record.h
    knowledge/record.cpp
    knowledge/clerk.h
    knowledge/clerk.cpp
    knowledge/store.h
    knowledge/store.cpp
    knowledge/query.h
    knowledge/query.cpp
    knowledge/refine.h
    knowledge/refine.cpp
    knowledge/export.h
    knowledge/export.cpp
)
target_link_libraries(apogee_business_knowledge PUBLIC nlohmann_json::nlohmann_json)

# The knowledge-graph layer over collections: the extraction contract
# (prompt and schema compiled in, validation in host code) and the
# resumable build with the record pass. Model-free: generation and
# embedding arrive as closures (tests/scripts/cmake/layering.cmake holds it to that).
apogee_add_module(graph
    graph/extract.h
    graph/extract.cpp
    graph/build.h
    graph/build.cpp
    graph/communities.h
    graph/communities.cpp
)
target_link_libraries(apogee_business_graph PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(tools
    tools/tools.h
    tools/args.h
    tools/args.cpp
    tools/process.h
    tools/process.cpp
    tools/fs.h
    tools/fs.cpp
    tools/shell.h
    tools/shell.cpp
    tools/git.h
    tools/git.cpp
    tools/notes.h
    tools/notes.cpp
    tools/rag_query.h
    tools/rag_query.cpp
    tools/environment.h
    tools/environment.cpp
    tools/toolsets.h
    tools/toolsets.cpp
)
target_link_libraries(apogee_business_tools PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(mcp
    mcp/types.h
    mcp/types.cpp
    mcp/transport.h
    mcp/transport.cpp
    mcp/client.h
    mcp/client.cpp
    mcp/registry.h
    mcp/registry.cpp
    mcp/serve_stdio.h
    mcp/serve_stdio.cpp
    mcp/mcp.h
)
target_link_libraries(apogee_business_mcp PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(models
    models/lineage.h
    models/lineage.cpp
    models/acquire.h
    models/acquire.cpp
    models/source_ollama.h
    models/source_ollama.cpp
    models/source_hf.h
    models/source_hf.cpp
    models/quantize.h
    models/quantize.cpp
    models/convert.h
    models/convert.cpp
    models/migrate.h
    models/migrate.cpp
)
target_link_libraries(apogee_business_models PRIVATE nlohmann_json::nlohmann_json)

# The quantizer is llama.cpp's own (models/quantize.cpp), so this module links
# it under the same switch; without it, a quantization answers that the build
# has no llama.cpp in it.
if(APOGEE_ENABLE_LLAMA)
    target_link_libraries(apogee_business_models PRIVATE llama)
    target_compile_definitions(apogee_business_models PRIVATE APOGEE_ENABLE_LLAMA=1)
endif()

apogee_add_module(training
    training/python_env.h
    training/python_env.cpp
    training/script_runner.h
    training/script_runner.cpp
    training/kit.h
    training/kit.cpp
    training/synth.h
    training/synth.cpp
    training/datasets.h
    training/datasets.cpp
    training/trainer.h
    training/trainer.cpp
    training/script_trainer.h
    training/script_trainer.cpp
    training/mlx_trainer.h
    training/mlx_trainer.cpp
    training/peft_trainer.h
    training/peft_trainer.cpp
    training/mock_trainer.h
    training/mock_trainer.cpp
    training/eval.h
    training/eval.cpp
    training/manifest.h
    training/manifest.cpp
    training/store.h
    training/store.cpp
    training/promote.h
    training/promote.cpp
    training/convert.h
    training/convert.cpp
    training/pipeline.h
    training/pipeline.cpp
    training/regime.h
    training/regime.cpp
    training/cycle.h
    training/cycle.cpp
)
target_link_libraries(apogee_business_training PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE yaml-cpp::yaml-cpp)

apogee_add_module(scaffold
    scaffold/mcp_server.h
    scaffold/mcp_server.cpp
    scaffold/agent.h
    scaffold/agent.cpp
)

apogee_add_layer()
