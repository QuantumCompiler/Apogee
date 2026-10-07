# The Infrastructure layer (ADR 0001): what everything stands on -- the
# platform seam, the event bus, terminal attributes, the version stamp.
#
# One static library per module -- apogee_infrastructure_<module> -- linked to
# exactly the modules cmake/modules.cmake declares for it (apogee_add_module,
# source/CMakeLists.txt). This file says what each module compiles and which
# third-party code it reaches; which modules it reaches lives in the map.

apogee_add_module(platform
    platform/child_process.h
    platform/child_process.cpp
    platform/ffmpeg.h
    platform/ffmpeg.cpp
    platform/pid_lock.h
    platform/pid_lock.cpp
    platform/platform.h
    platform/platform.cpp
)

# A leaf: includes nothing else from the project, so anything may publish.
apogee_add_module(events
    events/bus.h
    events/bus.cpp
)
target_link_libraries(apogee_infrastructure_events PUBLIC nlohmann_json::nlohmann_json)

apogee_add_module(ansi
    ansi/ansi.h
    ansi/ansi.cpp
    ansi/text_width.h
    ansi/text_width.cpp
)

apogee_add_module(version
    version/version.h
    version/version.cpp
)
target_compile_definitions(apogee_infrastructure_version PRIVATE
    APOGEE_VERSION="${PROJECT_VERSION}"
    APOGEE_GIT_COMMIT="${APOGEE_GIT_COMMIT}"
    APOGEE_BUILD_DATE="${APOGEE_BUILD_DATE}"
)

apogee_add_layer()
