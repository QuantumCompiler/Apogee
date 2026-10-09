#include "cli/symphonies_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "agentloop/member_call.h"
#include "agentloop/side_call.h"
#include "ansi/ansi.h"
#include "ansi/text_width.h"
#include "backends/factory.h"
#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "harness/harness.h"
#include "machine/json_reporter.h"
#include "operations/suites.h"
#include "platform/child_process.h"
#include "scaffold/symphony.h"
#include "symphony/definition.h"
#include "symphony/runner.h"
#include "symphony/view.h"
#include "views/status_line.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee symphonies: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[noreturn]] void fail_backend(const std::string& message) {
    std::cerr << "apogee symphonies: " << message << "\n";
    throw CLI::RuntimeError(kBackendError);
}

std::filesystem::path config_path_for(const RootContext& context) {
    return harness::resolve_config_path(context.config_path);
}

std::filesystem::path symphonies_dir_for(const std::filesystem::path& config_path) {
    return symphony::directory_for(config_path);
}

harness::Config load(const std::filesystem::path& path) {
    try {
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

/// A definition found, and the catalog its play stages resolve against
/// (27r).
struct Resolved {
    symphony::Definition definition;
    symphony::Catalog catalog;
};

/// The definition `name_or_path` names -- or the reason there is none, said.
Resolved find_or_fail(const harness::Config& config, const std::filesystem::path& config_path,
                      std::string_view name_or_path) {
    symphony::Found found =
        symphony::find_definition(config, symphonies_dir_for(config_path), name_or_path);
    if (!found.definition.has_value()) {
        fail_user(found.error);
    }
    return Resolved{.definition = std::move(*found.definition),
                    .catalog = std::move(found.catalog)};
}

/// `text` padded with spaces to `width` display cells, at least one after.
std::string padded(std::string_view text, std::size_t width) {
    const std::size_t cells = ansi::display_width(text);
    return std::string{text} + std::string(cells < width ? width - cells : 1, ' ');
}

std::string source_label(const symphony::Definition& definition) {
    std::string label{symphony::to_string(definition.source)};
    if (definition.overrides) {
        label += harness::find_bundled_symphony(definition.spec.name) != nullptr
                     ? " (overrides shipped)"
                     : " (overrides file)";
    }
    return label;
}

/// A prompt or a schema, indented under its stage.
void print_block(std::ostream& out, std::string_view text, std::string_view indent) {
    std::string_view rest = text;
    while (!rest.empty()) {
        const std::size_t newline = rest.find('\n');
        const std::string_view line = rest.substr(0, newline);
        out << (line.empty() ? std::string{} : std::string{indent} + std::string{line}) << "\n";
        if (newline == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(newline + 1);
    }
}

/// A stage that plays a symphony (27r): what it plays, and what it gives it.
void print_play_stage(const harness::SymphonyStage& stage, std::size_t index) {
    std::cout << "  ·  plays " << stage.play;
    if (stage.image) {
        std::cout << "  ·  passes the image on";
    }
    if (stage.input.empty()) {
        std::cout << "  ·  given "
                  << (index == 0 ? "the symphony's input" : "the previous stage's answer") << "\n";
        return;
    }
    std::cout << "  ·  given:\n";
    print_block(std::cout, stage.input, "    ");
}

void print_definition(const symphony::Definition& definition, const symphony::Catalog& catalog) {
    const harness::SymphonySpec& spec = definition.spec;
    std::cout << spec.name << "  ·  " << source_label(definition);
    if (!definition.path.empty()) {
        std::cout << "  ·  " << definition.path.string();
    }
    std::cout << "\n";
    if (!spec.description.empty()) {
        std::cout << spec.description << "\n";
    }
    std::cout << "\ninput: "
              << (spec.input.description.empty() ? std::string{"(not described)"}
                                                 : spec.input.description)
              << "\n";
    if (spec.input.image) {
        std::cout << "       and an image, given with --image\n";
    }
    const std::size_t count = spec.stages.size();
    for (std::size_t index = 0; index < count; ++index) {
        const harness::SymphonyStage& stage = spec.stages[index];
        std::cout << "\nstage " << index + 1 << "/" << count << "  " << stage.name;
        if (stage.plays()) {
            print_play_stage(stage, index);
            continue;
        }
        std::cout << "  ·  role " << stage.role;
        if (stage.image) {
            std::cout << "  ·  given the image";
        }
        std::cout << "  ·  brief up to " << stage.brief_tokens.value_or(symphony::kStageBriefTokens)
                  << " tokens, answer up to "
                  << stage.answer_tokens.value_or(symphony::kStageAnswerTokens) << " tokens\n";
        print_block(std::cout, stage.prompt, "    ");
        if (!stage.schema.empty()) {
            std::cout << "  answer held to its schema:\n";
            print_block(std::cout, stage.schema, "    ");
        }
    }
    const std::vector<std::string> problems = symphony::validate(spec, catalog);
    if (!problems.empty()) {
        std::cout << "\ncannot be played:\n";
        for (const std::string& problem : problems) {
            std::cout << "  - " << problem << "\n";
        }
        return;
    }
    // A chain says what one play of it costs, every symphony it reaches
    // counted (27r).
    if (const harness::SymphonyWalk walked = symphony::walk(spec, catalog); walked.depth > 1) {
        std::cout << "\none play: " << walked.stage_calls << " member calls, one after another, "
                  << walked.depth << " symphonies deep (the cap is " << catalog.max_depth << ")\n";
    }
}

// ---- list ---------------------------------------------------------------------

/// `list`'s columns, in display cells.
constexpr std::size_t kNameColumn = 22;
constexpr std::size_t kStagesColumn = 30;
constexpr std::size_t kSourceColumn = 28;

void bind_list(CLI::App& parent, const RootContext& context) {
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* cmd = parent.add_subcommand(
        "list", "List the symphonies -- the shipped starters, the config's, your spec files");
    add_read_format(cmd, format);
    cmd->callback([&context, format]() {
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        const symphony::Catalog catalog =
            symphony::catalog(config, symphonies_dir_for(config_path));
        if (*format == ReadFormat::Json) {
            write_document(std::cout, symphony::list_document(catalog));
            return;
        }
        for (const std::string& line : symphony_list_lines(catalog)) {
            std::cout << line << "\n";
        }
        for (const std::string& problem : catalog.problems) {
            std::cerr << "apogee symphonies: skipped " << problem << "\n";
        }
    });
}

// ---- show ---------------------------------------------------------------------

void bind_show(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* cmd = parent.add_subcommand(
        "show", "Show a symphony: its input, its stages -- role, prompt, schema, caps");
    cmd->add_option("name", *name, "A symphony's name, or a spec file's path")
        ->type_name(kSymphonyValue)
        ->required();
    add_read_format(cmd, format);
    cmd->callback([&context, name, format]() {
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        const Resolved found = find_or_fail(config, config_path, *name);
        if (*format == ReadFormat::Json) {
            write_document(std::cout,
                           symphony::definition_document(found.definition, found.catalog));
            return;
        }
        print_definition(found.definition, found.catalog);
    });
}

// ---- create -------------------------------------------------------------------

/// `--stage NAME:ROLE:PROMPT`: the prompt is everything after the second
/// colon, colons and all.
harness::SymphonyStage parse_stage_flag(const std::string& value) {
    const std::size_t first = value.find(':');
    const std::size_t second = first == std::string::npos ? first : value.find(':', first + 1);
    if (second == std::string::npos) {
        fail_user("--stage '" + value +
                  "': expected NAME:ROLE:PROMPT -- e.g. "
                  "--stage 'summarize:utility:Summarize this: {{input}}'");
    }
    harness::SymphonyStage stage;
    stage.name = value.substr(0, first);
    stage.role = value.substr(first + 1, second - first - 1);
    stage.prompt = value.substr(second + 1);
    return stage;
}

/// `--play NAME:SYMPHONY[:INPUT]` (27r): the input template is everything
/// after the second colon, colons and all; without one the stage is given
/// the previous stage's answer.
harness::SymphonyStage parse_play_flag(const std::string& value) {
    const std::size_t first = value.find(':');
    if (first == std::string::npos) {
        fail_user("--play '" + value +
                  "': expected NAME:SYMPHONY[:INPUT] -- e.g. --play summary:summarize-verify");
    }
    const std::size_t second = value.find(':', first + 1);
    harness::SymphonyStage stage;
    stage.name = value.substr(0, first);
    stage.play = value.substr(first + 1, second == std::string::npos ? second : second - first - 1);
    if (second != std::string::npos) {
        stage.input = value.substr(second + 1);
    }
    return stage;
}

/// The stages `--stage` and `--play` give, in the order they were written:
/// each flag's values are its own, so the command's parse order interleaves
/// them.
std::vector<harness::SymphonyStage> stages_in_order(const CLI::App& command,
                                                    const CLI::Option* stage_option,
                                                    const std::vector<std::string>& stages,
                                                    const CLI::Option* play_option,
                                                    const std::vector<std::string>& plays) {
    std::vector<harness::SymphonyStage> out;
    std::size_t next_stage = 0;
    std::size_t next_play = 0;
    for (const CLI::Option* option : command.parse_order()) {
        if (option == stage_option && next_stage < stages.size()) {
            out.push_back(parse_stage_flag(stages[next_stage++]));
        } else if (option == play_option && next_play < plays.size()) {
            out.push_back(parse_play_flag(plays[next_play++]));
        }
    }
    return out;
}

void bind_create(CLI::App& parent, const RootContext& context) {
    struct Flags {
        std::string name;
        std::string description;
        std::string input_description;
        bool input_image = false;
        std::vector<std::string> stages;
        std::vector<std::string> plays;
        std::string from;
        bool force = false;
        CLI::Option* description_option = nullptr;
        CLI::Option* input_option = nullptr;
        CLI::Option* stage_option = nullptr;
        CLI::Option* play_option = nullptr;
    };

    auto flags = std::make_shared<Flags>();
    CLI::App* cmd = parent.add_subcommand(
        "create",
        "Write a symphony into the config: from --stage flags, from another (--from), or a "
        "one-stage skeleton to edit");
    cmd->add_option("name", flags->name, "The symphony's name (letters, digits, _ and -)")
        ->required();
    flags->description_option =
        cmd->add_option("--description", flags->description, "What the symphony does");
    flags->input_option = cmd->add_option("--input-description", flags->input_description,
                                          "What the symphony is given, for whoever plays it");
    cmd->add_flag("--input-image", flags->input_image,
                  "The play takes an image beside its text (play --image)");
    std::string roles;
    for (const std::string_view role : harness::symphony_role_names()) {
        roles += (roles.empty() ? "" : ", ") + std::string{role};
    }
    flags->stage_option =
        cmd->add_option("--stage", flags->stages,
                        "A stage, in play order (repeatable): NAME:ROLE:PROMPT, the role one of " +
                            roles +
                            " -- never a backend; the prompt reads {{input}} and earlier "
                            "stages' answers by name")
            ->expected(1)
            ->allow_extra_args(false)
            ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    flags->play_option =
        cmd->add_option("--play", flags->plays,
                        "A stage that plays another symphony, in play order with --stage "
                        "(repeatable): NAME:SYMPHONY[:INPUT], the input what it is given as its "
                        "{{input}} (default: the previous stage's answer)")
            ->expected(1)
            ->allow_extra_args(false)
            ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--from", flags->from,
                    "Start from another symphony -- a starter, a config entry or a spec file")
        ->type_name(kSymphonyValue);
    cmd->add_flag("--force", flags->force, "Replace an existing entry of the name");
    cmd->callback([&context, cmd, flags]() {
        const bool staged = !flags->stages.empty() || !flags->plays.empty();
        if (!flags->from.empty() && staged) {
            fail_user("--from and --stage/--play both say what the stages are -- give one");
        }
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        harness::SymphonySpec spec;
        if (!flags->from.empty()) {
            spec = find_or_fail(config, config_path, flags->from).definition.spec;
        } else if (staged) {
            spec.stages = stages_in_order(*cmd, flags->stage_option, flags->stages,
                                          flags->play_option, flags->plays);
        } else {
            spec = scaffold::starter_symphony(flags->name, {});
        }
        spec.name = flags->name;
        if (flags->description_option->count() > 0) {
            spec.description = flags->description;
        }
        if (flags->input_option->count() > 0) {
            spec.input.description = flags->input_description;
        }
        if (flags->input_image) {
            spec.input.image = true;
        }
        scaffold::SymphonyResult result;
        try {
            result = scaffold::create_symphony(config_path, spec, flags->force);
        } catch (const std::exception& e) {
            fail_user(e.what());
        }
        std::cout << (result.replaced ? "replaced" : "created") << " symphony '" << result.name
                  << "' (" << symphony::role_chain(spec) << ")\n"
                  << "  config: " << result.config_path.string() << "\n\n"
                  << "Play it:  apogee symphonies play " << result.name << " --input \"…\"\n"
                  << "Edit it:  apogee symphonies edit " << result.name << "\n";
    });
}

// ---- edit ---------------------------------------------------------------------

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Writes `before` to `scratch`, opens it in `$EDITOR`, and returns what the
/// file holds when the editor exits. `$EDITOR` may carry flags ("code
/// --wait"): split on spaces, as `agents edit` does. The editor is the user's
/// own foreground program -- the documented exception to the captured-stderr
/// rule (platform/child_process.h), at their request, with no turn running.
std::string edit_in_editor(const std::filesystem::path& scratch, const std::string& before) {
    std::error_code code;
    std::filesystem::create_directories(scratch.parent_path(), code);
    try {
        harness::write_file_atomically(scratch, before);
    } catch (const std::exception& e) {
        fail_user(std::string{"could not write the file to edit: "} + e.what());
    }
    const char* editor_env = std::getenv("EDITOR");
    std::istringstream words{editor_env == nullptr || *editor_env == '\0' ? "vi" : editor_env};
    platform::ChildCommand command;
    words >> command.program;
    for (std::string word; words >> word;) {
        command.arguments.push_back(word);
    }
    command.arguments.push_back(scratch.string());
    std::string error;
    const std::optional<int> status = platform::run_foreground(command, error);
    if (!status.has_value()) {
        fail_user("could not open the editor: " + error);
    }
    if (*status != 0) {
        fail_user("the editor exited with status " + std::to_string(*status) +
                  " -- nothing was written (the edit is at " + scratch.string() + ")");
    }
    std::optional<std::string> after = read_text(scratch);
    if (!after.has_value()) {
        fail_user("could not read the edited file back: " + scratch.string());
    }
    return std::move(*after);
}

void bind_edit(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand(
        "edit",
        "Edit a symphony in $EDITOR -- written back as its config entry; a starter or a spec "
        "file edited this way gains an entry of its name that stands in for it");
    cmd->add_option("name", *name, "The symphony's name")
        ->type_name(kSymphonyNameValue)
        ->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        if (name->find_first_of("/\\") != std::string::npos || name->ends_with(".yaml") ||
            name->ends_with(".yml")) {
            fail_user(*name +
                      " is a spec file's path -- edit the file in place; 'edit' takes a "
                      "symphony's name and writes its config entry");
        }
        const symphony::Definition definition = find_or_fail(config, config_path, *name).definition;
        const std::string before = harness::render_symphony_spec(definition.spec);
        const std::filesystem::path scratch = harness::home_for_config(config_path) / "cache" /
                                              ("symphony-" + definition.spec.name + ".yaml");
        const std::string after = edit_in_editor(scratch, before);
        std::error_code code;
        if (after == before) {
            std::filesystem::remove(scratch, code);
            std::cout << "no change to symphony '" << definition.spec.name << "'\n";
            return;
        }
        harness::SymphonySpec edited;
        try {
            edited = harness::parse_symphony_spec(after, scratch.string(), definition.spec.name,
                                                  config.backend_names());
        } catch (const harness::ConfigError& e) {
            fail_user(std::string{e.what()} + " -- nothing was written; the edit is kept at " +
                      scratch.string());
        }
        if (edited.name != definition.spec.name) {
            fail_user("'edit' does not rename -- make '" + edited.name + "' with 'apogee " +
                      "symphonies create " + edited.name + " --from " + scratch.string() +
                      "' (the edit is kept there), then delete '" + definition.spec.name + "'");
        }
        try {
            (void)scaffold::create_symphony(config_path, edited, true);
        } catch (const std::exception& e) {
            fail_user(std::string{e.what()} + " -- nothing was written; the edit is kept at " +
                      scratch.string());
        }
        std::filesystem::remove(scratch, code);
        if (definition.source == symphony::Source::Config) {
            std::cout << "updated symphony '" << edited.name << "' in " << config_path.string()
                      << "\n";
        } else {
            std::cout << "symphony '" << edited.name << "' is now a config entry in "
                      << config_path.string() << ", standing in for the "
                      << (definition.source == symphony::Source::Shipped ? "shipped starter"
                                                                         : "spec file")
                      << " of its name ('apogee symphonies delete " << edited.name
                      << "' brings that back)\n";
        }
    });
}

