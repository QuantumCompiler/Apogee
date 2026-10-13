#include "tools/delegate.h"

#include <map>
#include <optional>
#include <string>
#include <utility>

#include "agentloop/member_call.h"
#include "agentloop/side_call.h"
#include "agentloop/tool_selection.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/roles.h"
#include "tools/args.h"

namespace apogee::tools {
namespace {

/// The context `delegate` uses on this thread, or none.
thread_local DelegateContext* current_context = nullptr;

/// The sub-session's tool lines, said as side calls in the parent (26n):
/// each opened when its tool starts and closed when the next step does, so
/// the parent's surface draws them inside its thinking block as a consult's
/// are -- and machine mode says them as `tool_status`. The sub-session's own
/// answer and reasoning are never shown: the parent reads the answer as the
/// tool's result.
class Narrator final : public agentloop::Reporter {
public:
    explicit Narrator(agentloop::Reporter* parent) : parent_{parent} {}

    void on_tool_status(std::string_view detail) override {
        finish();
        if (parent_ == nullptr || detail.empty()) {
            return;
        }
        open_ = agentloop::SideCall{.role = "delegate", .detail = std::string{detail}};
        parent_->on_side_call(*open_);
    }

    void on_thinking() override {
        finish();
    }

    void on_answer_start() override {
        finish();
    }

