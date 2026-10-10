# The module map (Architecture A4): every module's layer, and the modules it
# links. One declaration, two enforcers, both reading this file:
#
#   the build      -- source/CMakeLists.txt makes each module a static library,
#                     apogee_<layer>_<module>, linked to exactly the modules
#                     named here, and apogee_assert_link_policy()
#                     (cmake/ApogeeLinkPolicy.cmake) fails the configure step
#                     on a link that points up a layer or closes a cycle.
#                     A module sees its own layer's include root and the roots
#                     its links bring, so an include that reaches up a layer
#                     does not compile.
#   the test       -- tests/scripts/cmake/layering.cmake holds the includes to this map in
#                     both directions: an include of another module with no
#                     link here fails, and so does a link nothing includes.
#
# The layers are ADR 0001's, lowest first, and the build adds them in this
# order (lib/documentation/adrs/cli/layer-enforcement.md). A module may link
# modules in its own layer and the layers below it, never above. A new module
# gets a row here the day its directory exists -- source/<layer>/<module>/ --
# and its links are the review conversation, held in this file.
#
# Data only: `set()` and nothing else above the index at the bottom, so the
# layering test reads it under `cmake -P` exactly as the build does.

set(APOGEE_LAYERS infrastructure data business presentation)

set(APOGEE_LAYER_infrastructure platform events ansi version)
set(APOGEE_LAYER_data contracts transport modelstore logger secrets embedstore backends)
set(APOGEE_LAYER_business harness agent agentloop knowledge graph tools mcp models training
                          tasks symphony scaffold)
set(APOGEE_LAYER_presentation markdown render views machine operations httpserver tui cli)

# ---- Infrastructure ---------------------------------------------------------
set(APOGEE_LINKS_platform "")
set(APOGEE_LINKS_events "")
set(APOGEE_LINKS_ansi platform)
set(APOGEE_LINKS_version platform)

# ---- Data -------------------------------------------------------------------
set(APOGEE_LINKS_contracts platform)
set(APOGEE_LINKS_transport contracts)
set(APOGEE_LINKS_modelstore contracts platform)
set(APOGEE_LINKS_logger contracts)
set(APOGEE_LINKS_secrets contracts)
set(APOGEE_LINKS_embedstore platform)
set(APOGEE_LINKS_backends contracts events logger modelstore platform secrets transport)

# ---- Business ---------------------------------------------------------------
set(APOGEE_LINKS_harness contracts)
set(APOGEE_LINKS_agent contracts)
set(APOGEE_LINKS_agentloop agent contracts embedstore harness logger platform)
set(APOGEE_LINKS_knowledge agentloop contracts embedstore harness)
set(APOGEE_LINKS_graph agentloop contracts embedstore harness knowledge)
set(APOGEE_LINKS_tools agent agentloop contracts embedstore graph harness platform)
set(APOGEE_LINKS_mcp agent contracts events platform transport)
set(APOGEE_LINKS_models contracts modelstore platform transport)
set(APOGEE_LINKS_training contracts platform transport)
set(APOGEE_LINKS_tasks agent agentloop contracts logger platform)
set(APOGEE_LINKS_symphony agent agentloop contracts harness)
set(APOGEE_LINKS_scaffold contracts symphony)

# ---- Presentation -----------------------------------------------------------
set(APOGEE_LINKS_markdown ansi)
set(APOGEE_LINKS_render "")
set(APOGEE_LINKS_views agentloop ansi contracts markdown platform)
set(APOGEE_LINKS_machine agent agentloop contracts tasks)
set(APOGEE_LINKS_operations agentloop backends contracts embedstore graph harness knowledge
                            logger platform symphony training)
set(APOGEE_LINKS_httpserver agent agentloop contracts embedstore events graph harness knowledge
                            logger operations scaffold secrets symphony tasks tools training)
set(APOGEE_LINKS_tui agentloop ansi contracts markdown operations platform views)
set(APOGEE_LINKS_cli agent agentloop ansi backends contracts embedstore events graph harness
                     httpserver knowledge logger machine mcp models modelstore operations
                     platform render scaffold secrets symphony tasks tools training transport
                     tui version views)

# ---- The index both enforcers read ------------------------------------------
# APOGEE_MODULES (every module, lowest layer first), APOGEE_MODULE_LAYER_<m>
# (its layer's name) and APOGEE_MODULE_RANK_<m> (its layer's position, 0 at
# Infrastructure). Derived, never written by hand.
set(APOGEE_MODULES "")
set(_apogee_rank 0)
foreach(_apogee_layer IN LISTS APOGEE_LAYERS)
    foreach(_apogee_module IN LISTS APOGEE_LAYER_${_apogee_layer})
        list(APPEND APOGEE_MODULES ${_apogee_module})
        set(APOGEE_MODULE_LAYER_${_apogee_module} ${_apogee_layer})
        set(APOGEE_MODULE_RANK_${_apogee_module} ${_apogee_rank})
    endforeach()
    set(APOGEE_LAYER_RANK_${_apogee_layer} ${_apogee_rank})
    math(EXPR _apogee_rank "${_apogee_rank} + 1")
endforeach()
unset(_apogee_rank)
unset(_apogee_layer)
unset(_apogee_module)
