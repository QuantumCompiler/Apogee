#include "cli/chat_attachments.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iterator>
#include <system_error>
#include <utility>

#include "agentloop/media.h"
#include "agentloop/retriever.h"
#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "harness/roles.h"
#include "operations/graph_sources.h"

namespace apogee::commands {
namespace {

/// `bytes` for a person: `812 bytes`, `4.2 KB`, `3.1 MB`.
[[nodiscard]] std::string size_of(std::uint64_t bytes) {
    std::array<char, 32> out{};
    if (bytes < 1024) {
        std::snprintf(out.data(), out.size(), "%llu bytes", static_cast<unsigned long long>(bytes));
    } else if (bytes < std::uint64_t{1024} * 1024) {
        std::snprintf(out.data(), out.size(), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(out.data(), out.size(), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return out.data();
}

[[nodiscard]] std::string files_of(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " file" : " files");
}

/// What a trim names media sent as it is by, so a dropped one is told apart
/// from the same attachment's text.
constexpr std::string_view kAsItIs = " (as it is)";

/// What ends a sentence, and so may follow an `@` mention without being
/// part of its name.
constexpr std::string_view kSentenceEnds = ".,;:!?)]}'\"";

/// What a trim names an attachment's map card by (26q).
constexpr std::string_view kMapCard = " (map)";

/// How the files of one attachment were read, when a model read them:
/// "described by X", "transcribed by X", "a timeline". Empty for text.
[[nodiscard]] std::string reading_of(const std::vector<logger::AttachedFile>& files) {
    std::vector<std::string> phrases;
    for (const logger::AttachedFile& file : files) {
        std::string phrase;
        if (file.reader.starts_with("vision: ")) {
            phrase = "described by " + file.reader.substr(8);
        } else if (file.reader.starts_with("transcription: ")) {
            phrase = "transcribed by " + file.reader.substr(15);
        } else if (file.reader.starts_with("timeline: ")) {
            phrase = "made a timeline by " + file.reader.substr(10);
        }
        if (!phrase.empty() && std::ranges::find(phrases, phrase) == phrases.end()) {
            phrases.push_back(std::move(phrase));
        }
    }
    std::string out;
    for (const std::string& phrase : phrases) {
        out += (out.empty() ? "" : ", ") + phrase;
    }
    return out;
}

/// A graphed folder's end of its `/attachments` line (27n): its part of the
/// graph counted from the index as it stands, or the absence said.
[[nodiscard]] std::string graph_suffix(const logger::AttachmentGraph& graph,
                                       const std::optional<embedstore::Store>& store) {
    embedstore::CodeMemberCounts counts;
    if (store.has_value() && !graph.label.empty()) {
        counts = store->code_member_counts(graph.label);
    }
    std::string out = "; ";
    out += attachment_graph_line(graph, counts.nodes, counts.edges);
    return out;
}

/// The folder a chat index lives in, private like the sessions.
void ensure_private(const std::filesystem::path& folder) {
    std::error_code code;
    std::filesystem::create_directories(folder, code);
    if (harness::supports_private_modes()) {
        std::filesystem::permissions(folder, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, code);
    }
}

}  // namespace

ChatAttachments::ChatAttachments(const harness::Harness& harness, logger::Session& session,
                                 std::filesystem::path store_path, Hooks hooks)
    : harness_{harness},
      session_{session},
      store_path_{std::move(store_path)},
      hooks_{std::move(hooks)} {
    // Vectors only from an embedding model the user named -- never the chat
    // model standing in for one -- and never one billed per call: nothing is
    // vectorised through a metered embedder on Apogee's initiative.
    const harness::Resolution role = harness::resolve_backend(
        harness_.config(), harness::RoleRequest{.role = harness::ModelRole::Embedding});
    if (!harness::is_named(role.from)) {
        lexical_reason_ = "no embedding model is set ('apogee config set-default-embedding')";
        return;
    }
    std::string reason;
    embedder_ = agentloop::resolve_embedder(harness_, harness_.config(), {}, reason);
    if (!embedder_.has_value()) {
        lexical_reason_ = reason;
    } else if (embedder_->metered) {
        lexical_reason_ = "the embedding model, " + embedder_->backend +
                          ", is billed per call, and attachments are indexed without asking";
        embedder_.reset();
    }
}

ChatAttachments::~ChatAttachments() {
    cancellation_.cancel();
    if (worker_.valid()) {
        worker_.wait();
    }
}

std::filesystem::path ChatAttachments::index_for(std::string_view chat_id) {
    return harness::attachments_dir() / (std::string{chat_id} + ".db");
}

void ChatAttachments::remove_index(std::string_view chat_id) {
    const std::filesystem::path index = index_for(chat_id);
    std::error_code code;
    for (const std::string_view suffix : {"", "-wal", "-shm", "-journal"}) {
        std::filesystem::remove(index.string() + std::string{suffix}, code);
    }
    std::filesystem::remove_all(harness::attachments_dir() / (std::string{chat_id} + ".media"),
                                code);
}

std::filesystem::path ChatAttachments::scratch() const {
    // Beside the index, in the private attachments folder, and removed once
    // each file is read: a video's frames are the user's as much as it is.
    return store_path_.parent_path() / (store_path_.stem().string() + ".media");
}

bool ChatAttachments::readable(std::string_view spec, std::vector<agentloop::FoundFile>& files,
                               const std::string& chat) {
    std::size_t billed = 0;
    std::string biller;
    std::erase_if(files, [&](const agentloop::FoundFile& file) {
        const std::optional<harness::Medium> medium = agentloop::medium_of(file.path);
        if (!medium.has_value()) {
            return false;
        }
        if (const std::string refusal = attachment_refusal(harness_, chat, *medium);
            !refusal.empty()) {
            hooks_.say(file.name + ": " + refusal, true);
            return true;
        }
        const agentloop::MediaReaders readers = agentloop::media_readers(harness_, chat, *medium);
        if (*medium != harness::Medium::Audio && !readers.describer.empty() &&
            harness_.generation_is_metered(readers.describer)) {
            biller = readers.describer;
            if (*medium == harness::Medium::Image) {
                ++billed;
            } else {
                const double seconds = agentloop::media_duration(file.path, cancellation_);
                billed += std::min(agentloop::kMaxTimelineFrames,
                                   static_cast<std::size_t>(seconds / agentloop::kFrameEvery) + 1);
            }
        }
        return false;
    });
    if (files.empty()) {
        return false;
    }
    if (billed > kMeteredDescriptionsAsked) {
        const std::string what = "about " + std::to_string(billed) +
                                 " image and frame descriptions by " + biller +
                                 ", which is billed per call";
        if (!hooks_.confirm_large ||
            !hooks_.confirm_large(std::string{spec} + " needs " + what + " -- go ahead?")) {
            hooks_.say(std::string{spec} + " not attached: it needs " + what +
                           " -- set a local vision model with 'apogee config set-default-vision'",
                       true);
            return false;
        }
    }
    return true;
}

agentloop::AttachmentText ChatAttachments::read_media(const agentloop::FoundFile& file,
                                                      harness::Medium medium,
                                                      const std::string& chat,
                                                      const harness::CancellationToken& token) {
    agentloop::MediaJob job;
    job.harness = &harness_;
    job.readers = agentloop::media_readers(harness_, chat, medium);
    job.scratch = scratch();
    job.cancellation = token;
    // Which model is reading what, and for how long (26e).
    const auto began = std::chrono::steady_clock::now();
    job.status = [this, began](const std::string& line) {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        const std::scoped_lock lock{mutex_};
        status_ = line + " (" + agentloop::clock_time(elapsed) + ")";
    };
    agentloop::MediaText read = agentloop::read_media(file.path, medium, job);
    return agentloop::AttachmentText{.text = std::move(read.text),
                                     .reader = std::move(read.reader),
                                     .reason = std::move(read.reason),
                                     .notes = std::move(read.notes)};
}

std::vector<harness::ContentPart> ChatAttachments::native_media(
    const agentloop::FoundFile& file, const std::string& chat,
    const harness::CancellationToken& token, std::vector<std::string>& notes) {
    const std::optional<harness::Medium> medium = agentloop::medium_of(file.path);
    if (!medium.has_value() || chat.empty() || !harness_.can_read(chat, *medium)) {
        return {};
    }
    if (*medium != harness::Medium::Image) {
        const double seconds = agentloop::media_duration(file.path, token);
        if (seconds > agentloop::kNativeSeconds) {
            notes.push_back(file.name + " runs " + agentloop::clock_time(seconds) +
                            ", over a minute, so the chat model reads its " +
                            (*medium == harness::Medium::Video ? "timeline" : "transcript") +
                            " rather than the " +
                            (*medium == harness::Medium::Video ? "clip" : "recording") + " itself");
            return {};
        }
    }
    {
        const std::scoped_lock lock{mutex_};
        status_ = "preparing " + file.name + " for " + chat + " to read as it is";
    }
    std::string error;
    const bool with_sound = *medium == harness::Medium::Video && harness_.accepts_audio(chat);
    std::vector<harness::ContentPart> parts = agentloop::native_parts(
        file.path, *medium, harness_.audio_sample_rate(chat), with_sound, scratch(), token, error);
    if (parts.empty() && !error.empty() && !token.stop_requested()) {
        notes.push_back(file.name + ": the chat model reads its text only -- it could not be " +
                        "prepared to read as it is (" + error + ")");
    }
    return parts;
}

bool ChatAttachments::attach(std::string_view spec,
                             const std::filesystem::path& working_directory) {
    agentloop::FoundFiles found = agentloop::find_attachment_files(spec, working_directory);
    if (!found.error.empty()) {
        hooks_.say(found.error, true);
        return false;
    }
    for (const std::string& skip : found.skips) {
        hooks_.say(skip, true);
    }
    // What the walk found, before media nothing reads is left out: the graph
    // build is handed the same list a `graph build --source` of the folder
    // would walk (27n), and counts what it cannot parse.
    const std::vector<agentloop::FoundFile> walked = found.files;
    const std::string chat = session_.backend;
    if (!readable(spec, found.files, chat)) {
        return false;
    }
    found.bytes = 0;
    for (const agentloop::FoundFile& file : found.files) {
        found.bytes += file.bytes;
    }
    if (found.large()) {
        const std::string what = files_of(found.files.size()) + " (" + size_of(found.bytes) +
                                 ") from " + std::string{spec};
        if (!hooks_.confirm_large || !hooks_.confirm_large("attach " + what + "?")) {
            hooks_.say(std::string{spec} + " is over " +
                           std::to_string(agentloop::kLargeAttachmentFiles) + " files or " +
                           size_of(agentloop::kLargeAttachmentBytes) +
                           " -- not attached; attach a narrower folder, or a glob",
                       true);
            return false;
        }
    }
    const std::string summary = "attaching " + std::string{spec} + " (" +
                                files_of(found.files.size()) + ", " + size_of(found.bytes) + ")";
    std::optional<AttachmentGraphJob> graph = graph_job(spec, working_directory, walked);
    {
        const std::scoped_lock lock{mutex_};
        queue_.push_back(Queued{.name = std::string{spec},
                                .files = std::move(found.files),
                                .chat = chat,
                                .graph = std::move(graph)});
    }
    hooks_.say(summary, false);
    start_worker();
    return true;
}

std::optional<std::string> ChatAttachments::graph_label_of(const std::string& name) const {
    // A recorded folder's -- a resumed chat's among them: re-attached, only
    // what changed is parsed; detached, its part is the one forgotten.
    const auto it = std::ranges::find(session_.attachments, name, &logger::Attachment::name);
    if (it != session_.attachments.end() && it->graph.has_value() && !it->graph->label.empty()) {
        return it->graph->label;
    }
    return std::nullopt;
}

std::set<std::string> ChatAttachments::graph_members_but(const std::string& name) const {
    std::set<std::string> out;
    for (const logger::Attachment& attachment : session_.attachments) {
        if (attachment.name != name && attachment.graph.has_value() &&
            !attachment.graph->label.empty()) {
            out.insert(attachment.graph->label);
        }
    }
    for (const auto& [owner, label] : graph_in_flight_) {
        if (owner != name) {
            out.insert(label);
        }
    }
    return out;
}

std::optional<AttachmentGraphJob> ChatAttachments::graph_job(
    std::string_view spec, const std::filesystem::path& working_directory,
    const std::vector<agentloop::FoundFile>& files) {
    const std::string name{spec};
    const std::optional<std::string> prior = graph_label_of(name);
    AttachmentGraphJob job;
    job.name = name;
    job.keep = graph_members_but(name);
    // A folder -- never one file, never a glob (no folder is named by one) --
    // whose files include a language a vendored grammar parses.
    job.build = false;
    if (hooks_.code_graph) {
        std::filesystem::path root{std::string{spec}};
        if (root.is_relative()) {
            root = working_directory / root;
        }
        root = root.lexically_normal();
        std::error_code code;
        if (std::filesystem::is_directory(root, code)) {
            job.files = source_files_under(files, root);
            job.build = offers_code(job.files);
            job.root = std::move(root);
        }
    }
    if (!job.build && !prior.has_value()) {
        return std::nullopt;  // nothing to build, and nothing built before
    }
    if (prior.has_value()) {
        job.label = *prior;  // re-attached: the same member, updated
    } else {
        // The folder's name, as `graph build --source` labels the same tree;
        // two folders of one name in one chat are told apart by a number.
        const std::string base = source_member_label(job.root.generic_string());
        job.label = base;
        for (int n = 2; job.keep.contains(job.label); ++n) {
            job.label = base + "-" + std::to_string(n);
        }
    }
    if (job.build) {
        graph_in_flight_[name] = job.label;
    }
    return job;
}

AttachmentGraphOutcome ChatAttachments::graph_pass(const AttachmentGraphJob& job,
                                                   const harness::CancellationToken& token) {
    // Whatever is not built here -- cancelled, failed, no longer asked for --
    // leaves its member to the settle that records it, which forgets it
    // (`reconcile_graph`): an absent graph, never a stale or half-built one
    // trusted as this attachment's. A cancel that is the process ending
    // reaches no settle, so the graph stays as the saved session knows it.
    AttachmentGraphOutcome out;
    if (!job.build) {
        out.state = AttachmentGraphOutcome::State::Forgotten;
        return out;
    }
    {
        const std::scoped_lock lock{mutex_};
        status_ = "building the code graph of " + job.name;
    }
    // Cancelled already -- Ctrl-C while the chunks were indexed -- the build
    // stops before its first file and says so.
    return build_attachment_graph(
        store_path_, job, token, [this, &job](const graph::SourceProgress& progress) {
            const std::scoped_lock lock{mutex_};
            status_ = "building the code graph of " + job.name + ": " + progress.file + " (" +
                      std::to_string(progress.index) + " of " + std::to_string(progress.count) +
                      ")";
        });
}

void ChatAttachments::start_worker() {
    if (worker_.valid()) {
        if (worker_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            return;  // running: it drains the queue, this item included
        }
        worker_.get();
    }
    ensure_private(store_path_.parent_path());
    worker_ = std::async(std::launch::async, [this, token = cancellation_] {
        for (;;) {
            Queued item;
            {
                const std::scoped_lock lock{mutex_};
                if (queue_.empty()) {
                    status_.clear();
                    return;
                }
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            const std::string chat = item.chat;
            agentloop::AttachmentIndex index{
                store_path_, harness::attachments_dir(), embedder_,
                [this, &chat](const agentloop::FoundFile& file, harness::Medium medium,
                              const harness::CancellationToken& cancellation) {
                    return read_media(file, medium, chat, cancellation);
                }};
            Indexed indexed{.name = item.name, .added = {}, .native = {}, .graph = {}};
            for (std::size_t at = 0; at < item.files.size(); ++at) {
                const agentloop::FoundFile& file = item.files[at];
                if (token.stop_requested()) {
                    indexed.added.push_back({.skip = file.name + ": cancelled"});
                    continue;
                }
                const std::string counter = item.files.size() > 1
                                                ? " (" + std::to_string(at + 1) + " of " +
                                                      std::to_string(item.files.size()) + ")"
                                                : "";
                {
                    const std::scoped_lock lock{mutex_};
                    status_ = "indexing " + file.name + counter;
                }
                try {
                    indexed.added.push_back(
                        index.add(file, token, [&](std::size_t done, std::size_t total) {
                            const std::scoped_lock lock{mutex_};
                            status_ = "embedding " + file.name + counter + ": " +
                                      std::to_string(done) + " of " + std::to_string(total) +
                                      " chunks";
                        }));
                } catch (const std::exception& e) {
                    indexed.added.push_back({.skip = file.name + ": " + e.what()});
                }
                // What the chat model reads as it is, beside what it was read
                // into: attaching an image again looks at it again (26e).
                agentloop::AttachmentIndex::Added& added = indexed.added.back();
                if (added.file.has_value() && !token.stop_requested()) {
                    std::vector<std::string> notes;
                    std::vector<harness::ContentPart> parts =
                        native_media(file, chat, token, notes);
                    std::ranges::move(parts, std::back_inserter(indexed.native));
                    for (const std::string& note : notes) {
                        added.note += (added.note.empty() ? "" : "\n") + note;
                    }
                }
            }
            // The folder's code graph (27n), after its chunks and from the
            // files the attach found -- when this attach replaces anything:
            // with nothing of it indexed, the earlier attachment stands, and
            // its graph with it.
            const bool replaces = std::ranges::any_of(
                indexed.added, [](const agentloop::AttachmentIndex::Added& added) {
                    return added.file.has_value();
                });
            if (item.graph.has_value() && replaces) {
                indexed.graph = graph_pass(*item.graph, token);
            }
            const std::scoped_lock lock{mutex_};
            done_.push_back(std::move(indexed));
        }
    });
}

void ChatAttachments::settle() {
    if (worker_.valid()) {
        if (worker_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            // A turn waits on it, saying how far it has got; Ctrl-C keeps
            // what is ready rather than losing everything.
            const InterruptScope interrupt;
            std::string shown;
            while (worker_.wait_for(std::chrono::milliseconds{200}) != std::future_status::ready) {
                if (InterruptScope::token().stop_requested()) {
                    cancellation_.cancel();
                }
                std::string line;
                {
                    const std::scoped_lock lock{mutex_};
                    line = status_;
                }
                // Only a change: on a pipe each one is a line of its own.
                if (hooks_.progress && !line.empty() && line != shown) {
                    hooks_.progress(line);
                    shown = line;
                }
            }
            if (hooks_.progress) {
                hooks_.progress({});
            }
        }
        worker_.get();
        if (cancellation_.stop_requested()) {
            cancellation_ = harness::CancellationToken::create();
        }
    }
    std::vector<Indexed> finished;
    {
        const std::scoped_lock lock{mutex_};
        finished.swap(done_);
    }
    // Everything queued is done and about to be recorded: the session holds
    // each graphed folder's label from here (27n).
    graph_in_flight_.clear();
    if (finished.empty()) {
        return;
    }
    const agentloop::TurnBudget budget =
        agentloop::turn_budget(harness_, session_.backend, session_.params.max_tokens);
    for (Indexed& indexed : finished) {
        record(std::move(indexed), budget);
    }
    reconcile_graph();
    save();
}

void ChatAttachments::reconcile_graph() {
    std::error_code code;
    if (!std::filesystem::exists(store_path_, code)) {
        return;
    }
    // The index's code graph holds exactly the recorded folders' parts
    // (27n): a member none of them owns -- a folder detached, or recorded
    // with its graph cancelled, failed or no longer asked for; one attached
    // twice before a settle, the second time with no code; a build a crash
    // cut short -- is forgotten, the rest re-linked. Nothing to forget,
    // nothing done.
    try {
        forget_attachment_graph(store_path_, {}, graph_members_but({}));
    } catch (const std::exception& e) {
        hooks_.say(std::string{"the chat's code graph could not be checked: "} + e.what(), true);
    }
}

ChatAttachments::GraphSaid ChatAttachments::take_graph(const Indexed& indexed) {
    GraphSaid out;
    if (!indexed.graph.has_value()) {
        return out;  // no pass: no folder of code, now or before
    }
    const AttachmentGraphOutcome& outcome = *indexed.graph;
    switch (outcome.state) {
        case AttachmentGraphOutcome::State::Built:
            out.record = outcome.record;
            out.line = attachment_graph_line(outcome.record, outcome.nodes, outcome.edges);
            break;
        case AttachmentGraphOutcome::State::Cancelled:
        case AttachmentGraphOutcome::State::Failed:
            // Absent, and said -- never half-trusted.
            out.record = outcome.record;
            out.line = attachment_graph_line(outcome.record, 0, 0);
            out.warning = true;
            break;
        case AttachmentGraphOutcome::State::Forgotten:
            break;
    }
    return out;
}

void ChatAttachments::record(Indexed indexed, const agentloop::TurnBudget& budget) {
    std::vector<logger::AttachedFile> files;
    std::int64_t chunks = 0;
    std::size_t copied = 0;
    bool vectorised = false;
    for (agentloop::AttachmentIndex::Added& added : indexed.added) {
        if (!added.skip.empty()) {
            hooks_.say(added.skip, true);
        }
        if (!added.note.empty()) {
            hooks_.say(added.note, true);
        }
        if (added.file.has_value()) {
            files.push_back(std::move(*added.file));
            chunks += added.chunks;
            copied += added.copied ? 1 : 0;
            vectorised = vectorised || added.vectorised;
        }
    }
    if (files.empty()) {
        hooks_.say("nothing attached from " + indexed.name, true);
        return;  // the earlier attachment of this name, if any, stands with its graph
    }
    GraphSaid graph = take_graph(indexed);
    // Attached again under the same name: the new one replaces it.
    std::erase_if(session_.attachments, [&](const logger::Attachment& attachment) {
        return attachment.name == indexed.name;
    });
    pending_inline_.erase(indexed.name);
    inline_texts_.erase(indexed.name);
    inline_costs_.erase(indexed.name);
    pending_map_.erase(indexed.name);
    map_texts_.erase(indexed.name);
    map_costs_.erase(indexed.name);

    session_.attachments.push_back(logger::Attachment{.name = indexed.name,
                                                      .files = std::move(files),
                                                      .inline_at = {},
                                                      .map_at = {},
                                                      .graph = std::move(graph.record)});
    const logger::Attachment& attachment = session_.attachments.back();
    std::int64_t in_use = inline_tokens_in_use(budget);
    // A folder's or a glob's map first (26q): a few lines, and what keeps a
    // model from inventing paths -- inside the attachment share, never past
    // it, so where nothing is inlined nothing is mapped either.
    bool mapped = false;
    const bool mappable = !map_text(attachment).empty();
    if (mappable) {
        const std::int64_t card = map_tokens(budget, attachment);
        mapped = agentloop::fits_inline(budget, in_use, card);
        if (mapped) {
            pending_map_.insert(attachment.name);
            in_use += card;
        }
    }
    const std::string text = inline_text(attachment);
    harness::ChatRequest request;
    request.messages.push_back(harness::ChatMessage::user(text));
    const std::int64_t tokens = budget.tokens(request).tokens;
    const bool inlined = agentloop::fits_inline(budget, in_use, tokens);
    if (inlined) {
        pending_inline_.insert(attachment.name);
        inline_costs_[attachment.name] = tokens;
    }
    const bool native = !indexed.native.empty();
    pending_native_.erase(attachment.name);
    if (native) {
        pending_native_[attachment.name] = std::move(indexed.native);
    }

    std::string line = "attached " + attachment.name + ": " + files_of(attachment.files.size()) +
                       ", " + std::to_string(chunks) + (chunks == 1 ? " chunk" : " chunks");
    if (mapped) {
        line += ", with a map of its folders";
    } else if (mappable) {
        line += budget.budget.known() ? ", no map: it does not fit the attachment share"
                                      : ", no map: the model's window is unknown";
    }
    if (const std::string reading = reading_of(attachment.files); !reading.empty()) {
        line += ", " + reading;
    }
    if (copied > 0) {
        line += copied == attachment.files.size()
                    ? ", from another chat's index"
                    : ", " + std::to_string(copied) + " from another chat's index";
    }
    if (!vectorised) {
        line += ", searched by its words" +
                (lexical_reason_.empty() ? std::string{} : ": " + lexical_reason_);
    }
    line += native ? " -- read as it is with your next message, then " : " -- ";
    line += inlined ? "inlined whole" : "its excerpts are retrieved each turn";
    hooks_.say(line, false);
    if (!graph.line.empty()) {
        hooks_.say(graph.line, graph.warning);
    }
}

std::string ChatAttachments::inline_text(const logger::Attachment& attachment) {
    if (const auto it = inline_texts_.find(attachment.name); it != inline_texts_.end()) {
        return it->second;
    }
    const agentloop::AttachmentIndex index{store_path_, {}, std::nullopt};
    std::string text;
    for (const logger::AttachedFile& file : attachment.files) {
        text += agentloop::render_inline_attachment(file.name, index.text_of(file.sha256));
    }
    inline_texts_[attachment.name] = text;
    return text;
}

std::string ChatAttachments::map_text(const logger::Attachment& attachment) {
    if (attachment.files.size() < 2) {
        return {};
    }
    if (const auto it = map_texts_.find(attachment.name); it != map_texts_.end()) {
        return it->second;
    }
    std::vector<std::string> names;
    std::vector<std::string> contents;
    for (const logger::AttachedFile& file : attachment.files) {
        names.push_back(file.name);
        contents.push_back(file.sha256);
    }
    const agentloop::AttachmentIndex index{store_path_, {}, std::nullopt};
    std::string card =
        agentloop::render_map_card(attachment.name, names, index.chunks_of(contents));
    map_texts_[attachment.name] = card;
    return card;
}

std::int64_t ChatAttachments::map_tokens(const agentloop::TurnBudget& budget,
                                         const logger::Attachment& attachment) {
    if (const auto it = map_costs_.find(attachment.name); it != map_costs_.end()) {
        return it->second;
    }
    harness::ChatRequest request;
    request.messages.push_back(harness::ChatMessage::user(map_text(attachment)));
    const std::int64_t tokens = budget.tokens(request).tokens;
    map_costs_[attachment.name] = tokens;
    return tokens;
}

std::int64_t ChatAttachments::inline_tokens_in_use(const agentloop::TurnBudget& budget) {
    std::int64_t total = 0;
    for (const logger::Attachment& attachment : session_.attachments) {
        // A map rides the same share as the text (26q).
        if (attachment.map_at.has_value() || pending_map_.contains(attachment.name)) {
            total += map_tokens(budget, attachment);
        }
        if (!attachment.inline_at.has_value() && !pending_inline_.contains(attachment.name)) {
            continue;
        }
        if (const auto it = inline_costs_.find(attachment.name); it != inline_costs_.end()) {
            total += it->second;
            continue;
        }
        harness::ChatRequest request;
        request.messages.push_back(harness::ChatMessage::user(inline_text(attachment)));
        const std::int64_t tokens = budget.tokens(request).tokens;
        inline_costs_[attachment.name] = tokens;
        total += tokens;
    }
    return total;
}

bool ChatAttachments::detach(std::string_view name) {
    const auto it = std::ranges::find(session_.attachments, name, &logger::Attachment::name);
    if (it == session_.attachments.end()) {
        return false;
    }
    std::set<std::string> removed;
    for (const logger::AttachedFile& file : it->files) {
        removed.insert(file.sha256);
    }
    session_.attachments.erase(it);
    // Its part of the code graph goes with it (27n), the rest re-linked.
    reconcile_graph();
    for (const logger::Attachment& other : session_.attachments) {
        for (const logger::AttachedFile& file : other.files) {
            removed.erase(file.sha256);  // still attached under another name
        }
    }
    agentloop::AttachmentIndex index{store_path_, {}, std::nullopt};
    for (const std::string& sha256 : removed) {
        index.remove(sha256);
    }
    const std::string key{name};
    pending_inline_.erase(key);
    pending_native_.erase(key);
    inline_texts_.erase(key);
    inline_costs_.erase(key);
    pending_map_.erase(key);
    map_texts_.erase(key);
    map_costs_.erase(key);
    save();
    return true;
}

std::vector<std::string> ChatAttachments::describe() const {
    std::vector<std::string> lines;
    // A graphed folder's line ends with its graph's (27n), counted from the
    // index as it stands -- another folder's code may have linked into it.
    std::optional<embedstore::Store> store;
    if (std::ranges::any_of(session_.attachments, [](const logger::Attachment& attachment) {
            return attachment.graph.has_value() && !attachment.graph->label.empty();
        })) {
        try {
            store.emplace(store_path_);
        } catch (const std::exception&) {
            store.reset();
        }
    }
    for (const logger::Attachment& attachment : session_.attachments) {
        std::uint64_t bytes = 0;
        for (const logger::AttachedFile& file : attachment.files) {
            bytes += file.bytes;
        }
        std::string state = "retrieved each turn";
        if (attachment.inline_at.has_value()) {
            state = "inlined";
        } else if (pending_inline_.contains(attachment.name)) {
            state = "inlined with your next message";
        }
        if (pending_native_.contains(attachment.name)) {
            state = "read as it is with your next message, then " + state;
        }
        if (attachment.map_at.has_value() || pending_map_.contains(attachment.name)) {
            state += ", with a map of its folders";
        }
        const std::string reading = reading_of(attachment.files);
        std::string graph;
        if (const std::optional<logger::AttachmentGraph>& part = attachment.graph;
            part.has_value()) {
            graph = graph_suffix(*part, store);
        }
        lines.push_back(attachment.name + " -- " + files_of(attachment.files.size()) + ", " +
                        size_of(bytes) + (reading.empty() ? "" : ", " + reading) + ", " + state +
                        graph);
    }
    return lines;
}

std::vector<std::string> ChatAttachments::names() const {
    std::vector<std::string> out;
    for (const logger::Attachment& attachment : session_.attachments) {
        out.push_back(attachment.name);
    }
    return out;
}

bool ChatAttachments::retrieves() const {
    return std::ranges::any_of(session_.attachments, [&](const logger::Attachment& attachment) {
        return !attachment.inline_at.has_value() && !pending_inline_.contains(attachment.name);
    });
}

std::optional<AttachmentGraphScope> ChatAttachments::graph_scope() const {
    AttachmentGraphScope scope;
    scope.store = store_path_;
    // The session's graphed folders: after every settle and detach the
    // index's code graph is exactly theirs (27n's reconcile).
    for (const logger::Attachment& attachment : session_.attachments) {
        if (attachment.graph.has_value() && !attachment.graph->label.empty()) {
            scope.folders.push_back(
                GraphedFolder{.name = attachment.name, .label = attachment.graph->label});
        }
    }
    if (scope.folders.empty()) {
        return std::nullopt;
    }
    // And the index holds it: a folder whose files parsed to nothing has no
    // graph to walk. Opening a store creates one, so a missing index is
    // asked about first.
    std::error_code code;
    if (!std::filesystem::exists(store_path_, code)) {
        return std::nullopt;
    }
    try {
        if (!embedstore::Store{store_path_}.has_graph()) {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return scope;
}

ChatAttachments::Turn ChatAttachments::for_turn(std::size_t user_message, const std::string& query,
                                                const agentloop::TurnBudget& budget, int limit,
                                                const harness::CancellationToken& cancellation,
                                                const agentloop::SideCallSink& on_side_call) {
    Turn turn;
    bool anchored = false;
    for (logger::Attachment& attachment : session_.attachments) {
        if (pending_inline_.contains(attachment.name)) {
            attachment.inline_at = user_message;
            anchored = true;
        }
        if (pending_map_.contains(attachment.name)) {
            attachment.map_at = user_message;
            anchored = true;
        }
    }
    pending_inline_.clear();
    pending_map_.clear();

    std::set<std::string> inline_sources;
    const auto in_front = [&](const logger::Attachment& attachment) {
        for (const logger::AttachedFile& file : attachment.files) {
            inline_sources.insert(agentloop::attachment_source(file.sha256));
        }
    };
    for (const logger::Attachment& attachment : session_.attachments) {
        // Read as it is on this message (26e); from the next, its text
        // stands in -- a vision chat stops re-reading every image every turn.
        bool as_it_is = false;
        if (const auto native = pending_native_.find(attachment.name);
            native != pending_native_.end()) {
            turn.inlined.push_back(
                agentloop::InlineAttachment{.message = user_message,
                                            .name = attachment.name + std::string{kAsItIs},
                                            .text = {},
                                            .parts = std::move(native->second)});
            in_front(attachment);
            as_it_is = true;
        }
        if (attachment.inline_at.has_value() &&
            (!as_it_is || *attachment.inline_at != user_message)) {
            turn.inlined.push_back(agentloop::InlineAttachment{.message = *attachment.inline_at,
                                                               .name = attachment.name,
                                                               .text = inline_text(attachment)});
            in_front(attachment);
        }
        // Its map after its text, so the model reads the map first and a
        // trim takes the text before it (26q). The map holds no content, so
        // the files are searched as ever.
        if (attachment.map_at.has_value()) {
            turn.inlined.push_back(
                agentloop::InlineAttachment{.message = *attachment.map_at,
                                            .name = attachment.name + std::string{kMapCard},
                                            .text = map_text(attachment)});
        }
    }
    pending_native_.clear();
    if (retrieves()) {
        agentloop::RagTurn rag;
        rag.store_path = store_path_;
        // A question naming code is searched by its words for those names;
        // one that does not, by words and meaning together when the index
        // has vectors. `/retriever` overrides either.
        const std::vector<std::string> names = agentloop::code_names_in(query);
        std::string search = query;
        if (!names.empty()) {
            search.clear();
            for (const std::string& name : names) {
                search += (search.empty() ? "" : " ") + name;
            }
        }
        rag.question = search;
        rag.limit = limit;
        rag.retriever_flag = session_.retriever;
        if (!names.empty()) {
            rag.retriever_pin = std::string{agentloop::to_string(agentloop::Retriever::Lexical)};
        } else if (embedder_.has_value()) {
            rag.retriever_pin = std::string{agentloop::to_string(agentloop::Retriever::Hybrid)};
        }
        rag.rerank_flag = session_.rerank;
        rag.embedder = embedder_;
        rag.embedder_reason = lexical_reason_;
        rag.conversation = session_.backend;
        rag.on_side_call = on_side_call;
        rag.harness = &harness_;
        rag.config = &harness_.config();
        rag.cancellation = cancellation;
        // The name the chat's code graph's section carries, too (27o).
        rag.collection = std::string{kAttachmentGraphName};
        rag.budget = budget;
        rag.attachments = true;
        // The chat's code graph, when it holds one (27o): the excerpts seed
        // its expansion, under the graph knobs' defaults `RagTurn` holds --
        // decided from the chat's own state, never by `resolve_turn_graph`.
        rag.graph_enabled = graph_scope().has_value();
        rag.exclude_sources = std::move(inline_sources);
        rag.moments = agentloop::moments_in(query);
        turn.retrieved = agentloop::retrieve_for_turn(rag);
    }
    if (anchored) {
        save();
    }
    return turn;
}

void ChatAttachments::after_turn(const std::vector<std::string>& inline_dropped) {
    bool changed = false;
    for (const std::string& name : inline_dropped) {
        if (name.ends_with(kMapCard)) {
            const std::string owner = name.substr(0, name.size() - kMapCard.size());
            const auto it =
                std::ranges::find(session_.attachments, owner, &logger::Attachment::name);
            if (it != session_.attachments.end() && it->map_at.has_value()) {
                it->map_at.reset();
                changed = true;
                hooks_.say("the map of " + owner +
                               " no longer fits the conversation -- it is left out from now on",
                           true);
            }
            continue;
        }
        if (name.ends_with(kAsItIs)) {
            hooks_.say(name.substr(0, name.size() - kAsItIs.size()) +
                           " did not fit this request as it is, so the model did not see it -- "
                           "its text stands in from now on",
                       true);
            continue;
        }
        const auto it = std::ranges::find(session_.attachments, name, &logger::Attachment::name);
        if (it == session_.attachments.end() || !it->inline_at.has_value()) {
            continue;
        }
        it->inline_at.reset();
        changed = true;
        hooks_.say(name +
                       " no longer fits the conversation whole -- its excerpts are retrieved "
                       "from now on",
                   true);
    }
    if (changed) {
        save();
    }
}

void ChatAttachments::after_compaction() {
    bool changed = false;
    bool remapped = false;
    for (logger::Attachment& attachment : session_.attachments) {
        if (attachment.inline_at.has_value()) {
            attachment.inline_at.reset();
            changed = true;
        }
        // A map rides once more, on the next message (26q): what the
        // attachment looks like outlives the summary that replaced it.
        if (attachment.map_at.has_value()) {
            attachment.map_at.reset();
            pending_map_.insert(attachment.name);
            remapped = true;
        }
    }
    if (changed) {
        hooks_.say(
            "compaction folded the messages the attachments rode -- their excerpts are "
            "retrieved from now on",
            false);
    }
    if (remapped) {
        hooks_.say("the attachments' maps ride your next message again", false);
    }
    if (changed || remapped) {
        save();
    }
}

void ChatAttachments::save() const {
    if (hooks_.save) {
        logger::save(session_);
    }
}

std::vector<std::string> mentioned_paths(std::string_view message) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < message.size(); ++at) {
        if (message[at] != '@' ||
            (at > 0 && std::isspace(static_cast<unsigned char>(message[at - 1])) == 0)) {
            continue;  // an address, not a mention
        }
        std::size_t start = at + 1;
        std::string path;
        if (start < message.size() && message[start] == '"') {
            const std::size_t close = message.find('"', start + 1);
            path = std::string{message.substr(start + 1, close == std::string_view::npos
                                                             ? std::string_view::npos
                                                             : close - start - 1)};
            at = close == std::string_view::npos ? message.size() : close;
        } else {
            std::size_t end = start;
            while (end < message.size() &&
                   std::isspace(static_cast<unsigned char>(message[end])) == 0) {
                ++end;
            }
            path = std::string{message.substr(start, end - start)};
            at = end;
        }
        if (!path.empty()) {
            out.push_back(std::move(path));
        }
    }
    return out;
}

std::optional<std::string> existing_mention(std::string_view mention,
                                            const std::filesystem::path& working_directory) {
    const auto resolved = [&](const std::string& spelling) {
        const std::filesystem::path path{spelling};
        return path.is_relative() ? working_directory / path : path;
    };
    std::string candidate{mention};
    for (;;) {
        std::error_code code;
        if (!candidate.empty() && std::filesystem::exists(resolved(candidate), code)) {
            // Windows reads "report.pdf." as "report.pdf" -- a name there
            // never ends in a dot -- so a sentence's punctuation still names
            // the file. Where a shorter spelling is the same file, the rest
            // was the sentence's. A POSIX file truly named "report.pdf." is
            // a different file, and keeps its name.
            std::string shorter = candidate;
            while (!shorter.empty() &&
                   kSentenceEnds.find(shorter.back()) != std::string_view::npos) {
                shorter.pop_back();
                if (!shorter.empty() &&
                    std::filesystem::equivalent(resolved(candidate), resolved(shorter), code)) {
                    candidate = shorter;
                }
            }
            return candidate;
        }
        // "summarize @report.pdf." -- the full stop is the sentence's.
        if (candidate.empty() || kSentenceEnds.find(candidate.back()) == std::string_view::npos) {
            return std::nullopt;
        }
        candidate.pop_back();
    }
}

}  // namespace apogee::commands
