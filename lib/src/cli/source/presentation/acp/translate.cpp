#include "acp/translate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace apogee::acp {

namespace {

using enum Disposition;

/// Every machine event type, as `machine/protocol.cpp` declares them, with
/// what it becomes. The unit suite holds this to the declaration both ways.
constexpr std::array kEventRules{
    EventRule{"session", SessionStart,
              "the child's first event: the session is open; its model is the menu's"},
    EventRule{"thinking", Dropped, "a marker: the reasoning itself arrives as thinking_delta"},
    EventRule{"thinking_delta", Update, "agent_thought_chunk"},
    EventRule{"memory", Update, "a thought line naming what was recalled"},
    EventRule{"sources", Update, "a thought line naming the excerpts sent"},
    EventRule{"tool_status", Update,
              "a side call's line as a thought; a tool's own [tool] line is dropped, its "
              "tool_call draws it"},
    EventRule{"tool_call", Update, "tool_call, then tool_call_update as its state moves"},
    EventRule{"notice", Notice,
              "notice to a host that takes them, else the turn's thought stream -- the next "
              "turn's when none runs"},
    EventRule{"todo", Update, "plan"},
    EventRule{"answer_start", Dropped, "a marker: the answer arrives as answer_delta"},
    EventRule{"answer_delta", Update, "agent_message_chunk"},
    EventRule{"answer_end", Dropped, "a marker: the turn ends with result"},
    EventRule{"result", PromptEnd, "the prompt's answer, its stop reason from finish_reason"},
    EventRule{"question", Question,
              "the permission sheet (session/request_permission), or ask_user as a form "
              "(elicitation/create) where the host takes one"},
    EventRule{"error", PromptError, "the prompt's error, in Apogee's words"},
    EventRule{"task_started", Dropped, "a task's: never written by an execute session"},
    EventRule{"task_plan", Dropped, "a task's: never written by an execute session"},
    EventRule{"task_round", Dropped, "a task's: never written by an execute session"},
    EventRule{"task_grant", Dropped, "a task's: never written by an execute session"},
    EventRule{"task_finished", Dropped, "a task's: never written by an execute session"},
    EventRule{"job", Dropped, "a long job's: never written by an execute session"},
    EventRule{"stage", Dropped, "a long job's: never written by an execute session"},
    EventRule{"bytes", Dropped, "a long job's: never written by an execute session"},
    EventRule{"ready", Dropped, "a long job's: never written by an execute session"},
    EventRule{"step", Dropped, "a long job's: never written by an execute session"},
    EventRule{"frames", Dropped, "a long job's: never written by an execute session"},
    EventRule{"assembling", Dropped, "a long job's: never written by an execute session"},
    EventRule{"segment", Dropped, "a long job's: never written by an execute session"},
    EventRule{"scene", Dropped, "a long job's: never written by an execute session"},
    EventRule{"iteration", Dropped, "a training run's: never written by an execute session"},
    EventRule{"pass", Dropped, "a cycle pass's: never written by an execute session"},
    EventRule{"document", Dropped, "a long job's: never written by an execute session"},
};

/// The native tools' kinds (ACP's ToolKind), and whether each one's target
/// is a path. The unit suite holds the rows to the native registry: a new
/// native tool is classified here, or fails.
constexpr std::array kKinds{
    KindRow{"read_file", "read", true},
    KindRow{"write_file", "edit", true},
    KindRow{"edit_file", "edit", true},
    KindRow{"delete_file", "delete", true},
    KindRow{"list_directory", "read", true},
    KindRow{"search_files", "search"},
    KindRow{"grep_files", "search"},
    KindRow{"run_command", "execute"},
    KindRow{"git_status", "read"},
    KindRow{"git_log", "read"},
    KindRow{"git_diff", "read"},
    KindRow{"git_show", "read"},
    KindRow{"read_note", "read"},
    KindRow{"list_notes", "read"},
    KindRow{"write_note", "edit"},
    KindRow{"delete_note", "delete"},
    KindRow{"search_documents", "search"},
    KindRow{"list_collections", "read"},
    KindRow{"collection_info", "read"},
    KindRow{"graph_query", "search"},
    KindRow{"graph_path", "search"},
    KindRow{"graph_explain", "read"},
    KindRow{"graph_neighbors", "read"},
    KindRow{"todo_write", "think"},
    KindRow{"fetch_url", "fetch"},
    KindRow{"web_search", "fetch"},
    KindRow{"consult", "think"},
    KindRow{"delegate", "other"},
};

[[nodiscard]] nlohmann::json text_block(std::string_view text) {
    return nlohmann::json{{"type", "text"}, {"text", std::string{text}}};
}

[[nodiscard]] nlohmann::json chunk(std::string_view kind, std::string_view text) {
    return nlohmann::json{{"sessionUpdate", std::string{kind}}, {"content", text_block(text)}};
}

/// A JSON line, never a throw for its bytes.
[[nodiscard]] std::string line_of(const nlohmann::json& object) {
    return object.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

[[nodiscard]] std::string string_of(const nlohmann::json& object, std::string_view key) {
    const auto found = object.find(std::string{key});
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// `@"path"`: the mention syntax a path with spaces takes. A path holding a
/// quote cannot be one, and is said as text.
[[nodiscard]] std::string mention(const std::string& path) {
    if (path.find('"') != std::string::npos) {
        return path;
    }
    return "@\"" + path + "\"";
}

[[nodiscard]] int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::span<const EventRule> event_rules() noexcept {
    return kEventRules;
}

const EventRule* event_rule(std::string_view type) noexcept {
    const auto found = std::ranges::find(kEventRules, type, &EventRule::type);
    return found == kEventRules.end() ? nullptr : &*found;
}

std::span<const KindRow> tool_kinds() noexcept {
    return kKinds;
}

std::string_view tool_kind(std::string_view tool) noexcept {
    const auto found = std::ranges::find(kKinds, tool, &KindRow::tool);
    return found == kKinds.end() ? std::string_view{"other"} : found->kind;
}

bool tool_takes_path(std::string_view tool) noexcept {
    const auto found = std::ranges::find(kKinds, tool, &KindRow::tool);
    return found != kKinds.end() && found->path;
}

std::string_view stop_reason(std::string_view finish_reason) noexcept {
    if (finish_reason == "length") {
        return "max_tokens";
    }
    if (finish_reason == "content_filter") {
        return "refusal";
    }
    if (finish_reason == "cancelled") {
        return "cancelled";
    }
    return "end_turn";
}

std::string_view plan_status(std::string_view state) noexcept {
    if (state == "active") {
        return "in_progress";
    }
    if (state == "done") {
        return "completed";
    }
    return "pending";
}

std::string_view tool_call_status(std::string_view state) noexcept {
    if (state == "running") {
        return "in_progress";
    }
    if (state == "done") {
        return "completed";
    }
    if (state == "failed") {
        return "failed";
    }
    return "pending";
}

nlohmann::json thought_update(std::string_view text) {
    return chunk("agent_thought_chunk", text);
}

nlohmann::json notice_update(std::string_view text) {
    // A notice's title is plain text for a compact place: its first line,
    // the rest as its description.
    const std::size_t cut = text.find('\n');
    nlohmann::json update{{"sessionUpdate", "notice"},
                          {"severity", "info"},
                          {"title", std::string{text.substr(0, cut)}}};
    if (cut != std::string_view::npos && cut + 1 < text.size()) {
        update["description"] = std::string{text.substr(cut + 1)};
    }
    return update;
}

Translator::Translator(std::filesystem::path cwd, std::filesystem::path home)
    : cwd_{std::move(cwd)}, home_{std::move(home)} {}

std::filesystem::path Translator::location(std::string_view target) const {
    if (target.empty()) {
        return {};
    }
    std::filesystem::path path;
    // `~` and `~/...`, as the file tools expand them; `~user` is not one.
    if (target == "~" || target.starts_with("~/")) {
        if (home_.empty()) {
            return {};
        }
        path = target == "~" ? home_ : home_ / std::string{target.substr(2)};
    } else {
        path = std::filesystem::path{std::string{target}};
        if (path.is_relative()) {
            path = cwd_ / path;
        }
    }
    return path.lexically_normal();
}

std::string Translator::fresh_id(const std::string& base) const {
    if (!cards_.contains(base)) {
        return base;
    }
    for (std::size_t n = 2;; ++n) {
        std::string candidate = base + "-" + std::to_string(n);
        if (!cards_.contains(candidate)) {
            return candidate;
        }
    }
}

std::string Translator::card_for(const nlohmann::json& event, bool& opened) {
    opened = false;
    const std::string id = string_of(event, "id");
    // Every call is reported pending first (dispatch, before its gate): a
    // pending report is a new call, whatever its id.
    const bool pending = string_of(event, "state") == "pending";
    if (id.empty()) {
        // A provider that gave its call no id: the call that is pending now
        // is the one each later report is about, until the next opens.
        if (pending || anonymous_.empty()) {
            anonymous_ = fresh_id("apogee-call-" + std::to_string(++minted_));
            opened = true;
        }
        return anonymous_;
    }
    if (const auto latest = latest_.find(id); latest != latest_.end() && !pending) {
        return latest->second;
    }
    // Its first report -- or a later call under an id already drawn, which
    // gets a card of its own rather than taking the earlier call's.
    std::string card = fresh_id(id);
    latest_.insert_or_assign(id, card);
    opened = true;
    return card;
}

nlohmann::json Translator::drawn(std::string_view id, const Card& call) const {
    nlohmann::json out{{"toolCallId", std::string{id}},
                       {"title", call.target.empty() ? call.name : call.name + " " + call.target},
                       {"kind", std::string{tool_kind(call.name)}}};
    // A location only where the target is a path: a note's key, a command or
    // a host is none.
    if (tool_takes_path(call.name)) {
        if (const std::filesystem::path path = location(call.target); !path.empty()) {
            out["locations"] = nlohmann::json::array({{{"path", path.generic_string()}}});
        }
    }
    return out;
}

std::vector<nlohmann::json> Translator::updates(const nlohmann::json& event) {
    const std::string type = string_of(event, "type");
    std::vector<nlohmann::json> out;
    if (type == "thinking_delta") {
        out.push_back(thought_update(string_of(event, "text")));
    } else if (type == "answer_delta") {
        out.push_back(chunk("agent_message_chunk", string_of(event, "text")));
    } else if (type == "tool_status") {
        const std::string text = string_of(event, "text");
        // A tool's own line is its tool_call's to draw; a side call -- the
        // utility model summarising, a consult -- is said as a thought.
        if (!text.empty() && !text.starts_with("[tool] ")) {
            out.push_back(thought_update(text + "\n"));
        }
    } else if (type == "memory") {
        const auto chats = event.value("chats", 0);
        const auto decisions = event.value("decisions", 0);
        std::string line = "recalled";
        if (chats > 0) {
            line += " " + std::to_string(chats) + (chats == 1 ? " past chat" : " past chats");
        }
        if (decisions > 0) {
            line += std::string{chats > 0 ? " and" : ""} + " " + std::to_string(decisions) +
                    (decisions == 1 ? " decision" : " decisions");
        }
        out.push_back(thought_update(line + "\n"));
    } else if (type == "sources") {
        std::string line = "sources:";
        if (const auto sources = event.find("sources");
            sources != event.end() && sources->is_array()) {
            bool first = true;
            for (const nlohmann::json& source : *sources) {
                if (source.is_object()) {
                    line += (first ? " " : ", ") + string_of(source, "file");
                    first = false;
                }
            }
        }
        out.push_back(thought_update(line + "\n"));
    } else if (type == "todo") {
        nlohmann::json entries = nlohmann::json::array();
        if (const auto items = event.find("items"); items != event.end() && items->is_array()) {
            for (const nlohmann::json& item : *items) {
                if (!item.is_object()) {
                    continue;
                }
                entries.push_back({{"content", string_of(item, "text")},
                                   {"priority", "medium"},
                                   {"status", std::string{plan_status(string_of(item, "state"))}}});
            }
        }
        out.push_back(nlohmann::json{{"sessionUpdate", "plan"}, {"entries", std::move(entries)}});
    } else if (type == "tool_call") {
        bool opened = false;
        const std::string card = card_for(event, opened);
        const std::string status{tool_call_status(string_of(event, "state"))};
        if (opened) {
            const Card call{string_of(event, "name"), string_of(event, "target")};
            nlohmann::json update = drawn(card, call);
            update["sessionUpdate"] = "tool_call";
            update["status"] = status;
            cards_.insert_or_assign(card, call);
            out.push_back(std::move(update));
        } else {
            out.push_back(nlohmann::json{
                {"sessionUpdate", "tool_call_update"}, {"toolCallId", card}, {"status", status}});
        }
    }
    return out;
}

nlohmann::json Translator::permission_tool_call(const nlohmann::json& question) {
    // The card of the call it names: the one that id last opened.
    std::string card;
    if (const std::string id = string_of(question, "tool_call_id"); !id.empty()) {
        const auto latest = latest_.find(id);
        card = latest != latest_.end() ? latest->second : id;
    } else {
        card =
            anonymous_.empty() ? fresh_id("apogee-call-" + std::to_string(++minted_)) : anonymous_;
    }
    // Titled and located by what the question asks about -- never another
    // call's -- and by its card only when it names no tool.
    Card call{string_of(question, "tool"), string_of(question, "target")};
    if (const auto drawn_card = cards_.find(card);
        call.name.empty() && drawn_card != cards_.end()) {
        call = drawn_card->second;
    }
    nlohmann::json out = drawn(card, call);
    out["status"] = "pending";
    if (const std::string detail = string_of(question, "detail"); !detail.empty()) {
        out["content"] = nlohmann::json::array(
            {nlohmann::json{{"type", "content"}, {"content", text_block(detail)}}});
    }
    return out;
}

nlohmann::json permission_options(bool outbound) {
    return nlohmann::json::array(
        {{{"optionId", "yes"}, {"name", "Allow once"}, {"kind", "allow_once"}},
         {{"optionId", "session"},
          {"name", outbound ? "Allow this website for this session" : "Allow for this session"},
          {"kind", "allow_always"}},
         {{"optionId", "always"},
          {"name", outbound ? "Always allow this website (added to tools.allowed_hosts)"
                            : "Always allow (remembered in the config)"},
          {"kind", "allow_always"}},
         {{"optionId", "no"}, {"name", "Deny"}, {"kind", "reject_once"}}});
}

std::string permission_answer(const nlohmann::json* result) {
    if (result == nullptr || !result->is_object()) {
        return "no";
    }
    const auto outcome = result->find("outcome");
    if (outcome == result->end() || !outcome->is_object() ||
        string_of(*outcome, "outcome") != "selected") {
        return "no";
    }
    const std::string chosen = string_of(*outcome, "optionId");
    for (const std::string_view word : {"yes", "session", "always"}) {
        if (chosen == word) {
            return chosen;
        }
    }
    return "no";
}

std::string file_uri_path(std::string_view uri) {
    constexpr std::string_view kScheme = "file://";
    if (uri.size() < kScheme.size()) {
        return {};
    }
    for (std::size_t i = 0; i < kScheme.size(); ++i) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(uri[i]))) != kScheme[i]) {
            return {};
        }
    }
    std::string_view rest = uri.substr(kScheme.size());
    // `file://host/path`: the path starts at the first slash after the host.
    if (!rest.starts_with('/')) {
        const std::size_t slash = rest.find('/');
        if (slash == std::string_view::npos) {
            return {};
        }
        rest = rest.substr(slash);
    }
    if (const std::size_t end = rest.find_first_of("?#"); end != std::string_view::npos) {
        rest = rest.substr(0, end);
    }
    std::string path;
    path.reserve(rest.size());
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            const int high = hex_value(rest[i + 1]);
            const int low = hex_value(rest[i + 2]);
            if (high >= 0 && low >= 0) {
                path += static_cast<char>((high << 4) | low);
                i += 2;
                continue;
            }
        }
        path += rest[i];
    }
    return path;
}

