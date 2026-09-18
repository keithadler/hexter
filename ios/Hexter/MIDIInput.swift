// hexter for iOS - every MIDI source the system knows about, USB or Bluetooth, feeds the engine
//
// Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.

import CoreMIDI
import Foundation

final class MIDIInput {
    private var client = MIDIClientRef()
    private var port = MIDIPortRef()
    private let sink: ([UInt8]) -> Void
    private let status: (String) -> Void

    // Parser state. CoreMIDI delivers packets on its own thread, one packet at a time,
    // so plain properties are fine here.
    private var pending: [UInt8] = []
    private var need = 0
    private var runningStatus: UInt8 = 0
    private var sysex: [UInt8] = []
    private var inSysex = false
    private let maxSysex = 4608

    init(sink: @escaping ([UInt8]) -> Void, status: @escaping (String) -> Void) {
        self.sink = sink
        self.status = status
    }

    func start() {
        MIDIClientCreateWithBlock("hexter" as CFString, &client) { [weak self] _ in
            DispatchQueue.main.async { self?.connectAll() }
        }
        MIDIInputPortCreateWithBlock(client, "hexter in" as CFString, &port) { [weak self] packets, _ in
            self?.handle(packets)
        }
        connectAll()
    }

    deinit {
        if port != 0 { MIDIPortDispose(port) }
        if client != 0 { MIDIClientDispose(client) }
    }

    private func connectAll() {
        var names: [String] = []
        for i in 0..<MIDIGetNumberOfSources() {
            let source = MIDIGetSource(i)
            MIDIPortConnectSource(port, source, nil)
            names.append(displayName(of: source))
        }
        status(names.isEmpty ? "No MIDI input. Plug in a keyboard, or play the one below."
                             : "MIDI in: " + names.joined(separator: ", "))
    }

    private func displayName(of object: MIDIObjectRef) -> String {
        var name: Unmanaged<CFString>?
        if MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &name) == noErr, let s = name?.takeRetainedValue() {
            return s as String
        }
        return "MIDI device"
    }

    private func handle(_ packets: UnsafePointer<MIDIPacketList>) {
        for packet in packets.unsafeSequence() {
            let raw = UnsafeRawPointer(packet).advanced(by: MemoryLayout<MIDIPacket>.offset(of: \.data)!)
            let bytes = UnsafeBufferPointer(start: raw.assumingMemoryBound(to: UInt8.self), count: Int(packet.pointee.length))
            for byte in bytes { parse(byte) }
        }
    }

    private func parse(_ byte: UInt8) {
        if byte >= 0xF8 { return }                                   // realtime: not the engine's business
        if inSysex {
            sysex.append(byte)
            if byte == 0xF7 { sink(sysex); sysex = []; inSysex = false }
            else if sysex.count > maxSysex { sysex = []; inSysex = false }
            return
        }
        if byte == 0xF0 { inSysex = true; sysex = [0xF0]; pending = []; return }
        if byte >= 0xF1 { pending = []; need = 0; runningStatus = 0; return }   // system common: ignored
        if byte >= 0x80 {
            pending = [byte]
            runningStatus = byte
            let kind = byte & 0xF0
            need = (kind == 0xC0 || kind == 0xD0) ? 2 : 3
            return
        }
        if pending.isEmpty {
            guard runningStatus != 0 else { return }
            pending = [runningStatus]
            let kind = runningStatus & 0xF0
            need = (kind == 0xC0 || kind == 0xD0) ? 2 : 3
        }
        pending.append(byte)
        if pending.count == need {
            sink(pending)
            pending = []
        }
    }
}
