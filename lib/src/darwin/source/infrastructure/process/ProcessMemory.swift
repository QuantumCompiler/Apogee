import Darwin
import Foundation

/// What a process holds in memory, as the system counts it: the app reads its
/// own children's this way, never by parsing `ps`.
public enum ProcessMemory {
    /// The resident bytes of process `pid`, or `nil` when it is gone or the
    /// system will not say.
    public static func resident(pid: Int32) -> Int? {
        var info = proc_taskinfo()
        let size = Int32(MemoryLayout<proc_taskinfo>.size)
        let read = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, size)
        guard read == size else { return nil }
        return Int(info.pti_resident_size)
    }
}
