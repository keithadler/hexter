// hexter for iOS - the one screen: bank, voices, volume, meter, keyboard
//
// Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.

import CoreAudioKit
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject private var engine: Engine
    @State private var importing = false
    @State private var showBluetooth = false
    @State private var volume: Double = 0
    @State private var octave = 4                          // leftmost key is C(octave-1) in MIDI terms: C4 = 60

    private var meterBars: Int {
        engine.peak > 0.001 ? min(20, max(1, Int(engine.peak * 20))) : 0
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 12) {
                Menu {
                    ForEach(engine.bundledBanks, id: \.self) { url in
                        Button(url.deletingPathExtension().lastPathComponent) { engine.load(url: url) }
                    }
                } label: {
                    Label(engine.bankName.isEmpty ? "Bank" : engine.bankName, systemImage: "chevron.down")
                        .labelStyle(.titleAndIcon)
                }
                Spacer()
                Button("Bluetooth MIDI") { showBluetooth = true }
                Button("Open bank…") { importing = true }
                    .buttonStyle(.borderedProminent)
                    .tint(.orange)
            }
            .font(.headline)

            Text(engine.bankName.isEmpty ? "No bank loaded."
                 : "\(engine.bankName): \(engine.patchCount) voices, \(String(Int(engine.sampleRate))) Hz")
                .font(.caption)
            Text(engine.midiStatus)
                .font(.caption)
                .foregroundStyle(.secondary)

            List {
                ForEach(Array(engine.programNames.enumerated()), id: \.offset) { index, name in
                    HStack {
                        Text("\(index + 1)").frame(width: 36, alignment: .trailing).foregroundStyle(.secondary)
                        Text(name).font(.system(.body, design: .monospaced))
                        Spacer()
                        if index == engine.program { Image(systemName: "checkmark.circle.fill").foregroundStyle(.orange) }
                    }
                    .contentShape(Rectangle())
                    .onTapGesture { engine.select(index) }
                }
            }
            .listStyle(.plain)

            HStack(spacing: 12) {
                Text("Vol").font(.caption)
                Slider(value: $volume, in: -70...20)
                    .tint(.orange)
                    .onChange(of: volume) { engine.setVolume(Float($0)) }
                Text(String(repeating: "▮", count: meterBars))
                    .font(.system(.body, design: .monospaced))
                    .foregroundStyle(.orange)
                    .frame(width: 120, alignment: .leading)
                    .lineLimit(1)
                Stepper("C\(octave - 1)", value: $octave, in: 1...7)
                    .font(.caption)
                    .frame(width: 130)
            }

            KeyboardView(base: octave * 12) { key, on in
                if on { engine.noteOn(key) } else { engine.noteOff(key) }
            }
            .frame(minHeight: 140, idealHeight: 180, maxHeight: 220)
            .border(Color(white: 0.45), width: 1)
        }
        .padding(.horizontal, 10)
        .padding(.top, 4)
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data, .item]) { result in
            if case .success(let url) = result { engine.load(url: url) }
        }
        .sheet(isPresented: $showBluetooth) { BluetoothMIDIView() }
        .alert("hexter", isPresented: Binding(get: { engine.error != nil }, set: { if !$0 { engine.error = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(engine.error ?? "")
        }
    }
}

/// Apple's own Bluetooth MIDI pairing screen, the same one GarageBand shows.
struct BluetoothMIDIView: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> UINavigationController {
        let controller = CABTMIDICentralViewController()
        controller.title = "Bluetooth MIDI"
        return UINavigationController(rootViewController: controller)
    }

    func updateUIViewController(_ controller: UINavigationController, context: Context) {}
}