std::string prompt_text(const nlohmann::json& prompt, std::string& error) {
    if (!prompt.is_array()) {
        error = "prompt is a list of content blocks";
        return {};
    }
    std::vector<std::string> parts;
    for (const nlohmann::json& block : prompt) {
        if (!block.is_object()) {
            error = "a prompt's content block is an object";
            return {};
        }
        const std::string type = string_of(block, "type");
        if (type == "text") {
            parts.push_back(string_of(block, "text"));
        } else if (type == "resource_link") {
            const std::string uri = string_of(block, "uri");
            const std::string path = file_uri_path(uri);
            parts.push_back(path.empty() ? uri : mention(path));
        } else if (type == "resource") {
            // Embedded content is not advertised; a host that sends a file's
            // anyway has it attached from disk, as a link would be.
            const auto resource = block.find("resource");
            const std::string path = resource != block.end() && resource->is_object()
                                         ? file_uri_path(string_of(*resource, "uri"))
                                         : std::string{};
            if (path.empty()) {
                error =
                    "embedded resource content is not taken here (promptCapabilities."
                    "embeddedContext is false): send a resource_link to the file instead";
                return {};
            }
            parts.push_back(mention(path));
        } else if (type == "image" || type == "audio") {
            error = type + " content is not taken here (promptCapabilities." + type +
                    " is false): send text and resource_link blocks";
            return {};
        } else {
            error = "a '" + type +
                    "' content block is not one Apogee takes: send text and "
                    "resource_link blocks";
            return {};
        }
    }
    std::string text;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        text += (text.empty() ? "" : "\n") + part;
    }
    if (text.empty()) {
        error = "the prompt is empty";
    }
    return text;
}

