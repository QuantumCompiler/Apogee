#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"

/// Where a stored model came from, read from the records the store already
/// keeps (M4).
///
/// **A reading of records, not a new one.** Every GGUF `models convert` and
/// `models quantize` have made names its parent in its record -- `source`
/// says which verb made it, `ref` what it was made from, as the handle every
/// verb takes (`<model>/safetensors/<id>`, `<model>/gguf/<id>`), or as a path
/// for weights outside the store. That has been so since the store's first
/// day (2026-09-23), so a second field saying it again would be a second copy
/// of one fact.
///
/// **Inference is narrow, and says so.** A GGUF with no record of where it
/// came from, under a model whose store holds exactly one snapshot, is taken
/// to have been converted from it; so is a quantization whose F16 has been
/// deleted, record and all -- the space an F16 takes is why it goes. Every
/// answer built on that carries `inferred`. With two snapshots either could
/// be the parent, and nothing is guessed. Outside the store nothing is
/// inferred at all.
///
/// **Pure over a sweep.** Built from what a listing already holds -- the
/// stored GGUFs, their records, the snapshots and theirs -- so `models list`
/// reads nothing twice. The one thing asked of the disk is whether a parent
/// named by a path outside the store is still there.
namespace apogee::models {

/// How a model file came to be.
enum class OriginKind : std::uint8_t {
    /// No record says, and nothing could be inferred.
    Unknown,
    /// Downloaded as it is: from Hugging Face, or out of Ollama's store.
    Pulled,
    /// Made from a SafeTensors set by `models convert`.
    Converted,
    /// Made from another GGUF by `models quantize`.
    Quantized,
    /// A fine-tune `train promote` stored.
    Trained,
};

/// One step back from a model towards where it came from.
struct Origin {
    OriginKind kind = OriginKind::Unknown;
    /// What it came from: a handle in the store or a path outside it (a
    /// conversion, a quantization), the ref it was pulled as, or the backend
    /// and version a fine-tune was promoted to.
    std::string from;
    /// For a pull, the source (`huggingface`, `ollama`); for a quantization,
    /// its level; for a fine-tune, its run.
    std::string detail;
    /// Taken from the store's shape rather than read from any record.
    bool inferred = false;
};

/// What `record` says the GGUF it describes came from. No record, or one
/// naming no source Apogee writes, is `Unknown`.
[[nodiscard]] Origin origin_from_record(const std::optional<Sidecar>& record);

/// Where a SafeTensors set was pulled from: its record's ref, with the
/// revision when it is not `main`. No record is `Unknown`.
[[nodiscard]] Origin origin_from_snapshot(const std::optional<Snapshot>& record);

/// One stored GGUF and its record, as a sweep read them.
struct RecordedGguf {
    StoredGguf stored;
    std::optional<Sidecar> record;
};

/// One stored SafeTensors set and its record, as a sweep read them.
struct RecordedSnapshot {
    StoredSnapshot stored;
    std::optional<Snapshot> record;
};

/// One link of a chain: the origin, and whether what it names is gone.
struct LineageLink {
    Origin origin;
    /// The handle or path `origin.from` names is no longer on disk. The link
    /// is still said -- a broken chain names what it came from -- and is the
    /// chain's last.
    bool missing = false;
};

/// How a snapshot was consumed: which GGUFs were made from it.
struct Consumption {
    /// Known from the store's shape only -- no record ties any GGUF to it.
    bool inferred = false;
    /// The handles of the GGUFs whose chain reaches it -- its conversion, and
    /// what was quantized from that. Only those every link of which is
    /// recorded, when there are any.
    std::vector<std::string> by;
};

/// The lineage of everything in one store.
class Lineage {
public:
    Lineage() = default;
    Lineage(std::vector<RecordedGguf> ggufs, std::vector<RecordedSnapshot> snapshots);

    /// Where `file` came from. A stored GGUF answers from the record the sweep
    /// read, inferring where it may; any other file from `record`, never
    /// inferred.
    [[nodiscard]] Origin origin_of(const std::filesystem::path& file,
                                   const std::optional<Sidecar>& record) const;

    /// Where the stored weights `handle` came from: a GGUF's origin, or the
    /// pull a SafeTensors set came from. `Unknown` for a handle not stored.
    [[nodiscard]] Origin origin_of(std::string_view handle) const;

    /// The chain back from `origin`, one link per step -- quantized from an
    /// F16, converted from a snapshot, pulled from its upstream -- ending
    /// where the records end or at a link that is gone. A quantization whose
    /// F16 is gone continues, inferred, to its model's one snapshot.
    [[nodiscard]] std::vector<LineageLink> chain(const Origin& origin) const;

    /// Whether the chain back from `origin` passes through a conversion: the
    /// file was made, somewhere along the way, from a SafeTensors set.
    [[nodiscard]] bool converted(const Origin& origin) const;

    /// How the stored snapshot `handle` was consumed, or nullopt when no
    /// stored GGUF's chain reaches it. Recorded lineage answers first;
    /// inference only when no record does.
    [[nodiscard]] std::optional<Consumption> consumed(std::string_view handle) const;

private:
    [[nodiscard]] const RecordedGguf* gguf(std::string_view handle) const;
    [[nodiscard]] const RecordedSnapshot* snapshot(std::string_view handle) const;
    [[nodiscard]] Origin stored_origin(const RecordedGguf& gguf) const;
    /// `model`'s snapshot when it has exactly one, else null.
    [[nodiscard]] const RecordedSnapshot* only_snapshot(std::string_view model) const;

    std::vector<RecordedGguf> ggufs_;
    std::vector<RecordedSnapshot> snapshots_;
};

/// The store's lineage, read: every stored GGUF and snapshot with its record.
/// For a command with no sweep of its own (`models info`); records only,
/// never a model header.
[[nodiscard]] Lineage read_lineage(const StoreRoots& roots);

}  // namespace apogee::models
