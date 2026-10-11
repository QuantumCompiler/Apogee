#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tui/progress.h"
#include "tui/pump.h"
#include "tui/theme.h"
#include "tui/view.h"

/// A workbench view (32d): a read drawn as a table, and actions that call
/// cores. The anti-pattern this is built against -- a TUI of hand-built
/// views, each a second implementation of what the CLI already did -- is
/// answered by its shape: the view is handed a read to draw and a core per
/// key, and computes nothing of its own. The composition root (`cli/`)
/// gives it the rows `models list`, `chats list` and the config's own reads
/// return, and actions that call what `config set-default`, `config
/// delete-backend`, `config set-default-suite` and `chats delete` call.
namespace apogee::tui {

/// One row: what identifies it to its actions, and its cells as the read
/// words them.
struct ListRow {
    std::string key;
    std::vector<std::string> cells;
    enum class Look : std::uint8_t { Plain, Dim, Attention, Active };
    Look look = Look::Plain;
};

/// A key that runs a core on the selected row.
struct ListAction {
    /// The key that runs it -- never a digit or `q`, which stay the shell's.
    std::string key;
    /// What the hint bar calls it: `make default`.
    std::string label;
    /// Whether it applies to `row`; null: every row.
    std::function<bool(const ListRow&)> applies;
    /// The question asked first, answered `y` or anything else, no; null
    /// runs it at once.
    std::function<std::string(const ListRow&)> confirm;
    /// Runs it, on the view's worker thread, and returns what to say -- a
    /// line on the notice row, or several, drawn whole under the table until
    /// Esc (37b: a fix pass's report); a throw is said as the reason it was
    /// not done. The table is read again after.
    std::function<std::string(const ListRow&)> run;
    /// A pass over the whole view rather than its selected row (37b: a fix,
    /// a scan): offered with no row at all, and run with an empty one.
    bool whole_view = false;
};

struct ListOptions {
    /// The tab strip's name: `Models`.
    std::string title;
    std::vector<std::string> columns;
    /// The read: lines above the table, then its rows -- on the view's worker
    /// thread, each time the view is shown and after each action.
    std::function<std::pair<std::vector<std::string>, std::vector<ListRow>>()> load;
    /// Enter, as a read: the selected row's detail, drawn under the table
    /// until Esc. On the worker thread.
    std::function<std::vector<std::string>(const ListRow&)> detail;
    /// Enter, as an act instead: what the row opens, on the shell's thread
    /// (the chats view opens a conversation).
    std::function<void(const ListRow&)> open;
    /// What the hint bar calls Enter: `info`, `open`.
    std::string enter_label;
    /// The key that shows `detail` when Enter opens instead (the chats
    /// view's `i`); empty when Enter shows it.
    std::string detail_key;
    /// The actions, by key. An action keyed `r` takes the key from "read
    /// again" (37b) -- the view still reads afresh each time it is shown and
    /// after every action.
    std::vector<ListAction> actions;
    /// The read is a page, not a table (37b: the System view): its lines
    /// are the content, drawn plain, with no column header and no rows.
    bool page = false;
    /// A question asked of the view (37c: a knowledge query, a node's card):
    /// `/` opens an input row under the table -- the view takes its keys as
    /// typing until Enter asks or Esc closes it, the last question kept to
    /// refine. The answer, on the worker, with the selected row or an empty
    /// one, is drawn under the table as a detail is; a throw is said as the
    /// reason it could not be asked.
    std::function<std::string(const ListRow& row, const std::string& text)> ask;
    /// What the hint bar and the input row call it: `query`, `explain`.
    std::string ask_label;
    /// The question is about the selected row (the collection searched, the
    /// graph a node is in): offered only with one.
    bool ask_needs_row = false;
    /// The key that opens the input row (37d: the Symphonies view's `p`).
    std::string ask_key = "/";
    /// Enter asks with nothing typed too (37d: a play whose input is
    /// optional).
    bool ask_may_be_empty = false;
    /// The question is an act on the shell's thread rather than a read on the
    /// worker (37d: a play handed to the session): asked there, its answer's
    /// first line said on the notice row.
    bool ask_here = false;
    /// The question put before an ask runs, answered `y` or anything else,
    /// no (37e: a task's goal and its policy, before the run spends model
    /// turns); null asks at once. A row's own action keyed as the ask is
    /// takes the key on the rows it applies to (37e: `r` resumes a resumable
    /// task, and runs a new one elsewhere).
    std::function<std::string(const ListRow& row, const std::string& text)> ask_confirm;
    /// A long run the view started (37e: a task run): its narration drawn
    /// under the table from the progress seam, Ctrl-C stopping it through its
    /// own channel, and the table read again on a slow tick while it runs and
    /// once when it ends.
    std::shared_ptr<Progress> progress;
};

class ListView {
public:
    ListView(Pump& pump, Theme theme, ListOptions options);
    ~ListView();

    ListView(const ListView&) = delete;
    ListView& operator=(const ListView&) = delete;
    ListView(ListView&&) = delete;
    ListView& operator=(ListView&&) = delete;

    /// The view to register with the shell: read afresh each time it is
    /// shown.
    [[nodiscard]] View view();

    /// Reads again, off the shell's thread; any thread.
    void refresh();

    /// Waits for the work the view has handed its thread -- a read, an
    /// action -- to finish: for a test, before it looks.
    void settle();

    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace apogee::tui