std::vector<ModeChoice> offered_modes(bool bypass_allowed, std::string_view started_in) {
    std::vector<ModeChoice> modes{
        {"manual", "Manual", "Ask before anything that writes, runs a command or reaches out"},
        {"accept-edits", "Accept edits",
         "Edit files and notes without asking; commands, MCP tools and websites still ask"},
        {"plan", "Plan", "Nothing that writes runs: the model plans"},
    };
    if (bypass_allowed || started_in == "bypass") {
        modes.push_back({"bypass", "Bypass", "Nothing asks; a config deny still refuses"});
    }
    if (started_in == "dont-ask") {
        modes.push_back({"dont-ask", "Don't ask", "Whatever would ask is refused"});
    }
    return modes;
}

nlohmann::json modes_state(const std::vector<ModeChoice>& modes, std::string_view current) {
    nlohmann::json available = nlohmann::json::array();
    for (const ModeChoice& mode : modes) {
        available.push_back({{"id", std::string{mode.id}},
                             {"name", std::string{mode.name}},
                             {"description", std::string{mode.description}}});
    }
    return nlohmann::json{{"currentModeId", std::string{current}},
                          {"availableModes", std::move(available)}};
}

nlohmann::json config_options(const std::vector<ModelChoice>& models, std::string_view model,
                              const std::vector<ModeChoice>& modes, std::string_view mode) {
    nlohmann::json model_values = nlohmann::json::array();
    for (const ModelChoice& choice : models) {
        nlohmann::json value{{"value", choice.value}, {"name", choice.name}};
        if (!choice.description.empty()) {
            value["description"] = choice.description;
        }
        model_values.push_back(std::move(value));
    }
    nlohmann::json mode_values = nlohmann::json::array();
    for (const ModeChoice& choice : modes) {
        mode_values.push_back({{"value", std::string{choice.id}},
                               {"name", std::string{choice.name}},
                               {"description", std::string{choice.description}}});
    }
    return nlohmann::json::array({{{"id", "model"},
                                   {"name", "Model"},
                                   {"description", "The backend that answers, from the next turn"},
                                   {"category", "model"},
                                   {"type", "select"},
                                   {"currentValue", std::string{model}},
                                   {"options", std::move(model_values)}},
                                  {{"id", "mode"},
                                   {"name", "Permission mode"},
                                   {"description", "How much runs without asking"},
                                   {"category", "mode"},
                                   {"type", "select"},
                                   {"currentValue", std::string{mode}},
                                   {"options", std::move(mode_values)}}});
}

