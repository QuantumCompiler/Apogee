import Foundation

/// Where each node of a graph sits: a seeded force layout, computed here from
/// the nodes and edges a document gave -- the same input, the same picture.
///
/// Fruchterman-Reingold over a unit square, a fixed number of passes, the
/// start positions from a seeded generator, so a test can pin every position.
public struct GraphLayout: Equatable, Sendable {
    public struct Point: Equatable, Sendable {
        public let x: Double
        public let y: Double
    }

    /// Each node's position in the unit square, by name, inset from its edge.
    public let positions: [String: Point]

    public static let iterations = 300
    public static let inset = 0.08

    public init(nodes: [String], edges: [(String, String)], seed: UInt64 = 0x5EED) {
        let names = Array(Set(nodes)).sorted()
        let count = names.count
        guard count > 0 else {
            positions = [:]
            return
        }
        if count == 1 {
            positions = [names[0]: Point(x: 0.5, y: 0.5)]
            return
        }
        var rng = SplitMix64(seed: seed)
        var x = names.map { _ in rng.unit() }
        var y = names.map { _ in rng.unit() }
        let index = Dictionary(uniqueKeysWithValues: names.enumerated().map { ($1, $0) })
        let links = edges.compactMap { e -> (Int, Int)? in
            guard let a = index[e.0], let b = index[e.1], a != b else { return nil }
            return (a, b)
        }
        let k = (1.0 / Double(count)).squareRoot()
        var temperature = 0.1
        for _ in 0..<Self.iterations {
            var dx = [Double](repeating: 0, count: count)
            var dy = [Double](repeating: 0, count: count)
            for i in 0..<count {
                for j in (i + 1)..<count {
                    var ddx = x[i] - x[j]
                    var ddy = y[i] - y[j]
                    var distance = (ddx * ddx + ddy * ddy).squareRoot()
                    if distance < 1e-6 {
                        ddx = 1e-3 * Double(i - j)
                        ddy = 1e-3
                        distance = (ddx * ddx + ddy * ddy).squareRoot()
                    }
                    let force = k * k / distance
                    dx[i] += ddx / distance * force
                    dy[i] += ddy / distance * force
                    dx[j] -= ddx / distance * force
                    dy[j] -= ddy / distance * force
                }
            }
            for (a, b) in links {
                let ddx = x[a] - x[b]
                let ddy = y[a] - y[b]
                let distance = max((ddx * ddx + ddy * ddy).squareRoot(), 1e-6)
                let force = distance * distance / k
                dx[a] -= ddx / distance * force
                dy[a] -= ddy / distance * force
                dx[b] += ddx / distance * force
                dy[b] += ddy / distance * force
            }
            for i in 0..<count {
                let length = max((dx[i] * dx[i] + dy[i] * dy[i]).squareRoot(), 1e-9)
                let step = min(length, temperature)
                x[i] = min(1, max(0, x[i] + dx[i] / length * step))
                y[i] = min(1, max(0, y[i] + dy[i] / length * step))
            }
            temperature = max(temperature * 0.98, 0.002)
        }
        // Scaled to fill the square inside its inset.
        let (minX, maxX) = (x.min()!, x.max()!)
        let (minY, maxY) = (y.min()!, y.max()!)
        func fit(_ v: Double, _ lo: Double, _ hi: Double) -> Double {
            hi - lo < 1e-9 ? 0.5 : Self.inset + (v - lo) / (hi - lo) * (1 - 2 * Self.inset)
        }
        var out: [String: Point] = [:]
        for (i, name) in names.enumerated() {
            out[name] = Point(x: fit(x[i], minX, maxX), y: fit(y[i], minY, maxY))
        }
        positions = out
    }
}

/// A small seeded generator: SplitMix64.
struct SplitMix64 {
    private var state: UInt64

    init(seed: UInt64) {
        state = seed
    }

    mutating func next() -> UInt64 {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }

    /// In [0, 1).
    mutating func unit() -> Double {
        Double(next() >> 11) / Double(1 << 53)
    }
}