// ---- delete -------------------------------------------------------------------

void bind_delete(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand("delete", "Remove a symphony's config entry");
    cmd->add_option("name", *name, "The symphony's name")
        ->type_name(kSymphonyEntryValue)
        ->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        if (config.find_symphony(*name) == nullptr) {
            const symphony::Found found =
                symphony::find_definition(config, symphonies_dir_for(config_path), *name);
            if (found.definition.has_value() &&
                found.definition->source == symphony::Source::Shipped) {
                fail_user("'" + *name +
                          "' is a shipped starter, with no config entry to delete -- it stays");
            }
            if (found.definition.has_value() && !found.definition->path.empty()) {
                fail_user("'" + *name + "' is a spec file, with no config entry to delete -- " +
                          "remove " + found.definition->path.string() + " yourself");
            }
            fail_user(found.error.empty() ? "no symphony named '" + *name + "'" : found.error);
        }
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::delete_symphony(content, *name);
            });
        } catch (const std::exception& e) {
            fail_user(e.what());
        }
        std::cout << "removed symphony '" << *name << "' from " << config_path.string() << "\n";
        const symphony::Found remains =
            symphony::find_definition(load(config_path), symphonies_dir_for(config_path), *name);
        if (remains.definition.has_value()) {
            std::cout << "the " << symphony::to_string(remains.definition->source)
                      << " symphony of that name plays again\n";
        }
    });
}