nlohmann::json elicitation_params(const nlohmann::json& question, std::string_view session_id) {
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    std::string message;
    std::size_t index = 0;
    if (const auto questions = question.find("questions");
        questions != question.end() && questions->is_array()) {
        for (const nlohmann::json& entry : *questions) {
            ++index;
            const std::string asked = entry.is_object() ? string_of(entry, "question") : "";
            std::string description = asked;
            if (entry.is_object()) {
                if (const auto options = entry.find("options");
                    options != entry.end() && options->is_array() && !options->empty()) {
                    description += "\nOptions:";
                    for (const nlohmann::json& option : *options) {
                        if (!option.is_object()) {
                            continue;
                        }
                        description += "\n- " + string_of(option, "label");
                        if (const std::string said = string_of(option, "description");
                            !said.empty()) {
                            description += ": " + said;
                        }
                    }
                }
            }
            const std::string name = "answer_" + std::to_string(index);
            const std::string header = entry.is_object() ? string_of(entry, "header") : "";
            properties[name] = {
                {"type", "string"},
                {"title", header.empty() ? "Answer " + std::to_string(index) : header},
                {"description", description}};
            required.push_back(name);
            message += (message.empty() ? "" : "\n") + asked;
        }
    }
    return nlohmann::json{
        {"sessionId", std::string{session_id}},
        {"mode", "form"},
        {"message", message.empty() ? std::string{"Apogee's model asks"} : message},
        {"requestedSchema",
         {{"type", "object"}, {"properties", std::move(properties)}, {"required", required}}}};
}

