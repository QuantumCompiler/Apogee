#include "agentloop/loop.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

#include "agentloop/budget.h"
#include "agentloop/structured.h"
#include "agentloop/thinking.h"
#include "agentloop/tool_summary.h"
#include "contracts/errors.h"

namespace apogee::agentloop {
namespace {

/// Trims leading and trailing whitespace.
std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\n' ||
                                   text[begin] == '\r' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' ||
                           text[end - 1] == '\t')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

void accumulate(TokenCount& total, harness::Usage& split, const harness::Usage& usage) {
    if (usage.reported()) {
        total.tokens += usage.total_tokens();
        split.prompt_tokens += usage.prompt_tokens;
        split.completion_tokens += usage.completion_tokens;
        // `estimated` stays whatever it was: a total mixing an exact count with
        // an estimated one is an estimate, and saying otherwise overstates it.
        return;
    }
    total.estimated = true;
}

/// Appends a tool result to history, linked to the call it answers.
void append_result(std::vector<harness::ChatMessage>& history, const harness::ToolCall& call,
                   const std::string& content) {
    harness::ToolResult result;
    result.tool_call_id = call.id;
    result.name = call.name;
    result.content = content;
    history.push_back(harness::ChatMessage::from_tool_result(result));
}

/// The question a turn's tools are ranked for: the surface's restatement
/// when it has one, else the last user message as written.
std::string selection_query(const Options& options,
                            const std::vector<harness::ChatMessage>& history) {
    if (!options.selection_query.empty()) {
        return options.selection_query;
    }
    for (auto message = history.rbegin(); message != history.rend(); ++message) {
        if (message->role == harness::Role::User) {
            return message->content.plain_text();
        }
    }
    return {};
}

/// The tools one step offers (26g): with selection off, all of `tools`;
/// with it on, the offered ones in the registry's order, then `find_tools`
/// naming the rest, then `ask_user` -- which is offered whenever someone can
/// answer it.
std::vector<harness::Tool> step_tools(const std::vector<harness::Tool>& tools,
                                      const ToolSelection* selection, bool active) {
    if (selection == nullptr || !active) {
        return tools;
    }
    std::vector<harness::Tool> out;
    std::vector<std::string> hidden;
    std::optional<harness::Tool> question;
    for (const harness::Tool& tool : tools) {
        if (tool.name == kQuestionToolName) {
            question = tool;
        } else if (selection->offered(tool.name)) {
            out.push_back(tool);
        } else {
            hidden.push_back(tool.name);
        }
    }
    if (selection->offered(kFindToolsName)) {
        out.push_back(find_tools_tool(hidden));
    }
    if (question.has_value()) {
        out.push_back(std::move(*question));
    }
    return out;
}

/// A call's identity for the repeated-call guard: the tool and its
/// arguments as JSON, so `{"a":1, "b":2}` and `{"b":2,"a":1}` are one call.
std::string call_key(const harness::ToolCall& call) {
    const nlohmann::json parsed = nlohmann::json::parse(call.arguments, nullptr, false);
    return call.name + '\x1f' + (parsed.is_discarded() ? call.arguments : parsed.dump());
}

/// The last user message in `history`, as written: the request a tool call
/// serves, the question an answer answers (27g).
std::string last_user_text(const std::vector<harness::ChatMessage>& history) {
    for (const harness::ChatMessage& message : std::ranges::reverse_view(history)) {
        if (message.role == harness::Role::User) {
            return message.content.plain_text();
        }
    }
    return {};
}

/// What a gated tool's call passes before any verifier is woken (27g): its
/// arguments a JSON object, matching the tool's own parameter schema, and
/// the tool's own precheck -- a path that must exist. In that order, the
/// first failure deciding.
std::vector<StructuralCheck> tool_structure(const agent::Tool& tool, std::string_view arguments) {
    std::vector<StructuralCheck> out;
    out.emplace_back([arguments]() -> std::string {
        const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return "the arguments are not a JSON object";
        }
        return {};
    });
    out.emplace_back([&tool, arguments]() -> std::string {
        const nlohmann::json schema = nlohmann::json::parse(tool.parameters_schema, nullptr, false);
        if (schema.is_discarded() || !validate_schema(schema).ok) {
            return {};  // a schema that cannot judge is not the call's fault
        }
        const ValidationResult checked = validate_against(schema, nlohmann::json::parse(arguments));
        if (checked.ok) {
            return {};
        }
        std::string errors;
        for (const std::string& error : checked.errors) {
            errors += (errors.empty() ? "" : "; ") + error;
        }
        return "they do not match its parameters: " + errors;
    });
    if (tool.precheck) {
        out.emplace_back([&tool, arguments]() { return tool.precheck(arguments); });
    }
    return out;
}

