#include "mcp/transport.h"

#include <utility>

#include "backends/jsonl_framer.h"

namespace apogee::mcp {

// --- StderrTail -------------------------------------------------------------

StderrTail::StderrTail(Sink sink) : sink_{std::move(sink)} {}

void StderrTail::write(std::string_view bytes) {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        partial_ += bytes;
        std::size_t newline = partial_.find('\n');
        while (newline != std::string::npos) {
            std::string line = partial_.substr(0, newline);
            partial_.erase(0, newline + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            std::size_t start = 0;
            while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                ++start;
            }
            line.erase(0, start);
            if (!line.empty()) {
                lines_.push_back(std::move(line));
                if (lines_.size() > kStderrTailLines) {
                    lines_.pop_front();
                }
            }
            newline = partial_.find('\n');
        }
    }
    // Tee outside the lock: a slow sink must not block the reader.
    if (sink_) {
        sink_(bytes);
    }
}

std::string StderrTail::tail() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    std::string out;
    for (const std::string& line : lines_) {
        out += out.empty() ? "" : " | ";
        out += line;
    }
    std::string last = partial_;
    while (!last.empty() && (last.back() == '\r' || last.back() == ' ' || last.back() == '\n')) {
        last.pop_back();
    }
    if (!last.empty()) {
        out += out.empty() ? "" : " | ";
        out += last;
    }
    return out;
}

std::string stderr_note(const std::string& tail) {
    return tail.empty() ? std::string{} : " -- stderr: " + tail;
}

// --- StdioTransport ---------------------------------------------------------

StdioTransport::StdioTransport(std::unique_ptr<platform::ChildProcess> child, StderrTail::Sink sink)
    : child_{std::move(child)}, tail_{std::move(sink)} {}

StdioTransport::~StdioTransport() {
    close();
}

bool StdioTransport::send(const nlohmann::json& message) {
    const std::lock_guard<std::mutex> lock{write_mutex_};
    if (closed_ || child_ == nullptr) {
        return false;
    }
    return child_->write_stdin(message.dump() + "\n");
}

void StdioTransport::drain_stderr() {
    std::string piece;
    while (child_->read_stderr(piece, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        tail_.write(piece);
    }
}

std::optional<std::string> StdioTransport::recv(std::chrono::milliseconds timeout) {
    if (closed_ || child_ == nullptr) {
        return std::nullopt;
    }
    if (!lines_.empty()) {
        std::string line = std::move(lines_.front());
        lines_.pop_front();
        return line;
    }
    if (eof_) {
        return std::nullopt;
    }
    drain_stderr();
    std::string chunk;
    const platform::ReadStatus status = child_->read_stdout(chunk, timeout);
    drain_stderr();
    const auto sink = [this](std::string_view line) { lines_.emplace_back(line); };
    switch (status) {
        case platform::ReadStatus::Data:
            framer_.feed(chunk, sink);
            if (framer_.pending() > kMaxFrameBytes) {
                // A frame that never ends is a server that has gone wrong;
                // dropping the bytes keeps memory bounded, and the framing
                // resynchronises at the next newline.
                framer_.reset();
            }
            if (lines_.empty()) {
                return std::string{};
            }
            {
                std::string line = std::move(lines_.front());
                lines_.pop_front();
                return line;
            }
        case platform::ReadStatus::Timeout:
            return std::string{};
        case platform::ReadStatus::Eof:
        case platform::ReadStatus::Error:
            break;
    }
    eof_ = true;
    framer_.flush(sink);
    if (!lines_.empty()) {
        std::string line = std::move(lines_.front());
        lines_.pop_front();
        return line;
    }
    return std::nullopt;
}

std::string StdioTransport::stderr_tail() const {
    return tail_.tail();
}

void StdioTransport::close() {
    const std::lock_guard<std::mutex> lock{write_mutex_};
    if (closed_) {
        return;
    }
    closed_ = true;
    if (child_ != nullptr) {
        drain_stderr();
        child_->close_stdin();
        if (!child_->wait_for_exit(std::chrono::milliseconds{500}).has_value()) {
            child_->terminate();
        }
    }
}

std::unique_ptr<StdioTransport> spawn_stdio(const std::string& command,
                                            const std::vector<std::string>& arguments,
                                            const std::vector<std::string>& environment,
                                            StderrTail::Sink sink, std::string& error) {
    platform::ChildCommand spec;
    spec.program = command;
    spec.arguments = arguments;
    for (const std::string& entry : environment) {
        const std::size_t equals = entry.find('=');
        if (equals == std::string::npos || equals == 0) {
            error = "env entry '" + entry + "' is not KEY=VALUE";
            return nullptr;
        }
        spec.extra_environment.emplace_back(entry.substr(0, equals), entry.substr(equals + 1));
    }
    std::unique_ptr<platform::ChildProcess> child = platform::start_child(spec, error);
    if (child == nullptr) {
        if (error.empty()) {
            error = "could not start " + command;
        }
        return nullptr;
    }
    return std::make_unique<StdioTransport>(std::move(child), std::move(sink));
}

}  // namespace apogee::mcp