// ---- play ---------------------------------------------------------------------

struct PlayFlags {
    std::string name;
    std::string input;
    std::string image;
    std::string suite;
    bool quiet = false;
    CLI::Option* input_option = nullptr;
};

/// The input text: `--input`, else stdin when something is piped there.
std::string play_text(const PlayFlags& flags) {
    if (flags.input_option->count() > 0) {
        return flags.input;
    }
    if (!stdin_is_piped()) {
        return {};
    }
    std::string text = read_stdin();
    // `echo … |` ends its text with a newline the writer never meant.
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

/// The play's input: its text, and its image read into the part a member
/// is sent.
symphony::PlayInput play_input(const PlayFlags& flags) {
    symphony::PlayInput input;
    input.text = play_text(flags);
    if (flags.image.empty()) {
        return input;
    }
    const std::filesystem::path path{flags.image};
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) {
        fail_user(flags.image + ": cannot open file");
    }
    try {
        input.image.push_back(load_image_part(path));
    } catch (const std::exception& e) {
        fail_user(e.what());
    }
    return input;
}

/// Every backend built, as `complete` builds them, and the active suite's
/// members held to naming ones that are there.
void build_members(harness::Harness& harness, const harness::Config& config,
                   const std::filesystem::path& config_path) {
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    const backends::BuildResult built = backends::build_providers(harness, build_options);
    if (built.constructed_count() == 0) {
        std::string message = "no usable backend is configured";
        message += built.skipped_summary().empty() ? " (add one with 'apogee config add-backend')"
                                                   : " -- " + built.skipped_summary();
        fail_user(message);
    }
    // A suite member naming nothing is refused, never routed around (27d).
    if (const std::string refused = validate_active_suite(config); !refused.empty()) {
        fail_user(refused);
    }
}