/// The question put to the producer about an objection to its answer.
std::string objection_question(std::string_view objection) {
    return "A reviewer checked your last answer and objected:\n\n" + std::string{objection} +
           "\n\nIf the objection is right, give the corrected answer. If it is wrong, say "
           "briefly why your answer stands.";
}

}  // namespace

std::string answer_objection(const harness::Harness& harness, const std::string& model,
                             const std::vector<harness::ChatMessage>& history,
                             std::string_view objection, std::optional<std::int64_t> max_tokens,
                             const harness::CancellationToken& cancellation) {
    std::vector<harness::ChatMessage> asked = history;
    asked.push_back(harness::ChatMessage::user(objection_question(objection)));
    harness::ChatRequest request;
    request.model = model;
    request.max_tokens = max_tokens;
    // Not a turn of the conversation: its own context on a local backend,
    // and the conversation's cache untouched.
    request.transient.side_request = true;
    const TurnBudget budget = turn_budget(harness, model, max_tokens);
    Assembly assembly =
        assemble_request(budget, request, asked, {}, {}, 0, current_turn_start(asked));
    request.messages = std::move(assembly.messages);
    return trim(harness.chat(request, cancellation).message.content.plain_text());
}

Validated check_answer(const harness::Harness& harness, const std::string& model,
                       const std::vector<harness::ChatMessage>& history, const Verifier& verifier,
                       const SideCallSink& narrate, std::optional<std::int64_t> max_tokens,
                       const harness::CancellationToken& cancellation) {
    std::string answer;
    if (!history.empty() && history.back().role == harness::Role::Assistant) {
        answer = trim(history.back().content.plain_text());
    }
    if (answer.empty()) {
        Validated nothing;
        nothing.result = Validated::Result::Unchecked;
        nothing.notes.emplace_back("there is no answer to check yet");
        return nothing;
    }
    const std::string question = last_user_text(history);
    return validate_artifact(
        answer,
        [&](const std::string& artifact) {
            return run_checks({}, &verifier, [&] { return answer_brief(question, artifact); });
        },
        [&](const std::string& /*artifact*/, const std::string& objection,
            std::string& note) -> std::optional<std::string> {
            std::string revised;
            try {
                const SideCallScope said{narrate, "validate",
                                         "asking " + model + " to answer the objection"};
                revised =
                    answer_objection(harness, model, history, objection, max_tokens, cancellation);
            } catch (const harness::CancelledError&) {
                throw;
            } catch (const std::exception& e) {
                note = std::string{"the model could not answer it: "} + e.what();
                return std::nullopt;
            }
            if (revised.empty()) {
                note = "the model gave no answer to it";
                return std::nullopt;
            }
            return revised;
        },
        // An answer is already the user's to read: the one verifier call is
        // the measure, and the model's reply stands beside it, unverified.
        false);
}

std::vector<harness::Tool> advertised_tools(const Options& options) {
    std::vector<harness::Tool> tools;
    if (options.tools != nullptr) {
        tools = options.tools->definitions();
    }
    // The availability rule, in one place: ask_user is advertised if and only
    // if there is someone to answer it.
    if (options.ask) {
        tools.push_back(question_tool());
    }
    return tools;
}