std::vector<std::string> elicitation_answers(const nlohmann::json* result, std::size_t count) {
    std::vector<std::string> answers;
    const bool accepted =
        result != nullptr && result->is_object() && string_of(*result, "action") == "accept";
    const nlohmann::json* content = nullptr;
    if (accepted) {
        if (const auto found = result->find("content");
            found != result->end() && found->is_object()) {
            content = &*found;
        }
    }
    for (std::size_t index = 1; index <= count; ++index) {
        if (!accepted) {
            answers.emplace_back(
                "The person declined to answer here. Choose yourself, and say what you chose.");
            continue;
        }
        const std::string name = "answer_" + std::to_string(index);
        if (content == nullptr || !content->contains(name)) {
            answers.emplace_back("(no answer given)");
            continue;
        }
        const nlohmann::json& value = content->at(name);
        if (value.is_string()) {
            answers.push_back(value.get<std::string>());
        } else if (value.is_array()) {
            std::string joined;
            for (const nlohmann::json& item : value) {
                joined += (joined.empty() ? "" : ", ") +
                          (item.is_string() ? item.get<std::string>() : line_of(item));
            }
            answers.push_back(joined);
        } else {
            answers.push_back(line_of(value));
        }
    }
    return answers;
}

std::string nobody_to_ask() {
    return "Nobody can be asked here: this ACP host takes no questions. Choose yourself, and say "
           "what you chose.";
}

