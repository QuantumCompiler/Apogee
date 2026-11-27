# A printed command goes through the renderer (CLI-M49; CLI-M61 extends it).
#
# The sibling of one_key_resolver.cmake, for the commands Apogee prints for a
# person to copy: a remedy, a door, a `run:` or `Run:` hint, `check`'s rows.
# `contracts/suggested_command.h` renders each one, and its first words are
# the build that printed it and the root flag that chose the root
# (`printed_program()`): `apogee-dev` on the dev channel, `apogee --dev` for
# a release binary run with `--dev`. A command written out whole in a string
# literal -- `"run 'apogee config init'"` -- names the release binary from
# every build and drops the flag, so pasted from a dev build it acts on
# `~/.apogee`, which the dev channel's law keeps read-only.
#
# The rule: a source line whose string literal holds `apogee ` and a
# lower-case word is a printed command written out whole, and fails -- as does
# one that spells a channel's name (`apogee-dev config init`) -- unless
#   - it is the house voice, `apogee <verb>: ` (`apogee mcp serve: `), which
#     keeps the product's word;
#   - the command runs on past the end of its literal into a value joined
#     with `+` or `<<` (`"apogee models repair " + name`, `"... '" + host`):
#     that is CLI-M61's rule, for commands a non-literal completes;
#   - or the line is on the list below: help text, a description, prose
#     naming a verb in passing, or a file's own contents written into the
#     install or an export -- each entry its file, a piece of the line, and
#     its reason.
# A command is followed through every literal it runs on into before it is
# judged -- an adjacent one on the next line, one added with `+`, and the one
# after a literal split right after the program word (`'apogee "` then
# `"config init'"`); comment lines are exempt. Each file is read whole, its
# brackets, semicolons and backslashes swapped for placeholders before it is
# split into lines (test_names.cmake's lesson: file(STRINGS) is a CMake list).
#
# It refuses to pass vacuously: it fails when it scans no source, when it
# never sees the house voice (so its matcher found nothing at all), and when
# a list entry names a file that is gone or matches no line the rule flags.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "one-suggested-command check found no sources under ${SOURCE_DIR}")
endif()

# Control characters, so no source text can already hold one.
string(ASCII 1 open_bracket)
string(ASCII 2 close_bracket)
string(ASCII 3 semicolon)
string(ASCII 4 backslash)
string(ASCII 5 escaped_backslash)
string(ASCII 6 escaped_quote)

