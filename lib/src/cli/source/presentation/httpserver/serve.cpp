#include "httpserver/serve.h"

#include <httplib.h>

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <csignal>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "httpserver/http_types.h"
#include "httpserver/shutdown.h"
#include "platform/platform.h"

namespace apogee::httpserver {
namespace {

/// The server a signal stops. One process serves one port.
std::atomic<httplib::Server*> active_server{nullptr};

/// Whether a `StopOnSignals` stands: the handler stands itself again only
/// then, never after the scope has put back what was there.
std::atomic<bool> signals_standing{false};

void on_signal(int signal) {
    // Windows took the handler down to call it: stood again, the next Ctrl-C
    // is another stop rather than the default action (57j).
    if (signals_standing.load()) {
        platform::rearm_signal(signal, on_signal);
    }
    stop_serving();
}

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

HttpRequest translate(const httplib::Request& request) {
    HttpRequest out;
    out.method = request.method;
    out.path = request.path;
    for (const auto& [key, value] : request.params) {
        out.query.emplace(key, value);
    }
    for (const auto& [key, value] : request.headers) {
        out.headers[lower(key)] = value;
    }
    out.body = request.body;
    out.remote_address = request.remote_addr;
    return out;
}

void apply(HttpResponse out, httplib::Response& response) {
    response.status = out.status;
    for (const auto& [key, value] : out.headers) {
        response.set_header(key, value);
    }
    if (out.streamed()) {
        // The whole stream runs inside ONE provider call: the loop writes
        // frames as the model produces them, each `sink.write` going straight
        // to the socket, and `done()` ends the chunked body. A write that
        // fails tells the writer the client is gone.
        auto stream = std::make_shared<StreamBody>(std::move(out.stream));
        response.set_chunked_content_provider(
            out.content_type, [stream](std::size_t /*offset*/, httplib::DataSink& sink) {
                (*stream)([&sink](std::string_view piece) {
                    return sink.write(piece.data(), piece.size());
                });
                sink.done();
                return true;
            });
        return;
    }
    if (!out.body.empty()) {
        response.set_content(out.body, out.content_type);
    }
}

}  // namespace

std::string bind_refusal(const BindOptions& bind) {
    if (bind.host.empty()) {
        return "--bind needs a host";
    }
    if (bind.port < 0 || bind.port > 65535) {
        return "--port must be between 0 and 65535";
    }
    if (is_loopback_host(bind.host) || bind.allow_remote) {
        return {};
    }
    return "refusing to bind " + bind.host +
           ": a non-loopback bind exposes the inference plane -- unauthenticated by design, "
           "for OpenAI-client compatibility -- to the network. Re-run with --allow-remote to "
           "confirm this is a server deployment, or keep the default --bind 127.0.0.1";
}

bool host_check_applies(const BindOptions& bind) {
    return is_loopback_host(bind.host) && !bind.allow_remote;
}

void stop_serving() noexcept {
    // The flag first, so a stream blocked waiting for its next event sees it
    // on its next poll; then the socket, which ends the accept loop.
    request_shutdown();
    if (httplib::Server* server = active_server.load()) {
        server->stop();
    }
}

StopOnSignals::StopOnSignals() {
    signals_standing.store(true);
    previous_int_ = std::signal(SIGINT, on_signal);
    previous_term_ = std::signal(SIGTERM, on_signal);
}

StopOnSignals::~StopOnSignals() {
    signals_standing.store(false);
    (void)std::signal(SIGINT, previous_int_);
    (void)std::signal(SIGTERM, previous_term_);
}

void run_server(Mux& mux, Handler& handler, const ServeOptions& options) {
    if (const std::string refusal = bind_refusal(options.bind); !refusal.empty()) {
        throw std::runtime_error(refusal);
    }
    // The request gate's Host check is for a loopback bind (57n): under
    // --allow-remote, loopback included, the operator's proxy owns the name
    // the server is reached by. Set before the first request, never after.
    mux.set_host_check(host_check_applies(options.bind));

    clear_shutdown();
    httplib::Server server;
    server.set_payload_max_length(kMaxPayloadBytes);

    // Every method routes through the mux, which answers 404 and 405 itself
    // in the error shape a client library understands.
    const auto dispatch = [&mux](const httplib::Request& request, httplib::Response& response) {
        apply(mux.dispatch(translate(request)), response);
    };
    server.Get(".*", dispatch);
    server.Post(".*", dispatch);
    server.Put(".*", dispatch);
    server.Patch(".*", dispatch);
    server.Delete(".*", dispatch);

    // What the library refuses before a handler runs -- a body over the
    // limit, a malformed request line -- still comes back as JSON.
    server.set_error_handler([](const httplib::Request& request, httplib::Response& response) {
        if (response.body.empty()) {
            apply(error_response(response.status,
                                 "request refused: " + request.method + " " + request.path,
                                 kNotFoundError),
                  response);
        }
    });
    server.set_exception_handler(
        [](const httplib::Request&, httplib::Response& response, std::exception_ptr error) {
            std::string what = "internal error";
            try {
                if (error) {
                    std::rethrow_exception(error);
                }
            } catch (const std::exception& e) {
                what = e.what();
            } catch (...) {
                what = "an exception that is not a std::exception";
            }
            apply(error_response(500, what, kServerError), response);
        });
    if (options.on_log) {
        server.set_logger(
            [&options](const httplib::Request& request, const httplib::Response& response) {
                options.on_log(request.method + " " + request.path + " -> " +
                               std::to_string(response.status));
            });
    }

    int port = options.bind.port;
    if (port == 0) {
        port = server.bind_to_any_port(options.bind.host);
        if (port < 0) {
            throw std::runtime_error("could not bind " + options.bind.host + " to a free port");
        }
    } else if (!server.bind_to_port(options.bind.host, port)) {
        throw std::runtime_error("could not bind " + options.bind.host + ":" +
                                 std::to_string(port) +
                                 " (port in use, or not an address of this machine)");
    }
    if (options.on_listening) {
        options.on_listening(options.bind.host, port);
    }

    // The sweeper: idle sessions leave the live set on a timer. A condition
    // variable rather than a sleep so shutdown does not wait out an interval.
    std::mutex sweep_mutex;
    std::condition_variable sweep_signal;
    bool sweep_stop = false;
    const std::chrono::seconds interval =
        options.sweep_interval.count() > 0 ? options.sweep_interval : std::chrono::seconds{1};
    std::thread sweeper{[&] {
        std::unique_lock<std::mutex> lock{sweep_mutex};
        while (!sweep_signal.wait_for(lock, interval, [&] { return sweep_stop; })) {
            lock.unlock();
            (void)handler.sessions().evict_idle(options.session_ttl);
            lock.lock();
        }
    }};

    active_server.store(&server);
    bool clean = false;
    {
        const StopOnSignals signals;
        // Blocks until stop(). An in-flight turn finishes before the pool joins.
        clean = server.listen_after_bind();
    }
    active_server.store(nullptr);

    {
        const std::lock_guard<std::mutex> lock{sweep_mutex};
        sweep_stop = true;
    }
    sweep_signal.notify_all();
    sweeper.join();

    if (!clean) {
        throw std::runtime_error("the listener failed");
    }
}

}  // namespace apogee::httpserver
