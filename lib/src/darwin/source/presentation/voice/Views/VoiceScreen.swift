import AVFoundation
import BusinessJobs
import BusinessMedia
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Voice place: the speeches, one played with its sentences followed,
/// and the say sheet.
public struct VoiceScreen: View {
    @Bindable private var model: VoiceViewModel
    @State private var addingFolder = false

    public init(model: VoiceViewModel) {
        self.model = model
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Voice") {
                Button("Say something…") { model.sheetOpen = true }.disabled(model.isRunning)
            }
            Divider()
            HStack(spacing: 0) {
                VStack(spacing: 0) {
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 1) {
                            if let run = model.run {
                                HStack {
                                    Text(run.status == .running ? (run.segment.map { "Segment \($0.done) of \($0.of)" } ?? "Speaking") : Self.status(run))
                                        .font(.apogeeSmall)
                                    Spacer()
                                    if model.isRunning {
                                        Button("Stop") { Task { await model.stop() } }
                                    } else {
                                        Button("Dismiss") { model.dismissRun() }
                                    }
                                }
                                .padding(Spacing.m)
                                .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
                            }
                            ForEach(model.speeches) { speech in
                                let current = model.selected == speech.output
                                Button {
                                    model.select(speech)
                                } label: {
                                    VStack(alignment: .leading, spacing: 2) {
                                        Text(speech.name).font(.apogeeBody).lineLimit(1)
                                        Text(([speech.voice] + [SpeechPage.length(speech).map { String(format: "%.1f s", $0) }].compactMap { $0 }).joined(separator: " · "))
                                            .font(.apogeeMeta)
                                            .foregroundStyle(Palette.textTertiary)
                                    }
                                    .padding(Spacing.m)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                            }
                            if model.speeches.isEmpty && model.run == nil {
                                Text("No speeches yet. Text is spoken into a WAV, its record beside it in ~/Music/Apogee.")
                                    .font(.apogeeSmall)
                                    .foregroundStyle(Palette.textTertiary)
                                    .padding(Spacing.m)
                            }
                        }
                        .padding(Spacing.s)
                    }
                    Divider()
                    HStack {
                        Spacer()
                        Button("Add a folder…") { addingFolder = true }.font(.apogeeSmall)
                    }
                    .padding(Spacing.s)
                }
                .frame(width: 264)
                .background(Palette.sidebar)
                Divider()
                if let speech = model.selectedSpeech {
                    SpeechPage(model: model, speech: speech)
                } else {
                    EmptyState(symbol: "waveform", title: "Voice", message: "Pick a speech to hear it, its sentences followed as they are spoken.")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
        .sheet(isPresented: $model.sheetOpen) { SaySheet(model: model) }
        .sheet(isPresented: Binding(get: { model.consent != nil }, set: { if !$0 && model.consent != nil { model.decline() } })) {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text(model.consentRebuilds ? "Rebuild Apogee's Python environment?" : "Install what voice needs?").font(.apogeeHeading)
                Text(model.consentSaid).font(.apogeeBody).foregroundStyle(Palette.textSecondary)
                Text(model.consentRebuilds
                     ? MediaLibrary.rebuildNote
                     : "This installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                Text((["apogee"] + (model.consent ?? [])).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                HStack {
                    Spacer()
                    Button("Not now", role: .cancel) { model.decline() }.keyboardShortcut(.cancelAction)
                    Button(model.consentRebuilds ? "Rebuild" : "Install") { model.install() }.keyboardShortcut(.defaultAction)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 580)
        }
        .fileImporter(isPresented: $addingFolder, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { model.add(folder: url) }
        }
    }

    static func status(_ run: JobProgress) -> String {
        switch run.status {
        case .done: "Done"
        case .stopped: "Stopped: nothing was written"
        case .failed(let message): message
        case .running: "Speaking"
        }
    }
}

/// One speech: the player, and its sentences lit as they are spoken.
struct SpeechPage: View {
    @Bindable var model: VoiceViewModel
    let speech: SpeechRecord