std::string user_line(std::string_view text) {
    return line_of(nlohmann::json{{"type", "user"}, {"text", std::string{text}}});
}

std::string cancel_line() {
    return line_of(nlohmann::json{{"type", "cancel"}});
}

std::string set_line(std::string_view key, std::string_view value) {
    return line_of(nlohmann::json{{"type", "set"}, {std::string{key}, std::string{value}}});
}

std::string answer_line(std::string_view text) {
    return line_of(nlohmann::json{{"type", "answer"}, {"text", std::string{text}}});
}

std::string hello_line(std::string_view client, std::string_view version) {
    nlohmann::json line{{"type", "hello"}, {"wants", nlohmann::json::array({"acp"})}};
    nlohmann::json who = nlohmann::json::object();
    if (!client.empty()) {
        who["name"] = std::string{client};
    }
    if (!version.empty()) {
        who["version"] = std::string{version};
    }
    line["client"] = std::move(who);
    return line_of(line);
}

nlohmann::json listed_sessions(const std::vector<ListedChat>& chats, std::string_view cwd) {
    nlohmann::json sessions = nlohmann::json::array();
    for (const ListedChat& chat : chats) {
        // A chat begun in a terminal has no folder, and the protocol needs
        // one for every entry: only a host's chats are listed.
        if (chat.origin.empty() || chat.cwd.empty() || (!cwd.empty() && chat.cwd != cwd)) {
            continue;
        }
        nlohmann::json entry{{"sessionId", chat.id}, {"cwd", chat.cwd}};
        if (!chat.name.empty() && chat.name != chat.id) {
            entry["title"] = chat.name;
        }
        if (!chat.updated_at.empty()) {
            entry["updatedAt"] = chat.updated_at;
        }
        sessions.push_back(std::move(entry));
    }
    return sessions;
}