void play(const RootContext& context, const PlayFlags& flags, ReadFormat format) {
    const std::filesystem::path config_path = config_path_for(context);
    harness::Config config = load(config_path);
    const Resolved found = find_or_fail(config, config_path, flags.name);
    const symphony::Definition& definition = found.definition;
    if (!flags.suite.empty()) {
        if (const std::string refused = select_suite(config, flags.suite); !refused.empty()) {
            fail_user(refused);
        }
    }

    const symphony::PlayInput input = play_input(flags);
    // What the definition refuses is said before anything is built -- the
    // whole walk checked, every symphony it plays found (27r).
    if (const std::string refused = symphony::refusal(definition.spec, found.catalog, input);
        !refused.empty()) {
        fail_user(refused +
                  (input.text.empty() ? " -- pass --input \"…\", or pipe it on stdin" : ""));
    }

    harness::Harness harness{config};
    build_members(harness, config, config_path);

    // Each stage is said: on the busy line while it runs, kept above it once
    // it is done -- on a pipe, the kept lines alone; with JSON or --quiet,
    // nothing (the document carries every stage).
    const bool json = format == ReadFormat::Json;
    BusyLine busy{std::cerr, "playing " + definition.spec.name, busy_options(flags.quiet || json)};
    const ansi::Style style{ansi::resolve_color(ansi::ColorMode::Auto, busy.active(),
                                                std::getenv("NO_COLOR") != nullptr)};
    const bool say = !flags.quiet && !json;
    agentloop::SideCallSink narrate = [&busy, &style, say](const agentloop::SideCall& call) {
        const std::string label = call.role + " — " + call.detail;
        if (!call.done) {
            busy.set(label);
            return;
        }
        if (!say) {
            return;
        }
        const std::string line = label + agentloop::side_call_suffix(call);
        busy.above([&] { std::cerr << style.dim(line) << "\n"; });
    };

    const InterruptScope interrupt;
    agentloop::MemberCalls calls{harness};
    symphony::PlayResult result;
    try {
        const agentloop::MemberCalls::Turn turn =
            calls.begin_turn(std::move(narrate), InterruptScope::token());
        // One budget for the whole walk, the config's caps when it sets them.
        symphony::PlayOptions options;
        options.per_turn = config.symphony_caps.stage_calls.value_or(0);
        options.answer_tokens = config.symphony_caps.answer_tokens.value_or(0);
        result = symphony::play(definition.spec, found.catalog, input, calls, options);
    } catch (const harness::CancelledError&) {
        busy.finish();
        std::cerr << "apogee symphonies: cancelled\n";
        throw CLI::RuntimeError(kCancelled);
    }
    busy.finish();
    if (!result.ok()) {
        if (result.member_failure) {
            fail_backend(result.failure);
        }
        fail_user(result.failure);
    }
    if (json) {
        const harness::SuiteConfig* suite = harness::active_suite(config);
        write_document(std::cout,
                       symphony::play_document(
                           definition.spec.name,
                           suite == nullptr ? std::string{} : config.models.default_suite, result));
        return;
    }
    std::cout << result.output;
    if (result.output.empty() || result.output.back() != '\n') {
        std::cout << "\n";
    }
}

