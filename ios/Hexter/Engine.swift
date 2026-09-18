// hexter for iOS - the engine, its audio output, and what the screen needs to know about it
//
// Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.

import AVFoundation
import Combine
import Foundation

final class Engine: ObservableObject {
    @Published var bankName = ""
    @Published var programNames: [String] = []
    @Published var program = 0
    @Published var peak: Float = 0
    @Published var sampleRate: Double = 0
    @Published var midiStatus = "No MIDI input. Plug in a keyboard, or play the one below."
    @Published var error: String?

    let bundledBanks: [URL]

    private var bridge: OpaquePointer?
    private let audio = AVAudioEngine()
    private var meter: Timer?
    private var midi: MIDIInput?
    private var observers: [NSObjectProtocol] = []

    init() {
        let urls = Bundle.main.urls(forResourcesWithExtension: "dx7", subdirectory: nil) ?? []
        bundledBanks = urls.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    func start() {
        guard bridge == nil else { return }
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setPreferredSampleRate(48000)
            try session.setPreferredIOBufferDuration(0.005)
            try session.setActive(true)
        } catch {
            self.error = "Audio session: \(error.localizedDescription)"
        }
        let rate = session.sampleRate
        sampleRate = rate

        guard let b = hexter_bridge_new(Float(rate)) else {
            error = "The engine could not start."
            return
        }
        bridge = b

        let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1)!
        let source = AVAudioSourceNode(format: format) { _, _, frameCount, bufferList -> OSStatus in
            let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
            guard let data = buffers[0].mData else { return noErr }
            hexter_bridge_render(b, data.assumingMemoryBound(to: Float.self), frameCount)
            return noErr
        }
        audio.attach(source)
        audio.connect(source, to: audio.mainMixerNode, format: format)
        startAudio()

        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] note in
            guard let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                  AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
            self?.startAudio()
        })
        observers.append(center.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self, !self.audio.isRunning else { return }
            self.startAudio()
        })

        meter = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            guard let self, let b = self.bridge else { return }
            self.peak = hexter_bridge_peak(b)
        }

        midi = MIDIInput(sink: { [weak self] bytes in self?.midi(bytes) },
                         status: { [weak self] text in DispatchQueue.main.async { self?.midiStatus = text } })
        midi?.start()

        if let first = bundledBanks.first(where: { $0.lastPathComponent.hasPrefix("dx7_roms") }) ?? bundledBanks.first {
            _ = load(url: first)
        }
    }

    private func startAudio() {
        do {
            try audio.start()
            let session = AVAudioSession.sharedInstance()
            NSLog("hexter: %.0f Hz, io buffer %.1f ms, output latency %.1f ms",
                  session.sampleRate, session.ioBufferDuration * 1000, session.outputLatency * 1000)
        } catch {
            self.error = "Audio: \(error.localizedDescription)"
        }
    }

    /// Loads a bank file (any format the engine reads) into programs 0..127.
    @discardableResult
    func load(url: URL) -> Bool {
        guard let b = bridge else { return false }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url) else {
            error = "Could not read \(url.lastPathComponent)."
            return false
        }
        var message = [CChar](repeating: 0, count: 256)
        let count = data.withUnsafeBytes { raw -> Int32 in
            hexter_bridge_load_bank(b, raw.bindMemory(to: UInt8.self).baseAddress, data.count,
                                    url.lastPathComponent, &message, message.count)
        }
        if count <= 0 {
            error = "\(url.lastPathComponent): \(String(cString: message))"
            return false
        }
        bankName = url.deletingPathExtension().lastPathComponent
        programNames = (0..<128).map { i in
            var name = [CChar](repeating: 0, count: 16)
            hexter_bridge_program_name(b, Int32(i), &name)
            return String(cString: name)
        }
        program = 0
        return true
    }

    var patchCount: Int { programNames.count }

    func select(_ index: Int) {
        guard let b = bridge, index >= 0, index < 128 else { return }
        program = index
        hexter_bridge_select_program(b, Int32(index))
    }

    func setVolume(_ db: Float) {
        guard let b = bridge else { return }
        hexter_bridge_set_volume(b, db)
    }

    /// Any thread: a complete MIDI message.
    func midi(_ bytes: [UInt8]) {
        guard let b = bridge else { return }
        bytes.withUnsafeBufferPointer { buf in
            _ = hexter_bridge_midi(b, buf.baseAddress, buf.count)
        }
    }

    func noteOn(_ key: Int, velocity: Int = 100) { midi([0x90, UInt8(clamping: key), UInt8(clamping: velocity)]) }
    func noteOff(_ key: Int) { midi([0x80, UInt8(clamping: key), 64]) }
}