std::vector<nlohmann::json> replay_updates(const std::vector<SavedEntry>& entries) {
    std::vector<nlohmann::json> out;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const SavedEntry& entry = entries[index];
        const std::string prefix = "entry-" + std::to_string(index) + "-";
        const auto message = [&](std::string_view kind, const std::string& id,
                                 std::string_view text) {
            nlohmann::json update = chunk(kind, text);
            update["messageId"] = prefix + id;
            out.push_back(std::move(update));
        };
        if (entry.compacted.has_value()) {
            // Where the chat said `history compacted`, as `chats show` says
            // it: a host that took no compaction update reads it as words.
            message("agent_message_chunk", "compaction",
                    "history compacted; what came before is this summary:\n" + *entry.compacted);
        } else if (!entry.user.empty()) {
            message("user_message_chunk", "user", entry.user);
        }
        for (std::size_t n = 0; n < entry.tools.size(); ++n) {
            const SavedEntry::Tool& tool = entry.tools[n];
            // A finished call, drawn as `chats show` lists it: never its
            // result, which the listing never shows either.
            out.push_back(nlohmann::json{
                {"sessionUpdate", "tool_call"},
                {"toolCallId", prefix + "tool-" + std::to_string(n + 1)},
                {"title", tool.summary.empty() ? tool.name : tool.name + " " + tool.summary},
                {"kind", std::string{tool_kind(tool.name)}},
                {"status", "completed"}});
        }
        for (std::size_t n = 0; n < entry.answer.size(); ++n) {
            message("agent_message_chunk", "agent-" + std::to_string(n + 1), entry.answer[n]);
        }
    }
    return out;
}

std::string refused_setting(std::string_view notice) {
    // A set line that did not take is said `set <key>: <why>` (52d); one the
    // line named wrongly, `set: ...`.
    if (!notice.starts_with("set ")) {
        return {};
    }
    const std::string_view rest = notice.substr(4);
    const std::size_t colon = rest.find(':');
    if (colon == std::string_view::npos || colon == 0 ||
        rest.substr(0, colon).find(' ') != std::string_view::npos) {
        return {};
    }
    return std::string{rest.substr(0, colon)};
}

}  // namespace apogee::acp