    /// How long the speech is: the record's own length, else where its last
    /// sentence ends.
    static func length(_ speech: SpeechRecord) -> Double? {
        speech.seconds ?? speech.sentences.last.map { $0.offset + $0.seconds }
    }
    @State private var player: AVAudioPlayer?
    @State private var playing = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                HStack {
                    Button(playing ? "Pause" : "Play") { toggle() }
                        .keyboardShortcut(.space, modifiers: [])
                    Text(Self.length(speech).map { String(format: "%.1f of %.1f s", model.position, $0) } ?? String(format: "%.1f s", model.position))
                        .font(.apogeeMeta.monospacedDigit())
                        .foregroundStyle(Palette.textSecondary)
                    Spacer()
                    Text("\(speech.voice)\(speech.language.map { " · \($0)" } ?? "") · \(speech.model)")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                }
                VStack(alignment: .leading, spacing: Spacing.s) {
                    ForEach(Array(speech.sentences.enumerated()), id: \.offset) { index, sentence in
                        Button {
                            seek(to: index)
                        } label: {
                            Text(sentence.text)
                                .font(.apogeeProse)
                                .foregroundStyle(model.currentSentence == index ? Palette.textPrimary : Palette.textSecondary)
                                .padding(.horizontal, Spacing.s)
                                .padding(.vertical, Spacing.xs)
                                .background(model.currentSentence == index ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .buttonStyle(.plain)
                        .help(String(format: "from %.1f s", sentence.offset))
                    }
                }
                Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: speech.output)]) }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
        .task(id: speech.output) {
            player = try? AVAudioPlayer(contentsOf: URL(fileURLWithPath: speech.output))
            playing = false
            // The clock the highlight follows: the player's own position.
            while !Task.isCancelled {
                if let player { model.position = player.currentTime }
                if playing, player?.isPlaying == false { playing = false }
                try? await Task.sleep(for: .milliseconds(50))
            }
        }
        .onDisappear { player?.stop() }
    }

    private func toggle() {
        guard let player else { return }
        if player.isPlaying {
            player.pause()
            playing = false
        } else {
            player.play()
            playing = true
        }
    }

    private func seek(to sentence: Int) {
        guard let player, let start = model.start(of: sentence) else { return }
        player.currentTime = start
        model.position = start
        if !player.isPlaying {
            player.play()
            playing = true
        }
    }
}

/// The say sheet: the text, a voice from the model's own, the speed.
struct SaySheet: View {
    @Bindable var model: VoiceViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Say something").font(.apogeeHeading)
            TextField("What to say", text: $model.text, axis: .vertical).lineLimit(3...8)
            if model.voices.isEmpty {
                Text(model.voiceModel == nil
                    ? "No voice model is in the store yet: apogee models pull hexgrad/Kokoro-82M brings one."
                    : "The voice model lists no voices.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
            } else {
                Picker("Voice", selection: $model.voice) {
                    Text("The default (af_heart when it has it)").tag("")
                    ForEach(model.voices) { group in
                        Section(group.language) {
                            ForEach(group.voices, id: \.self) { Text($0).tag($0) }
                        }
                    }
                }
            }
            HStack {
                Text("Speed")
                Slider(value: $model.speed, in: 0.5...2.0, step: 0.1)
                Text(String(format: "%.1f×", model.speed)).font(.apogeeMeta.monospacedDigit())
            }
            Text("Written to \(model.plannedOutput.path) -- never over a file that is there.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            if let arguments = model.plannedArguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { model.sheetOpen = false }.keyboardShortcut(.cancelAction)
                Button("Speak") { model.speak() }
                    .keyboardShortcut(.return, modifiers: .command)
                    .disabled(model.plannedArguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }
}

#if DEBUG
#Preview("A speech, its second sentence lit") {
    let model = VoicePreview.speeches("screen-read")
    VoiceScreen(model: model)
        .frame(width: 1100, height: 600)
        .task { await VoicePreview.playing(model, at: 1.2) }
}

#Preview("No speeches yet") {
    let model = VoicePreview.speeches("screen-empty", spoken: false)
    VoiceScreen(model: model)
        .frame(width: 1100, height: 600)
        .task { await model.connect(binary: previewBinary) }
}

#Preview("Speaking: segment 1 of 2") {
    let model = VoicePreview.speeches("screen-running", starter: PreviewJobs(said: VoicePreview.segment))
    VoiceScreen(model: model)
        .frame(width: 1100, height: 600)
        .task { await VoicePreview.say(model) }
}

#Preview("Refused for its runtime: asking to install") {
    let model = VoicePreview.speeches("screen-refused", starter: VoicePreview.refused)
    VoiceScreen(model: model)
        .frame(width: 1100, height: 600)
        .task { await VoicePreview.say(model) }
}

#Preview("Not installed: the refusal stands") {
    let model = VoicePreview.speeches("screen-declined", starter: VoicePreview.refused)
    VoiceScreen(model: model)
        .frame(width: 1100, height: 600)
        .task {
            await VoicePreview.say(model)
            await VoicePreview.until { model.consent != nil }
            model.decline()
        }
}

#Preview("Hello there: its first sentence lit") {
    let model = VoicePreview.speeches("page-first")
    if let speech = VoicePreview.spoken(by: model) {
        SpeechPage(model: model, speech: speech)
            .frame(width: 800, height: 420)
            .task { await VoicePreview.playing(model, at: 0) }
    }
}

#Preview("Its second sentence lit") {
    let model = VoicePreview.speeches("page-second")
    if let speech = VoicePreview.spoken(by: model) {
        SpeechPage(model: model, speech: speech)
            .frame(width: 800, height: 420)
            .task { await VoicePreview.playing(model, at: 1.2) }
    }
}

#Preview("Say something, in the model's own voices") {
    let model = VoicePreview.speeches("sheet-voices")
    SaySheet(model: model)
        .task {
            await model.connect(binary: previewBinary)
            model.text = "Hello there. How are you?"
            model.voice = "bf_emma"
        }
}

#Preview("Say something, no voice model in the store") {
    let model = VoicePreview.speeches("sheet-no-model", runner: VoicePreview.noVoiceModel)
    SaySheet(model: model)
        .task {
            await model.connect(binary: previewBinary)
            model.text = "Hello there. How are you?"
        }
}
#endif
