#include "agentloop/loop.h"

#include <stdexcept>
#include <utility>

#include "harness/errors.h"

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

}  // namespace

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
    RunResult result;
    result.tokens.estimated = false;

    const std::vector<harness::Tool> tools = advertised_tools(options);

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
        request.messages =
            splice_transient(history, options.transient_prefix, options.transient_at);
        if (!options.transient_prefix.empty()) {
            // The markers a provider with a persistent prompt cache reads to
            // keep injected context out of its cached prefix.
            request.transient.start = options.transient_at;
            request.transient.length = options.transient_prefix.size();
        }
        // On the final pass the tools are withdrawn, which is what forces an
        // answer instead of another tool call.
        if (!final_pass) {
            request.tools = tools;
        }

        harness::ChatResponse response;
        bool streamed = false;
        std::string streamed_text;

        harness::StreamOptions stream;
        stream.cancellation = options.cancellation;
        stream.on_thinking = [&reporter](std::string_view chunk) {
            // Thinking goes to its own channel and never into the answer.
            reporter.on_thinking_token(chunk);
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

                if (options.tools == nullptr) {
                    // No registry at all: a hallucinated call still gets a
                    // readable result rather than a crash.
                    append_result(history, call,
                                  "Error: no tool named '" + call.name +
                                      "'. No tools are "
                                      "available in this conversation.");
                    continue;
                }

                const agent::ToolOutcome outcome =
                    agent::dispatch(*options.tools, call, dispatch_context);
                append_result(history, call, outcome.content);
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