RunResult run(const harness::Harness& harness, std::vector<harness::ChatMessage>& history,
              const Options& options, Reporter& reporter) {
    // A model with no chat template has no tool format (26r): offered tools,
    // it only imitates calling them. Withheld here, once, for every surface --
    // ask_user with them, since it is one -- and each surface says so where
    // it starts. The turn itself runs: framing, never a gate.
    if ((options.tools != nullptr || options.ask) &&
        harness.model_behavior_for(options.model).base_model) {
        Options bare = options;
        bare.tools = nullptr;
        bare.tool_selection = nullptr;
        bare.ask = {};
        return run(harness, history, bare, reporter);
    }

    RunResult result;
    result.tokens.estimated = false;

    // The turn's member calls (27f): counted from zero, said where this
    // run's other model calls are said, and cancelled with the turn -- and a
    // play the model started that could not run kept as a notice (27t).
    std::optional<MemberCalls::Turn> member_turn;
    if (options.member_calls != nullptr) {
        member_turn.emplace(options.member_calls->begin_turn(
            [&reporter](const SideCall& call) { reporter.on_side_call(call); },
            options.cancellation,
            [&reporter](std::string_view line) { reporter.on_notice(line); }));
    }

    // The suite's validation (27g), where a turn has a member-call budget:
    // a gated tool's arguments before it runs, and a standing answer check.
    // With no suite, no `validate:` block or a seam off, nothing changes. A
    // side run -- a clerk -- is never validated, and a structured answer is
    // held by its schema, which a model is never asked about.
    const SideCallSink side = [&reporter](const SideCall& call) { reporter.on_side_call(call); };
    const bool validating = options.member_calls != nullptr && !options.side_request;
    const bool check_tools = validating && seam_on(harness.config(), Seam::ToolArgs);
    const bool check_answers =
        validating && options.response_schema.empty() && seam_on(harness.config(), Seam::Answer);
    std::optional<Verifier> verifier;
    std::string no_verifier;
    if (check_tools || check_answers) {
        if (const VerifierRole role = verifier_role(harness.config()); role.missing.empty()) {
            verifier = bind_verifier(harness, *options.member_calls, role.role);
        } else {
            no_verifier = role.missing;
        }
    }
    ToolArgChecks tool_checks;

    const std::vector<harness::Tool> tools = advertised_tools(options);

    // The turn's tools, chosen once (26g): a step offers the same set, so a
    // model that caches its prompt reads only what is new, until a step
    // needs more.
    ToolSelection* const selection = options.tools != nullptr ? options.tool_selection : nullptr;
    bool selecting = false;
    if (selection != nullptr) {
        const ToolOffer offer =
            selection->begin_turn(selection_query(options, history), options.cancellation);
        selecting = offer.active;
        if (selecting) {
            reporter.on_progress(describe_offer(offer, options.tools->size()));
        }
    }

    // The environment note rides first, ahead of any retrieval, whenever
    // there are tools (25d). Rendered once per turn: it changes once a day,
    // so a local model's cached prompt survives from turn to turn and step
    // to step. Unlike retrieval, the budget never trims it.
    std::vector<harness::ChatMessage> pinned;
    if (options.tools != nullptr) {
        if (std::string note = options.tools->environment(); !note.empty()) {
            pinned.push_back(harness::ChatMessage::system(std::move(note)));
        }
    }

    // Every request of the turn is assembled against the model's window
    // (26c): earlier turns' tool results as stubs, and on overflow the
    // lowest priorities trimmed. Only what is sent changes, never `history`.
    const TurnBudget budget = turn_budget(harness, options.model, options.max_tokens);

    // Whether this question is reasoned about (26i): asked once, so every
    // step of the turn agrees -- a tool loop that thought on one step and
    // not the next would read as two answers.
    ThinkingDecision decided;
    const harness::Thinking thinking =
        resolve_turn_thinking(harness, options.thinking, options.thinking_judge,
                              selection_query(options, history), options.cancellation, &decided);
    if (options.thinking.mode == harness::ThinkingMode::Auto) {
        reporter.on_progress(std::string{"thinking: auto -- "} + (thinking.off() ? "off" : "on") +
                             ", by " + decided.by);
    }
    if (check_tools && !verifier.has_value()) {
        reporter.on_notice("validate: tool arguments checked by structure only -- " + no_verifier);
    }
    const std::size_t turn_start = current_turn_start(history);
    bool stubs_said = false;
    std::vector<std::string> trims_said;

    // How often each call has run in this turn. The third identical one is
    // answered without running: a small local model re-reads the same file
    // and re-runs the same command in a loop (the 25b spike's 3B model read
    // one file three times and ran the shell eight), and the answer it needs
    // is already in its history.
    std::map<std::string, int> call_counts;

    // Tools that said they cannot work for the rest of this turn
    // (`ToolOutcome::unavailable`): no longer offered, and a call that still
    // names one is answered without running it.
    std::set<std::string, std::less<>> withdrawn;

    agent::DispatchContext dispatch_context;
    dispatch_context.permission = options.permission;
    dispatch_context.confirm = options.confirm;
    dispatch_context.on_status = [&reporter](std::string_view detail) {
        // An empty status means "back to rest" -- reported as thinking rather
        // than as an empty tool status, which the Reporter contract forbids.
        if (detail.empty()) {
            reporter.on_thinking();
        } else {
            reporter.on_tool_status(detail);
        }
    };

    for (int iteration = 0;; ++iteration) {
        options.cancellation.throw_if_cancelled();

        const bool final_pass = iteration >= options.max_iterations;
        result.iterations = iteration + 1;

        reporter.on_thinking();

        harness::ChatRequest request;
        request.model = options.model;
        request.temperature = options.temperature;
        request.max_tokens = options.max_tokens;
        request.thinking = thinking;
        // On the final pass the tools are withdrawn, which is what forces an
        // answer instead of another tool call. Set before the messages: the
        // budget counts the definitions too.
        if (!final_pass) {
            request.tools = step_tools(tools, selection, selecting);
            std::erase_if(request.tools, [&withdrawn](const harness::Tool& tool) {
                return withdrawn.contains(tool.name);
            });
        }
        Assembly assembly =
            assemble_request(budget, request, history, pinned, options.transient_prefix,
                             options.transient_at, turn_start, options.inline_attachments);
        for (const std::string& name : assembly.inline_dropped) {
            if (std::ranges::find(result.inline_dropped, name) == result.inline_dropped.end()) {
                result.inline_dropped.push_back(name);
            }
        }
        request.messages = std::move(assembly.messages);
        if (assembly.transient_length > 0) {
            // The markers a provider with a persistent prompt cache reads to
            // keep injected context out of its cached prefix.
            request.transient.start = assembly.transient_start;
            request.transient.length = assembly.transient_length;
        }
        if (assembly.stubs > 0 && !stubs_said) {
            // Every turn after one with tools, so said only under --verbose.
            reporter.on_progress(
                "earlier turns' tool results sent as " + std::to_string(assembly.stubs) + " stub" +
                (assembly.stubs == 1 ? "" : "s") + " (" +
                std::to_string((assembly.stubbed_bytes + 1023) / 1024) + " KB not re-sent)");
            stubs_said = true;
        }
        if (!assembly.trims.empty() && assembly.trims != trims_said) {
            // Trimmed to fit: said every time it changes, never silently.
            std::string line = "context budget: ";
            for (std::size_t index = 0; index < assembly.trims.size(); ++index) {
                line += (index == 0 ? "" : "; ") + assembly.trims[index];
            }
            reporter.on_notice(line);
        }
        trims_said = std::move(assembly.trims);
        // The schema rides every request: a provider whose JSON mode cannot
        // coexist with tools applies it on the tools-less final pass, and one
        // whose mode can applies it throughout.
        request.transient.response_schema = options.response_schema;
        request.transient.side_request = options.side_request;

        harness::ChatResponse response;
        bool streamed = false;
        std::string streamed_text;

        harness::StreamOptions stream;
        stream.cancellation = options.cancellation;
        stream.on_thinking = [&reporter](std::string_view chunk) {
            // Thinking goes to its own channel and never into the answer.
            reporter.on_thinking_token(chunk);
        };
        stream.on_status = [&reporter](const harness::StatusEvent& event) {
            // Only notices and progress notes cross here: a model's load
            // progress is the surface's own business, reported before the turn.
            if (event.detail.empty()) {
                return;
            }
            if (event.type == harness::StatusEvent::Type::Notice) {
                reporter.on_notice(event.detail);
            } else if (event.type == harness::StatusEvent::Type::PromptCache) {
                reporter.on_progress(event.detail);
            } else if (event.type == harness::StatusEvent::Type::ThinkingBudget) {
                reporter.on_thinking_budget_reached();
            }
        };
        stream.on_token = [&](std::string_view chunk) {
            if (chunk.empty()) {
                return;
            }
            streamed_text += chunk;
            if (!options.stream_answer) {
                return;
            }
            if (!streamed) {
                // Only now is it certain there IS an answer rather than another
                // tool call, so this is where the status is cleared.
                reporter.on_clear_status();
                reporter.on_answer_start();
                streamed = true;
            }
            reporter.on_answer_token(chunk);
        };

        try {
            response = harness.stream_chat(request, stream);
        } catch (const harness::HarnessError&) {
            reporter.on_clear_status();
            throw;
        }
        accumulate(result.tokens, result.usage, response.usage);

        const std::vector<harness::ToolCall> calls = response.message.tool_calls;

        if (calls.empty() || final_pass) {
            if (streamed) {
                reporter.on_answer_end();
            } else {
                reporter.on_clear_status();
            }

            const std::string answer =
                trim(streamed_text.empty() ? response.message.content.plain_text() : streamed_text);

            harness::ChatMessage assistant = harness::ChatMessage::assistant(answer);
            history.push_back(std::move(assistant));

            if (!streamed && options.stream_answer && !answer.empty()) {
                // A provider that answered without streaming: the answer must
                // still reach the surface.
                reporter.on_answer_start();
                reporter.on_answer_token(answer);
                reporter.on_answer_end();
            }

            result.answer = answer;
            result.finish_reason = response.finish_reason;
            result.hit_iteration_limit = final_pass && !calls.empty();

            // The standing answer check (27g): once the answer is given, the
            // verifier once; a pass is its line in the thinking block, and
            // anything else is said after the answer -- never into history.
            if (check_answers && !answer.empty()) {
                if (verifier.has_value()) {
                    Validated checked =
                        check_answer(harness, options.model, history, *verifier, side,
                                     options.max_tokens, options.cancellation);
                    if (checked.result != Validated::Result::Passed) {
                        for (const std::string& line : answer_lines(checked, "validate")) {
                            reporter.on_notice(line);
                        }
                    }
                    result.answer_check = std::move(checked);
                } else {
                    reporter.on_notice("validate: the answer was not checked -- " + no_verifier);
                }
                reporter.on_clear_status();
            }
            return result;
        }

        // A tool phase. Everything appended from here is rolled back together
        // if the phase aborts, so an interrupted turn leaves no dangling
        // assistant message with unanswered tool calls.
        const std::size_t mark = history.size();

        harness::ChatMessage assistant = harness::ChatMessage::assistant(
            trim(streamed_text.empty() ? response.message.content.plain_text() : streamed_text));
        assistant.tool_calls = calls;
        history.push_back(std::move(assistant));

        try {
            for (const harness::ToolCall& call : calls) {
                options.cancellation.throw_if_cancelled();

                if (options.ask && call.name == kQuestionToolName) {
                    // ask_user is intercepted BEFORE dispatch: it is not a
                    // registry tool, and its arguments are validated here so a
                    // malformed call becomes a tool result the model can fix
                    // rather than an aborted turn.
                    std::string encoded;
                    try {
                        const QuestionRequest question = parse_question_request(call.arguments);
                        reporter.on_clear_status();  // the prompt owns the terminal
                        encoded = encode_answers(question, options.ask(question));
                        reporter.on_thinking();
                    } catch (const std::invalid_argument& e) {
                        encoded = std::string{"Error: invalid "} + std::string{kQuestionToolName} +
                                  " call -- " + e.what() + ". Fix the arguments and call " +
                                  std::string{kQuestionToolName} + " again.";
                    }
                    append_result(history, call, encoded);
                    continue;
                }

                if (selecting && call.name == kFindToolsName) {
                    // Answered here, like ask_user: it searches the registry
                    // rather than acting, and what it finds is offered from
                    // the next step (26g, default taken).
                    std::vector<std::string> added;
                    const std::string query = find_tools_query(call.arguments);
                    const std::string found = selection->find(query, options.cancellation, added);
                    if (!added.empty()) {
                        std::string line = "find_tools \"" + query + "\" offered";
                        for (std::size_t index = 0; index < added.size(); ++index) {
                            line += (index == 0 ? " " : ", ") + added[index];
                        }
                        reporter.on_progress(line);
                    }
                    append_result(history, call, found);
                    continue;
                }

                if (selecting && options.tools != nullptr &&
                    options.tools->find(call.name) != nullptr && selection->offer(call.name)) {
                    // Named without being offered -- a model remembers tools
                    // from earlier turns. It is dispatched and gated as any
                    // call is, and offered from the next step.
                    reporter.on_progress(
                        call.name + " called without being offered: offered from the next step");
                }

                if (options.tools == nullptr) {
                    // No registry at all: a hallucinated call still gets a
                    // readable result rather than a crash.
                    append_result(history, call,
                                  "Error: no tool named '" + call.name +
                                      "'. No tools are "
                                      "available in this conversation.");
                    continue;
                }

                if (++call_counts[call_key(call)] >= kRepeatedCallLimit) {
                    append_result(history, call,
                                  "You have already called " + call.name +
                                      " with these same arguments in this turn, and its "
                                      "result is above. Use that result instead of calling "
                                      "it again.");
                    continue;
                }

                if (withdrawn.contains(call.name)) {
                    append_result(history, call,
                                  "Error: " + call.name +
                                      " is unavailable for the rest of this turn (see its "
                                      "error above). Answer without it.");
                    continue;
                }

                // Tool-argument validation (27g), between selection and
                // execution: a gated tool's call is checked before the gate
                // ever sees it. Round one's objection is the call's result,
                // and it does not run; the revision is checked again, and
                // runs -- through the gate, as any call does -- after a pass
                // or, at the round limit, with the dispute said first.
                if (check_tools) {
                    if (const agent::Tool* tool = options.tools->find(call.name);
                        tool != nullptr && agent::gated(*tool)) {
                        const ToolArgChecks::Decision decided = tool_checks.check(
                            call.name, call.arguments, tool_structure(*tool, call.arguments),
                            verifier.has_value() ? &*verifier : nullptr, [&]() {
                                return tool_args_brief(last_user_text(history), call.name,
                                                       tool->description, call.arguments);
                            });
                        for (const std::string& line : decided.said) {
                            reporter.on_notice(line);
                        }
                        if (decided.result.has_value()) {
                            append_result(history, call, *decided.result);
                            continue;
                        }
                    }
                }

                const agent::ToolOutcome outcome =
                    agent::dispatch(*options.tools, call, dispatch_context);
                if (outcome.unavailable) {
                    withdrawn.insert(call.name);
                }
                std::string content = outcome.content;
                if (!options.summary_model.empty() && content.size() > kToolSummaryThreshold) {
                    // The utility model reads it first, so the chat model
                    // reads a summary instead of the whole of it (26b).
                    std::optional<SideCallScope> said;
                    said.emplace([&reporter](const SideCall& side) { reporter.on_side_call(side); },
                                 "utility",
                                 "summarising " + call.name + "'s " +
                                     std::to_string((content.size() + 1023) / 1024) +
                                     " KB result with " + options.summary_model);
                    std::optional<std::string> summary = summarize_tool_result(
                        harness, options.summary_model, call, content, options.cancellation);
                    said.reset();
                    if (summary.has_value()) {
                        reporter.on_progress(call.name + "'s " +
                                             std::to_string((content.size() + 1023) / 1024) +
                                             " KB result summarised by " + options.summary_model);
                        content = std::move(*summary);
                    }
                }
                append_result(history, call, content);
            }
        } catch (...) {
            // Roll the half-turn back. An aborted ask_user must not leave an
            // assistant message whose tool calls were never answered -- that
            // history is rejected outright by several providers.
            history.resize(mark);
            reporter.on_clear_status();
            throw;
        }
    }
}

RunResult run(const harness::Harness& harness, std::vector<harness::ChatMessage>& history,
              const Options& options) {
    NullReporter reporter;
    return run(harness, history, options, reporter);
}

}  // namespace apogee::agentloop