void bind_play(CLI::App& parent, const RootContext& context) {
    auto flags = std::make_shared<PlayFlags>();
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* cmd = parent.add_subcommand(
        "play",
        "Play a symphony once: its stages in order, each one call on a role of the active "
        "suite, the last stage's answer printed");
    cmd->add_option("name", flags->name, "A symphony's name, or a spec file's path")
        ->type_name(kSymphonyValue)
        ->required();
    flags->input_option =
        cmd->add_option("--input", flags->input, "The input text (default: stdin, when piped)");
    cmd->add_option("--image", flags->image,
                    "An image for a symphony whose input takes one (png, jpg, gif, webp, bmp)")
        ->type_name(kPathValue);
    cmd->add_option("--suite", flags->suite,
                    "Play under this suite -- its members answer for the roles it names -- or off "
                    "for none (default: models.default_suite)")
        ->type_name(kModelSuiteOrOffValue);
    cmd->add_flag("-q,--quiet", flags->quiet, "Say nothing about the stages as they run");
    add_read_format(cmd, format);
    cmd->callback([&context, flags, format]() { play(context, *flags, *format); });
}

}  // namespace

std::vector<std::string> symphony_list_lines(const symphony::Catalog& catalog) {
    std::vector<std::string> lines{padded("NAME", kNameColumn) + padded("STAGES", kStagesColumn) +
                                   padded("SOURCE", kSourceColumn) + "DESCRIPTION"};
    for (const symphony::Definition& definition : catalog.definitions) {
        const std::vector<std::string> problems = symphony::validate(definition.spec, catalog);
        lines.push_back(padded(definition.spec.name, kNameColumn) +
                        padded(symphony::role_chain(definition.spec), kStagesColumn) +
                        padded(source_label(definition), kSourceColumn) +
                        (problems.empty() ? definition.spec.description
                                          : "cannot be played -- 'apogee symphonies show " +
                                                definition.spec.name + "' says why"));
    }
    return lines;
}

std::string_view SymphoniesCommand::name() const noexcept {
    return "symphonies";
}

std::string_view SymphoniesCommand::summary() const noexcept {
    return "Define and play symphonies: staged prompt processes over a suite's members";
}

void SymphoniesCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);
    bind_list(*cmd, context);
    bind_show(*cmd, context);
    bind_create(*cmd, context);
    bind_edit(*cmd, context);
    bind_delete(*cmd, context);
    bind_play(*cmd, context);
}

}  // namespace apogee::commands