    /// Closes the open tool line, if any.
    void finish() {
        if (open_.has_value() && parent_ != nullptr) {
            open_->done = true;
            parent_->on_side_call(*open_);
        }
        open_.reset();
    }

private:
    agentloop::Reporter* parent_;
    std::optional<agentloop::SideCall> open_;
};

/// `text`'s first line, cut to `limit` characters with an ellipsis.
[[nodiscard]] std::string headline(std::string_view text, std::size_t limit) {
    std::string line{text.substr(0, text.find('\n'))};
    if (line.size() > limit) {
        line = line.substr(0, limit) + "…";
    }
    return line;
}

/// The names of the suite roles, `, `-joined.
[[nodiscard]] std::string role_list() {
    std::string out;
    for (const std::string_view name : harness::suite_role_names()) {
        out += (out.empty() ? "" : ", ") + std::string{name};
    }
    return out;
}

constexpr std::string_view kFraming =
    "You are a delegate: a fresh session handed one self-contained task by the session you work "
    "for. Do it with the tools you have, on your own -- there is nobody to ask. Then reply with "
    "what you found or did, briefly and concretely, naming files by path: your reply goes back "
    "to that session as its tool's result, not to a person. If you could not finish, say what "
    "stopped you and what is left.\n\nThe task:\n";

}  // namespace

DelegateScope::DelegateScope(DelegateContext context)
    : context_{std::move(context)}, previous_{current_context} {
    current_context = &context_;
}

DelegateScope::~DelegateScope() {
    current_context = previous_;
}

agent::ToolRegistry delegate_kit(const agent::ToolRegistry& registry) {
    agent::ToolRegistry kit;
    kit.set_environment(registry.environment_source());
    for (const std::string& name : registry.names()) {
        if (name == agentloop::kDelegateToolName) {
            continue;  // depth one: a delegate cannot delegate
        }
        if (const agent::Tool* tool = registry.find(name); tool != nullptr) {
            kit.add(*tool);
        }
    }
    return kit;
}

std::string delegate_policy() {
    return "Delegating: for a self-contained piece of a larger job -- reading through many "
           "files, a search across the project, a side investigation -- you can hand it to "
           "`delegate` with a brief that says everything it needs, since the brief is all it "
           "gets. It works in a fresh context with your tools and comes back with its "
           "conclusion, which keeps your own context for the job itself. Do small steps "
           "yourself.";
}

agent::ToolOutcome run_delegate(const DelegateContext& context, std::string_view arguments) {
    if (context.harness == nullptr) {
        return error("there is no session here to delegate from");
    }
    agent::ToolOutcome failure;
    const std::optional<Arguments> args = parse_arguments(
        arguments, R"({"brief": "Find every caller of parse_config and list them by file"})",
        failure);
    if (!args.has_value()) {
        return failure;
    }
    const std::string brief = args->string("brief");
    if (brief.empty()) {
        return error("brief is required: the whole task, as the delegate will know nothing else");
    }

    // The model: the session's own, or a role's through the one resolver --
    // never a backend named by the model.
    std::string model = context.parent.model;
    if (const std::string role_name = args->string("role"); !role_name.empty()) {
        const std::optional<harness::ModelRole> role = agentloop::role_named(role_name);
        if (!role.has_value()) {
            return error("'" + role_name + "' is not a role -- one of " + role_list() +
                         ", or leave it out for this session's own model");
        }
        model = harness::resolve_backend(
                    context.harness->config(),
                    harness::RoleRequest{.role = *role, .conversation = context.parent.model})
                    .key;
        if (model.empty()) {
            return error("no backend answers for the " + role_name +
                         " role -- leave the role out for this session's own model");
        }
        // A delegation is the model's initiative, which never spends (27f's
        // rule): another backend that bills per call is refused.
        if (model != context.parent.model && context.harness->generation_is_metered(model)) {
            return error("the " + role_name + " role is '" + model +
                         "', billed per call -- a delegation the model starts never spends: "
                         "leave the role out for this session's own model");
        }
    }

    const agent::ToolRegistry kit =
        context.tools != nullptr ? delegate_kit(*context.tools) : agent::ToolRegistry{};
    agentloop::Options sub;
    sub.model = model;
    sub.temperature = context.parent.temperature;
    sub.max_tokens = kDelegateAnswerTokens;
    sub.thinking = model == context.parent.model ? context.parent.thinking : harness::Thinking{};
    sub.thinking_judge = context.parent.thinking_judge;
    sub.summary_model = context.parent.summary_model;
    if (!kit.empty()) {
        sub.tools = &kit;
        // The parent's own gate, whole: its permissions, its session's
        // answers and mode, its prompt -- and with nobody to ask, a deny.
        sub.permission = context.parent.permission;
        sub.confirm = context.parent.confirm;
        sub.refusal = context.parent.refusal;
    }
    sub.cancellation = context.parent.cancellation;
    sub.stream_answer = false;
    sub.max_iterations = kDelegateSubTurns;
    for (const std::string& note : context.notes) {
        sub.transient_prefix.push_back(harness::ChatMessage::system(note));
    }

    std::vector<harness::ChatMessage> history{
        harness::ChatMessage::user(std::string{kFraming} + brief)};
    Narrator narrator{context.reporter};
    agentloop::RunResult result;
    {
        const agentloop::SideCallSink sink =
            context.reporter == nullptr
                ? agentloop::SideCallSink{}
                : agentloop::SideCallSink{
                      [reporter = context.reporter](const agentloop::SideCall& call) {
                          reporter->on_side_call(call);
                      }};
        agentloop::SideCallScope said{sink, "delegate",
                                      "a sub-session on " + model + ": " + headline(brief, 60)};
        try {
            result = agentloop::run(*context.harness, history, sub, narrator);
        } catch (const harness::CancelledError&) {
            narrator.finish();
            throw;  // the parent's turn is cancelled too; dispatch says so
        } catch (const harness::HarnessError& e) {
            narrator.finish();
            return error(std::string{"the delegate failed: "} + e.what());
        }
        narrator.finish();
        said.tokens(result.tokens.tokens);
    }

    // What it did, in one line: its calls, its tools, its spend.
    std::map<std::string, int> used;
    for (const harness::ChatMessage& message : history) {
        for (const harness::ToolCall& call : message.tool_calls) {
            ++used[call.name];
        }
    }
    std::string tools;
    for (const auto& [name, count] : used) {
        tools +=
            (tools.empty() ? "" : ", ") + name + (count > 1 ? " ×" + std::to_string(count) : "");
    }
    std::string account = "delegate on " + model + ": " + std::to_string(result.iterations) +
                          " model call" + (result.iterations == 1 ? "" : "s") + ", " +
                          (tools.empty() ? std::string{"no tools"} : "tools " + tools) + ", " +
                          std::to_string(result.tokens.tokens) +
                          (result.tokens.estimated ? " tokens (estimated)" : " tokens");
    if (result.hit_iteration_limit) {
        account += " -- stopped at its " + std::to_string(kDelegateSubTurns) +
                   "-call cap and answered without tools";
    }
    std::string answer = trim(result.answer);
    if (answer.empty()) {
        answer = "(the delegate gave no answer)";
    }
    return ok(answer + "\n\n[" + account + "]");
}

void register_delegate_tool(agent::ToolRegistry& registry) {
    agent::Tool tool;
    tool.name = std::string{agentloop::kDelegateToolName};
    tool.description =
        "Hand a self-contained task to a delegate: a fresh session with your tools (but not "
        "this one), under the same permissions, that works on its own and returns its "
        "conclusion as this tool's result. Write the brief as the whole task -- the delegate "
        "sees nothing else of this conversation. It makes at most " +
        std::to_string(kDelegateSubTurns) +
        " model calls. Optionally name a role (utility, extraction, ...) to run it on that "
        "role's model instead of this session's.";
    tool.parameters_schema =
        R"({"type":"object","properties":{"brief":{"type":"string","description":"The whole task, self-contained: what to do, where, and what to report"},"role":{"type":"string","description":"Optional: a role whose model runs the delegate; leave out for this session's own model"}},"required":["brief"]})";
    tool.run = [](std::string_view arguments) -> agent::ToolOutcome {
        if (current_context == nullptr) {
            return error("there is no session here to delegate from");
        }
        return run_delegate(*current_context, arguments);
    };
    registry.add(std::move(tool));
}

}  // namespace apogee::tools
