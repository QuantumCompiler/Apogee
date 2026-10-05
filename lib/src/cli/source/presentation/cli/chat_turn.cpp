#include "cli/chat_turn.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agentloop/budget.h"
#include "agentloop/content.h"
#include "agentloop/query_rewrite.h"
#include "agentloop/rag.h"
#include "agentloop/side_call.h"
#include "cli/chat_attachments.h"
#include "cli/chat_recall.h"
#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "logger/operational.h"

namespace apogee::commands {

harness::StatusSink progress_sink(agentloop::Reporter& reporter) {
    return [&reporter](const harness::StatusEvent& event) {
        if (!event.detail.empty()) {
            reporter.on_progress(event.detail);
        }
    };
}

ChatTurnResult run_chat_turn(const harness::Harness& harness, logger::Session& session,
                             const std::string& input, const agent::ToolRegistry* tools,
                             agentloop::ToolSelection* selection,
                             agentloop::MemberCalls* member_calls, const agentloop::AskFn& ask,
                             const ToolGate& gate, agentloop::Reporter& reporter,
                             const std::function<void(const std::string&)>& notice,
                             const RagSettings& rag, const std::string& review_note,
                             ChatAttachments* attached, ChatRecall* recall,
                             const harness::CancellationToken& cancellation) {
    ChatTurnResult outcome;
    const std::vector<harness::ChatMessage> incoming = build_messages({}, {}, input, {});
    // The turn's model calls besides the chat model's own, said where the
    // surface says them -- the terminal, inside the thinking block (26n).
    const agentloop::SideCallSink side = [&reporter](const agentloop::SideCall& call) {
        reporter.on_side_call(call);
    };

    // Context is measured against what is ABOUT TO BE SENT -- the saved history
    // plus this turn -- not the history alone. Measuring before appending means
    // the first turn always reads as empty, and a single large prompt never
    // trips the threshold it should.
    std::vector<harness::ChatMessage> prospective = session.messages;
    prospective.insert(prospective.end(), incoming.begin(), incoming.end());
    const agentloop::ContextUsage usage =
        agentloop::measure_context(harness, prospective, session.backend);

    if (usage.should_compact()) {
        // The utility model summarises when one is set, else the chat's own
        // backend, as before (26b).
        const std::string compactor =
            helper_backend(harness.config(), harness::ModelRole::Utility, session.backend);
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) +
               "% full -- compacting" + (compactor == session.backend ? "" : " with " + compactor));
        // Compacts the PRIOR history only: folding the message the user just
        // typed into a summary of the conversation so far would summarise away
        // the question being asked.
        {
            const agentloop::SideCallScope said{side, "utility",
                                                "compacting the conversation with " + compactor};
            session.messages = agentloop::compact_history(harness, session.messages, compactor);
        }
        ++session.compactions;
        if (attached != nullptr) {
            attached->after_compaction();
        }
    } else if (usage.should_warn()) {
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) + "% full" +
               (usage.exact ? "" : " (estimated)"));
    }

    for (const harness::ChatMessage& message : incoming) {
        session.messages.push_back(message);
    }

    agentloop::Options loop_options;
    loop_options.model = session.backend;
    loop_options.cancellation = cancellation;
    loop_options.temperature = session.params.temperature;
    loop_options.max_tokens = session.params.max_tokens;
    // Whether the model thinks first (26i): `/think` or the flag, else the
    // backend's; `auto` asks the utility model, when one is set.
    loop_options.thinking = resolve_thinking(
        session.params.thinking, session.params.thinking_budget, harness.config(), session.backend);
    loop_options.thinking_judge = named_utility(harness.config());
    loop_options.stream_answer = true;
    // A large tool result is summarised by the utility model, when one is
    // set, before the chat model reads it (26b).
    loop_options.summary_model = named_utility(harness.config());
    // The member calls a turn may make (27f), counted from zero each turn --
    // with tools or without, since the suite's validation (27g) spends from
    // the same count: a standing answer check needs no tool.
    loop_options.member_calls = member_calls;
    if (tools != nullptr) {
        loop_options.tools = tools;
        // The tools each step offers, when there are many (26g): one
        // selection for the conversation, so a turn can keep the last one's.
        loop_options.tool_selection = selection;
        loop_options.ask = ask;
        loop_options.permission = gate.permission;
        loop_options.confirm = gate.confirm;
    }

    // Retrieval runs PER TURN, against what the user just asked -- which is the
    // reason the injected chunks must stay out of history. Turn one's context
    // left lying in the transcript would still be competing for attention on
    // turn five, against the chunks that actually answer the new question.
    //
    // Which collection: the flag for the whole session, else whatever
    // `auto_rag` says RIGHT NOW. The config is re-read here rather than at
    // startup on purpose, and a config that has become unreadable mid-session
    // costs this turn its auto_rag and says so -- never the turn itself.
    RagChoice rag_choice;
    std::optional<harness::Config> turn_config;
    try {
        turn_config = harness::load_config(rag.config_path);
        // The chat's suite, not the file's default: retrieval's embedder and
        // judge resolve under the suite the chat is running (27d).
        turn_config->models.default_suite = harness.config().models.default_suite;
    } catch (const harness::ConfigError& e) {
        if (!rag.flag_given) {
            notice(std::string{"auto_rag skipped -- config unreadable: "} + e.what());
        }
    }
    if (rag.flag_given) {
        rag_choice = choose_rag_collection(true, rag.flag_value, {});
    } else if (turn_config.has_value()) {
        rag_choice = choose_rag_collection(false, {}, turn_config->auto_rag);
    }
    // A follow-up is searched as a standalone question, restated by the
    // utility model when one is set, else the chat's own (26b) -- once, for
    // the attachments and the collection alike. The chat model is still
    // asked the question as the user wrote it.
    std::string query = input;
    if (rag_choice.active() || (attached != nullptr && attached->retrieves())) {
        const std::string rewriter =
            helper_backend(harness.config(), harness::ModelRole::Utility, session.backend);
        std::optional<agentloop::SideCallScope> said;
        if (agentloop::has_earlier_turn(session.messages)) {
            said.emplace(side, "utility",
                         "rewriting the follow-up into a search query with " + rewriter);
        }
        const agentloop::QueryRewrite rewrite =
            agentloop::rewrite_query(harness, rewriter, session.messages, input, {});
        said.reset();
        if (rewrite.rewritten) {
            reporter.on_progress("search query by " + rewriter + ": " + rewrite.query);
        } else if (!rewrite.note.empty()) {
            notice(rewrite.note);
        }
        query = rewrite.query;
        loop_options.selection_query = query;
    } else if (selection != nullptr && selection->active()) {
        // The tools are ranked for a follow-up restated to stand alone --
        // by the utility model only when one is set: a request to the chat
        // model itself every turn would cost more than the ranking saves.
        if (const std::string utility = named_utility(harness.config());
            !utility.empty() && agentloop::has_earlier_turn(session.messages)) {
            std::optional<agentloop::SideCallScope> said;
            said.emplace(side, "utility",
                         "restating the follow-up to rank the tools, with " + utility);
            const agentloop::QueryRewrite rewrite =
                agentloop::rewrite_query(harness, utility, session.messages, input, {});
            said.reset();
            if (rewrite.rewritten) {
                reporter.on_progress("tools ranked for, by " + utility + ": " + rewrite.query);
                loop_options.selection_query = rewrite.query;
            }
        }
    }
    const agentloop::TurnBudget budget =
        agentloop::turn_budget(harness, session.backend, session.params.max_tokens);

    // The chat's attachments first (26d): an inlined one rides the message
    // just sent, and the others' excerpts are retrieved -- ahead of a
    // collection's, which gets what they leave of the share.
    std::int64_t share_used = 0;
    if (attached != nullptr) {
        ChatAttachments::Turn turn =
            attached->for_turn(session.messages.size() - 1, query, budget, rag.limit, {}, side);
        loop_options.inline_attachments = std::move(turn.inlined);
        if (turn.retrieved.has_value()) {
            if (turn.retrieved->error.empty() && !turn.retrieved->prefix.empty()) {
                loop_options.transient_prefix = turn.retrieved->prefix;
                share_used = turn.retrieved->tokens;
            }
            notice(describe_attachment_retrieval(*turn.retrieved));
        }
    }
    if (rag_choice.active()) {
        // The session's retriever and rerank SETTINGS, read each turn so
        // /retriever and /rerank take effect on the next question and a
        // resumed session continues as it was last set.
        const harness::Config fallback;
        const harness::Config& config = turn_config.has_value() ? *turn_config : fallback;
        const agentloop::RagResult retrieved = retrieve_for_collection(
            harness, config, rag_choice.collection, query, rag.limit, session.retriever,
            session.rerank, {}, session.backend, budget, share_used, side);
        if (retrieved.error.empty() && !retrieved.prefix.empty()) {
            loop_options.transient_prefix.insert(loop_options.transient_prefix.end(),
                                                 retrieved.prefix.begin(), retrieved.prefix.end());
            share_used += retrieved.tokens;
        }
        notice(describe_retrieval(rag_choice, retrieved));
    }
    // What earlier chats established, within what the share has left (26l):
    // transient like the rest, and said, so the user can see why the model
    // knows it.
    if (recall != nullptr && recall->active()) {
        const Recalled recalled =
            recall->for_turn(query, budget, share_used,
                             rag_choice.active() ? rag_choice.collection : std::string{}, side);
        if (!recalled.prefix.empty()) {
            loop_options.transient_prefix.insert(loop_options.transient_prefix.end(),
                                                 recalled.prefix.begin(), recalled.prefix.end());
            reporter.on_recall(recalled.chats, recalled.decisions);
        }
    }
    if (!review_note.empty()) {
        // The review note rides the transient prefix, never the transcript:
        // `/branch` can change it between turns, and a resumed session gets
        // whatever ITS flags say rather than a stale note from last time.
        loop_options.transient_prefix.insert(loop_options.transient_prefix.begin(),
                                             harness::ChatMessage::system(review_note));
    }

    try {
        agentloop::RunResult result =
            agentloop::run(harness, session.messages, loop_options, reporter);
        ++session.turns;
        if (recall != nullptr) {
            recall->after_turn();
        }
        if (attached != nullptr) {
            attached->after_turn(result.inline_dropped);
        }
        if (result.hit_iteration_limit) {
            notice("tool-call limit reached");
        }
        outcome.completed = true;
        outcome.run = std::move(result);
    } catch (const harness::CancelledError&) {
        notice("cancelled");
        outcome.cancelled = true;
    } catch (const harness::HarnessError& e) {
        logger::log(logger::Level::Error, "chat", e.what());
        notice(e.what());
        outcome.error = e.what();
    }

    // Persist after EVERY turn. A kill -9 mid-conversation must leave every
    // completed turn on disk, and that is a property of writing here rather
    // than at exit.
    logger::save(session);
    return outcome;
}

}  // namespace apogee::commands
