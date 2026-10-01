#include "commands/chat_attachments.h"

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
#include "commands/helpers.h"
#include "commands/interrupt.h"
#include "harness/layout.h"
#include "harness/roles.h"

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
    if (role.from != harness::ResolvedFrom::RolePointer) {
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
    {
        const std::scoped_lock lock{mutex_};
        queue_.push_back(
            Queued{.name = std::string{spec}, .files = std::move(found.files), .chat = chat});
    }
    hooks_.say(summary, false);
    start_worker();
    return true;
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
            Indexed indexed{.name = item.name, .added = {}, .native = {}};
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
    if (finished.empty()) {
        return;
    }
    const agentloop::TurnBudget budget =
        agentloop::turn_budget(harness_, session_.backend, session_.params.max_tokens);
    for (Indexed& indexed : finished) {
        record(std::move(indexed), budget);
    }
    save();
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
        return;
    }
    // Attached again under the same name: the new one replaces it.
    std::erase_if(session_.attachments, [&](const logger::Attachment& attachment) {
        return attachment.name == indexed.name;
    });
    pending_inline_.erase(indexed.name);
    inline_texts_.erase(indexed.name);
    inline_costs_.erase(indexed.name);

    session_.attachments.push_back(
        logger::Attachment{.name = indexed.name, .files = std::move(files), .inline_at = {}});
    const logger::Attachment& attachment = session_.attachments.back();
    const std::int64_t in_use = inline_tokens_in_use(budget);
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

std::int64_t ChatAttachments::inline_tokens_in_use(const agentloop::TurnBudget& budget) {
    std::int64_t total = 0;
    for (const logger::Attachment& attachment : session_.attachments) {
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
    save();
    return true;
}

std::vector<std::string> ChatAttachments::describe() const {
    std::vector<std::string> lines;
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
        const std::string reading = reading_of(attachment.files);
        lines.push_back(attachment.name + " -- " + files_of(attachment.files.size()) + ", " +
                        size_of(bytes) + (reading.empty() ? "" : ", " + reading) + ", " + state);
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

ChatAttachments::Turn ChatAttachments::for_turn(std::size_t user_message, const std::string& query,
                                                const agentloop::TurnBudget& budget, int limit,
                                                const harness::CancellationToken& cancellation) {
    Turn turn;
    bool anchored = false;
    for (logger::Attachment& attachment : session_.attachments) {
        if (pending_inline_.contains(attachment.name)) {
            attachment.inline_at = user_message;
            anchored = true;
        }
    }
    pending_inline_.clear();

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
        if (!attachment.inline_at.has_value() ||
            (as_it_is && *attachment.inline_at == user_message)) {
            continue;
        }
        turn.inlined.push_back(agentloop::InlineAttachment{.message = *attachment.inline_at,
                                                           .name = attachment.name,
                                                           .text = inline_text(attachment)});
        in_front(attachment);
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
        rag.harness = &harness_;
        rag.config = &harness_.config();
        rag.cancellation = cancellation;
        rag.collection = "attachments";
        rag.budget = budget;
        rag.attachments = true;
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
    for (logger::Attachment& attachment : session_.attachments) {
        if (attachment.inline_at.has_value()) {
            attachment.inline_at.reset();
            changed = true;
        }
    }
    if (changed) {
        hooks_.say(
            "compaction folded the messages the attachments rode -- their excerpts are "
            "retrieved from now on",
            false);
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
    std::string candidate{mention};
    for (;;) {
        std::filesystem::path path{candidate};
        if (path.is_relative()) {
            path = working_directory / path;
        }
        std::error_code code;
        if (!candidate.empty() && std::filesystem::exists(path, code)) {
            return candidate;
        }
        // "summarize @report.pdf." -- the full stop is the sentence's.
        if (candidate.empty() ||
            std::string_view{".,;:!?)]}'\""}.find(candidate.back()) == std::string_view::npos) {
            return std::nullopt;
        }
        candidate.pop_back();
    }
}

}  // namespace apogee::commands