function(as_read out text)
    string(REPLACE "[" "${open_bracket}" text "${text}")
    string(REPLACE "]" "${close_bracket}" text "${text}")
    string(REPLACE ";" "${semicolon}" text "${text}")
    string(REPLACE "\\" "${backslash}" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(as_written out text)
    string(REPLACE "${open_bracket}" "[" text "${text}")
    string(REPLACE "${close_bracket}" "]" text "${text}")
    string(REPLACE "${semicolon}" ";" text "${text}")
    string(REPLACE "${backslash}" "\\" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# The list: a file, a piece of the line, and why the line names `apogee`
# without being a command this binary prints. Three strings an entry.
set(listed
    # --- Files written into the install, the same bytes for every channel
    #     ("a channel changes the root and the binary's name, never the
    #     contents"): seeded drivers, recipes, symphonies, regimes and
    #     scaffolds, read by whichever binary runs them.
    "data/contracts/assets_mlx.cpp" "apogee" "a seeded MLX driver's own text"
    "data/contracts/assets_video.cpp" "apogee" "a seeded video or speech driver's own text"
    "data/contracts/assets_training.cpp" "apogee" "a seeded training driver's or regime's own text"
    "data/contracts/assets_recipes.cpp" "apogee task run" "a seeded recipe's usage comment"
    "data/contracts/assets_recipes.cpp" "apogee knowledge capture" "a seeded recipe's comment"
    "data/contracts/assets_recipes.cpp" "Read the transcript" "a seeded recipe's goal text"
    "data/contracts/assets_symphonies.cpp" "#   apogee symphonies " "a seeded symphony's usage comment"
    "data/contracts/assets_mcp.cpp" "apogee" "the seeded code-execution server's README"
    "data/contracts/assets_agents.cpp" "apogee analyze --agent doc-drift" "a seeded agent's prompt"
    "business/scaffold/mcp_server.cpp" "apogee mcp" "the README and docstring a scaffolded server is written with"
    "business/scaffold/mcp_server.cpp" "apogee config delete-mcp-server __NAME__" "the README a scaffolded server is written with"
    "data/contracts/config_migrate.cpp" ".opens = \"// " "commentary written into a migrated config file"
    "data/contracts/config_migrate.cpp" ".closes = \"// " "commentary written into a migrated config file"
    "data/contracts/layout.cpp" "by 'apogee " "the layout's purpose for a directory, the same for every channel"
    "data/contracts/layout.cpp" "('apogee " "the layout's purpose for a directory, the same for every channel"
    "presentation/operations/knowledge_core.cpp" "captured decision records (apogee knowledge)" "a collection's description, written into the config"
    # --- An export's own text, read on any machine.
    "business/graph/html_template.cpp" "apogee graph" "the HTML export's page text names the verb that made it"
    "business/graph/export_html.cpp" "apogee graph export graphml carries every one" "the HTML export's page text"
    # --- Help text and descriptions.
    "presentation/cli/agents_cmd.cpp" "Create and manage agent workflows (apogee analyze --agent)" "the command's help text"
    "presentation/cli/chat_session.cpp" "Retrieve context from this collection each turn (see 'apogee embed')" "an option's help text"
    "presentation/cli/complete.cpp" "Retrieve context from this collection (see 'apogee embed')" "an option's help text"
    "presentation/cli/knowledge.cpp" "Distil a saved chat session, by id or name (see 'apogee chats list')" "an option's help text"
    "presentation/cli/config_cmd.cpp" "The model calls 'apogee mcp serve' may make" "an option's help text"
    "presentation/cli/config_cmd.cpp" "refused: keys are stored with 'apogee auth add'" "an option's help text"
    "presentation/cli/config_cmd.cpp" "secrets go through 'apogee auth add'" "the command's help text"
    "presentation/cli/execute.cpp" "(apogee acp's own)" "a hidden option's help text"
    "presentation/cli/mcp_cmd.cpp" "(see: apogee mcp presets)" "an option's help text"
    "presentation/cli/mcp_cmd.cpp" "(apogee config set-backend <name> --mcp-serve complete)" "the command's help footer"
    "presentation/cli/mcp_cmd.cpp" "The entry a host takes: apogee mcp presets." "the command's help footer"
    "presentation/cli/check.cpp" "key -- as 'apogee providers scan' does" "an option's help text"
    "presentation/cli/serve_cmd.cpp" "Retrieve context from this collection on every request (see 'apogee " "an option's help text"
    "presentation/cli/acp_cmd.cpp" "each session's apogee runs in machine mode" "prose: `apogee` is the session's process, in a refusal's reason"
    "presentation/cli/acp_cmd.cpp" "apogee execute session in the host's project folder" "the command's help footer"
    "presentation/cli/acp_cmd.cpp" "the argument acp -- apogee acp registration prints the" "the command's help footer"
    "presentation/cli/quantize_child_cmd.cpp" "for the apogee that started this one" "the hidden command's help text"
    "presentation/cli/uninstall.cpp" "leave ~/.apogee alone" "an option's help text"
    "presentation/cli/tui_parity.cpp" "(apogee acp registration) and show in the macOS app" "the shell's parity table: why a verb has no view"
    "presentation/cli/mcp_serve.cpp" "document `apogee " "a served tool's description: the document it answers with"
    "presentation/cli/mcp_serve.cpp" "every row `apogee check` reports" "a served tool's description"
    "presentation/cli/mcp_serve.cpp" "Nothing is repaired: `apogee check --fix` is the" "a served tool's description"
    "presentation/cli/mcp_serve.cpp" "the one-shot `apogee complete -m <backend>" "a served tool's description"
    "presentation/cli/mcp_serve.cpp" "as apogee complete takes its prompt" "a served tool's parameter description"
    "presentation/operations/mcp_presets.cpp" "served on stdio by apogee mcp serve for" "a preset's description"
    "business/tools/rag_query.cpp" "as `apogee embed list` shows it" "a tool's parameter description for the model"
    "data/modelstore/mlx_info.cpp" ".description = \"a video model: 'apogee video generate' runs it" "a model kind's description in models info"
    "data/modelstore/mlx_info.cpp" ".description = \"a voice model: 'apogee voice say' speaks with it" "a model kind's description in models info"
    # --- A completion's descriptions, which the stubs show under the `apogee`
    #     they complete (a dev install writes no stubs, M10).
    "presentation/cli/complete_sources.cpp" "'apogee " "a completion's description of an empty list"
    "presentation/cli/complete_protocol.cpp" "no backends configured yet ('apogee config add-backend')" "a completion's description"
    # --- Prose naming the product or a verb in passing, or a record of
    #     what happened -- not a command to run.
    "presentation/operations/acp_registration.cpp" "PATH is the one this apogee runs with" "prose: `apogee` is the running process"
    "presentation/httpserver/admin_events.cpp" ": apogee event stream open" "an SSE comment line"
    "presentation/cli/tui_task.cpp" "It runs as 'apogee task resume' does" "the confirm compares the action to a verb"
    "business/tasks/runner.cpp" "by 'apogee task" "a task's recorded reason names the verb that ended it"
    "presentation/cli/task_cmd.cpp" "by 'apogee task" "a task's recorded reason names the verb that ended it"
    "presentation/cli/train.cpp" "halted by 'apogee train cycle halt'" "the cycle's recorded reason names the verb that halted it"
    "business/training/cycle.cpp" "'apogee train cycle halt' was run" "the cycle's reason, when none was recorded, names the verb that halted it"
)

list(LENGTH listed listed_length)
math(EXPR listed_remainder "${listed_length} % 3")
if(NOT listed_remainder EQUAL 0)
    message(FATAL_ERROR "the list holds ${listed_length} strings -- each entry is three: a file, "
                        "a piece of the line, and the reason")
endif()
math(EXPR listed_last "${listed_length} - 1")

# Every entry names a file that exists: one left behind by a move would
# exempt nothing -- or a new file that took the old name.
set(entry_count 0)
foreach(at RANGE 0 ${listed_last} 3)
    list(GET listed ${at} entry_file)
    if(NOT EXISTS "${SOURCE_DIR}/${entry_file}")
        message(FATAL_ERROR "the list names ${entry_file}, which is not under ${SOURCE_DIR} -- "
                            "a file that moved must move here too")
    endif()
    set(entry_matched_${entry_count} FALSE)
    math(EXPR entry_count "${entry_count} + 1")
endforeach()

# Whether `line` (placeholders in) is listed for `relative`; marks the entry.
macro(check_listed relative line result)
    set(${result} FALSE)
    set(_entry 0)
    foreach(_at RANGE 0 ${listed_last} 3)
        list(GET listed ${_at} _file)
        if(_file STREQUAL "${relative}")
            math(EXPR _piece_at "${_at} + 1")
            list(GET listed ${_piece_at} _piece)
            as_read(_piece "${_piece}")
            string(FIND "${line}" "${_piece}" _found)
            if(NOT _found EQUAL -1)
                set(${result} TRUE)
                set(entry_matched_${_entry} TRUE)
            endif()
        endif()
        math(EXPR _entry "${_entry} + 1")
    endforeach()
endmacro()

# `text` with its escapes read as one character each, stripped of nothing.
function(escaped_line out text)
    string(REPLACE "${backslash}${backslash}" "${escaped_backslash}" text "${text}")
    string(REPLACE "${backslash}\"" "${escaped_quote}" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Whether the command that starts `text` (inside a literal, on line `index`,
# counted from 1, of `lines`) is joined: followed through each literal it
# runs on into -- an adjacent one, or one added with `+` -- it reaches a
# literal's end still open (a space, `=`, `/` or `:` last, or a quote opening
# a value; no quote or bracket having closed it) and a value that is not a
# literal follows through `+` or `<<`. That is CLI-M61's rule; anything else is a command
# written out whole.
function(command_is_joined out text index)
    set(${out} FALSE PARENT_SCOPE)
    set(read "")
    set(at ${index})
    foreach(step RANGE 1 12)
        string(FIND "${text}" "\"" quote)
        if(quote EQUAL -1)
            # A literal that runs past the line: a raw string's body.
            return()
        endif()
        string(SUBSTRING "${text}" 0 ${quote} inside)
        string(APPEND read "${inside}")
        # A quote opening a value joined by hand (`delete-allowed-host '" +
        # host`) closes nothing.
        set(judged "${read}")
        if(judged MATCHES "[ =]['${escaped_quote}]$")
            string(LENGTH "${judged}" judged_length)
            math(EXPR judged_length "${judged_length} - 1")
            string(SUBSTRING "${judged}" 0 ${judged_length} judged)
        endif()
        if(judged MATCHES "['`)]")
            return()
        endif()
        math(EXPR after_at "${quote} + 1")
        string(SUBSTRING "${text}" ${after_at} -1 after)
        string(STRIP "${after}" after)
        if(after STREQUAL "" AND at LESS line_count)
            list(GET lines ${at} after)
            escaped_line(after "${after}")
            string(STRIP "${after}" after)
            math(EXPR at "${at} + 1")
        endif()
        if(after MATCHES "^\"")
            # An adjacent literal: the command runs on into it.
            string(SUBSTRING "${after}" 1 -1 text)
            continue()
        endif()
        if(NOT judged MATCHES "[ =/:]$")
            return()
        endif()
        # Matched last, so CMAKE_MATCH_2 is this match's.
        if(NOT after MATCHES "^(\\+|<<)(.*)$")
            return()
        endif()
        string(STRIP "${CMAKE_MATCH_2}" operand)
        if(operand STREQUAL "" AND at LESS line_count)
            list(GET lines ${at} operand)
            escaped_line(operand "${operand}")
            string(STRIP "${operand}" operand)
            math(EXPR at "${at} + 1")
        endif()
        if(NOT operand MATCHES "^\"")
            set(${out} TRUE PARENT_SCOPE)
            return()
        endif()
        string(SUBSTRING "${operand}" 1 -1 text)
    endforeach()
endfunction()

set(offenders "")
set(scanned 0)
set(voices 0)
foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    math(EXPR scanned "${scanned} + 1")
    file(READ "${source}" text)
    string(FIND "${text}" "apogee " any)
    if(any EQUAL -1)
        continue()
    endif()
    string(REPLACE "\r" "" text "${text}")
    as_read(text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    list(LENGTH lines line_count)

    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        # A command whose literal is split after the program word --
        # `'apogee "` then `"config migrate'` on the next line -- is one too.
        if(NOT line MATCHES "apogee(-dev|-test)? [a-z]" AND NOT line MATCHES "apogee \"")
            continue()
        endif()
        string(STRIP "${line}" stripped)
        # Documentation must be able to name what the rule forbids.
        if(stripped MATCHES "^(//|/\\*|\\*)")
            continue()
        endif()
        # Escapes read as one character, so a `\"` never ends a literal.
        escaped_line(work "${line}")

        set(flagged FALSE)
        # A channel's name spelled out (`apogee-dev config init`) is a
        # command too, joined or not: no site spells one (channel_program).
        if(work MATCHES "apogee-(dev|test) [a-z]")
            set(flagged TRUE)
        endif()
        set(rest "${work}")
        while(TRUE)
            string(FIND "${rest}" "apogee " at)
            if(at EQUAL -1)
                break()
            endif()
            string(SUBSTRING "${rest}" ${at} -1 tail)
            math(EXPR skip "${at} + 7")
            string(SUBSTRING "${rest}" ${skip} -1 rest)
            if(tail MATCHES "^apogee \"")
                # Split after the program word: a command when the literal
                # it runs on into starts with a lower-case word.
                string(SUBSTRING "${tail}" 8 -1 split_after)
                string(STRIP "${split_after}" split_after)
                if(split_after STREQUAL "" AND line_number LESS line_count)
                    list(GET lines ${line_number} split_after)
                    string(STRIP "${split_after}" split_after)
                endif()
                if(NOT split_after MATCHES "^\"[a-z]")
                    continue()
                endif()
            elseif(NOT tail MATCHES "^apogee [a-z]")
                continue()
            endif()
            # The house voice: `apogee <verb>: `.
            if(tail MATCHES "^apogee [a-z][-a-z0-9]*( [a-z][-a-z0-9]*)*:([ \"${escaped_quote}]|$)")
                math(EXPR voices "${voices} + 1")
                continue()
            endif()
            command_is_joined(joined "${tail}" ${line_number})
            if(NOT joined)
                set(flagged TRUE)
                break()
            endif()
        endwhile()

        if(flagged)
            check_listed("${relative}" "${line}" is_listed)
            if(NOT is_listed)
                # Kept read (its semicolons placeholders) until it is shown,
                # so a line's `;` never splits the list.
                list(APPEND offenders "${relative}:${line_number}: ${stripped}")
            endif()
        endif()
    endforeach()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "one-suggested-command check scanned no files")
endif()
if(voices EQUAL 0)
    message(FATAL_ERROR "one-suggested-command check never saw the house voice (`apogee <verb>: `) "
                        "-- its matcher found nothing, so it would pass on anything")
endif()

set(stale "")
set(entry 0)
foreach(at RANGE 0 ${listed_last} 3)
    if(NOT entry_matched_${entry})
        list(GET listed ${at} entry_file)
        math(EXPR piece_at "${at} + 1")
        list(GET listed ${piece_at} entry_piece)
        list(APPEND stale "${entry_file}: ${entry_piece}")
    endif()
    math(EXPR entry "${entry} + 1")
endforeach()
if(stale)
    string(REPLACE ";" "\n  " pretty "${stale}")
    message(FATAL_ERROR
        "The list exempts lines the rule no longer flags:\n  ${pretty}\n"
        "Each entry must name a line that exists and holds a command written out whole -- "
        "remove the entry, or move it with its line.")
endif()

if(offenders)
    list(LENGTH offenders offender_count)
    string(REPLACE ";" "\n  " pretty "${offenders}")
    as_written(pretty "${pretty}")
    message(FATAL_ERROR
        "A printed command is written out whole, on ${offender_count} line(s):\n  ${pretty}\n"
        "A command written in a string literal names the release binary from every build and "
        "drops the root flag, so pasted from `apogee-dev` or under `--dev` it acts on another "
        "install. Build it through contracts/suggested_command.h -- render() for a command with "
        "words to quote, or harness::printed_program() + \" <verb> ...\" for one whose every word "
        "is literal. Help text, a description or prose naming a verb in passing goes on the list "
        "in this file WITH a reason.")
endif()

message(STATUS "one-suggested-command check: ${scanned} files, ${voices} house-voice lines, "
               "every printed command through the renderer - OK")
