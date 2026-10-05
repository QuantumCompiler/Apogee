# The Presentation layer (ADR 0001): how Apogee is shown and driven -- the
# command line, the views, machine mode, the HTTP server.
#
# One static library per module -- apogee_presentation_<module> -- linked to
# exactly the modules cmake/modules.cmake declares for it (apogee_add_module,
# source/CMakeLists.txt). This file says what each module compiles and which
# third-party code it reaches; which modules it reaches lives in the map.

apogee_add_module(markdown
    markdown/types.h
    markdown/inline.h
    markdown/inline.cpp
    markdown/layout.h
    markdown/layout.cpp
    markdown/stream_renderer.h
    markdown/stream_renderer.cpp
)

apogee_add_module(render
    render/json_report.h
    render/json_report.cpp
)
target_link_libraries(apogee_presentation_render PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(views
    views/ask_prompt.h
    views/ask_prompt.cpp
    views/terminal.h
    views/terminal.cpp
    views/status_line.h
    views/status_line.cpp
    views/download_progress.h
    views/download_progress.cpp
    views/thinking_view.h
    views/thinking_view.cpp
    views/answer_view.h
    views/answer_view.cpp
    views/cli_reporter.h
    views/cli_reporter.cpp
    views/input_gate.h
    views/input_gate.cpp
    views/line_reader.h
    views/line_reader.cpp
)
target_link_libraries(apogee_presentation_views PRIVATE replxx::replxx)

apogee_add_module(machine
    machine/json_reporter.h
    machine/json_reporter.cpp
)
target_link_libraries(apogee_presentation_machine PRIVATE nlohmann_json::nlohmann_json)

apogee_add_module(operations
    operations/backend_names.h
    operations/backend_names.cpp
    operations/collections.h
    operations/collections.cpp
    operations/dataset_core.h
    operations/dataset_core.cpp
    operations/graph_members.h
    operations/graph_members.cpp
    operations/knowledge_core.h
    operations/knowledge_core.cpp
    operations/retrieval.h
    operations/retrieval.cpp
    operations/run_settings.h
    operations/run_settings.cpp
    operations/suites.h
    operations/suites.cpp
)
target_link_libraries(apogee_presentation_operations PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(httpserver
    httpserver/httpserver.h
    httpserver/http_types.h
    httpserver/http_types.cpp
    httpserver/sse_writer.h
    httpserver/sse_writer.cpp
    httpserver/sse_reporter.h
    httpserver/sse_reporter.cpp
    httpserver/session.h
    httpserver/session.cpp
    httpserver/handler.h
    httpserver/handler.cpp
    httpserver/mux.h
    httpserver/mux.cpp
    httpserver/shutdown.h
    httpserver/shutdown.cpp
    httpserver/admin_auth.h
    httpserver/admin_auth.cpp
    httpserver/jobs.h
    httpserver/jobs.cpp
    httpserver/admin_events.h
    httpserver/admin_events.cpp
    httpserver/admin_config.h
    httpserver/admin_config.cpp
    httpserver/admin_agents.h
    httpserver/admin_agents.cpp
    httpserver/admin_knowledge.h
    httpserver/admin_knowledge.cpp
    httpserver/admin_graph.h
    httpserver/admin_graph.cpp
    httpserver/admin_graphs.h
    httpserver/admin_graphs.cpp
    httpserver/admin_suites.h
    httpserver/admin_suites.cpp
    httpserver/admin_datasets.h
    httpserver/admin_datasets.cpp
    httpserver/admin_training.h
    httpserver/admin_training.cpp
    httpserver/admin.h
    httpserver/admin.cpp
    httpserver/admin_auth_routes.h
    httpserver/admin_auth_routes.cpp

    # The ONE translation unit that may reference listen()/accept(). The
    # no-listen symbol check allow-lists exactly this object and requires it to
    # be the only one -- see tests/scripts/cmake/no_listen_symbols.cmake.
    httpserver/serve.h
    httpserver/serve.cpp
)
target_link_libraries(apogee_presentation_httpserver PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE httplib::httplib)

apogee_add_module(cli
    cli/command.h
    cli/model_chain.h
    cli/model_chain.cpp
    cli/models_pull.h
    cli/models_pull.cpp
    cli/models_migrate.h
    cli/models_migrate.cpp
    cli/embed.h
    cli/embed.cpp
    cli/models.h
    cli/models.cpp
    cli/registry.h
    cli/registry.cpp
    cli/root.h
    cli/root.cpp
    cli/version_command.h
    cli/version_command.cpp
    cli/config_cmd.h
    cli/config_cmd.cpp
    cli/config_suites.h
    cli/config_suites.cpp
    cli/check.h
    cli/complete_protocol.h
    cli/complete_protocol.cpp
    cli/complete_sources.cpp
    cli/uninstall.h
    cli/uninstall.cpp
    cli/reset.h
    cli/reset.cpp
    cli/check.cpp
    cli/complete.h
    cli/complete.cpp
    cli/helpers.h
    cli/helpers.cpp
    cli/tool_vectors.h
    cli/tool_vectors.cpp
    cli/interrupt.h
    cli/interrupt.cpp
    cli/serve_cmd.h
    cli/serve_cmd.cpp
    cli/mcp_cmd.h
    cli/mcp_cmd.cpp
    cli/permissions.h
    cli/permissions.cpp
    cli/auth_cmd.h
    cli/auth_cmd.cpp
    cli/analyze.h
    cli/analyze.cpp
    cli/agents_cmd.h
    cli/agents_cmd.cpp
    cli/graph.h
    cli/graph.cpp
    cli/datasets.h
    cli/datasets.cpp
    cli/train.h
    cli/train.cpp
    cli/knowledge.h
    cli/knowledge.cpp
    cli/chat.h
    cli/chat.cpp
    cli/chat_completer.h
    cli/chat_completer.cpp
    cli/chat_attachments.h
    cli/chat_attachments.cpp
    cli/chat_history.h
    cli/chat_history.cpp
    cli/chat_recall.h
    cli/chat_recall.cpp
    cli/complete_sources.h
)
target_link_libraries(apogee_presentation_cli PUBLIC CLI11::CLI11 nlohmann_json::nlohmann_json)

apogee_add_layer()
