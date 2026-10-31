import PresentationApp

/// The process's entry point, and nothing else: the composition root, as the
/// CLI's `source/main.cpp` is. The application itself -- its scene, views and
/// view-models -- is the `PresentationApp` framework, so it tests headlessly.
@main
enum ApogeeMain {
    static func main() {
        ApogeeApp.main()
    }
}
