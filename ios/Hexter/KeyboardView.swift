// hexter for iOS - two octaves on the screen, as many fingers as the glass can see
//
// Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.

import SwiftUI
import UIKit

struct KeyboardView: UIViewRepresentable {
    var base: Int                              // MIDI key of the leftmost C
    var onNote: (Int, Bool) -> Void

    func makeUIView(context: Context) -> KeyboardUIView {
        let view = KeyboardUIView()
        view.base = base
        view.onNote = onNote
        return view
    }

    func updateUIView(_ view: KeyboardUIView, context: Context) {
        view.onNote = onNote
        if view.base != base { view.base = base }
    }
}

final class KeyboardUIView: UIView {
    var base = 48 { didSet { releaseAll(); setNeedsDisplay() } }
    var onNote: ((Int, Bool) -> Void)?

    private let whites = 14                                  // two octaves
    private let whiteSemitone = [0, 2, 4, 5, 7, 9, 11]
    private let blackAfterWhite = [0, 1, 3, 4, 5]            // which white keys have a black key to their right
    private var held: [ObjectIdentifier: Int] = [:]

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        contentMode = .redraw
        backgroundColor = .white
    }

    required init?(coder: NSCoder) { fatalError() }

    private var whiteWidth: CGFloat { bounds.width / CGFloat(whites) }
    private var blackWidth: CGFloat { whiteWidth * 0.6 }
    private var blackHeight: CGFloat { bounds.height * 0.6 }

    private func whiteKey(_ index: Int) -> Int { base + (index / 7) * 12 + whiteSemitone[index % 7] }

    private func blackRect(afterWhite index: Int) -> CGRect {
        CGRect(x: CGFloat(index + 1) * whiteWidth - blackWidth / 2, y: 0, width: blackWidth, height: blackHeight)
    }

    private func key(at point: CGPoint) -> Int? {
        guard bounds.contains(point) else { return nil }
        for index in 0..<whites where blackAfterWhite.contains(index % 7) {
            if blackRect(afterWhite: index).contains(point) { return whiteKey(index) + 1 }
        }
        let index = min(whites - 1, max(0, Int(point.x / whiteWidth)))
        return whiteKey(index)
    }

    override func draw(_ rect: CGRect) {
        let heldKeys = Set(held.values)
        for index in 0..<whites {
            let r = CGRect(x: CGFloat(index) * whiteWidth, y: 0, width: whiteWidth, height: bounds.height)
            (heldKeys.contains(whiteKey(index)) ? UIColor.systemOrange : UIColor(white: 0.93, alpha: 1)).setFill()
            UIRectFill(r)
            UIColor(white: 0.45, alpha: 1).setFill()
            UIRectFill(CGRect(x: r.minX, y: 0, width: 1, height: bounds.height))
        }
        for index in 0..<whites where blackAfterWhite.contains(index % 7) {
            (heldKeys.contains(whiteKey(index) + 1) ? UIColor.systemOrange : UIColor(white: 0.12, alpha: 1)).setFill()
            UIRectFill(blackRect(afterWhite: index))
        }
    }

    private func press(_ touch: UITouch) {
        guard let key = key(at: touch.location(in: self)) else { return }
        let id = ObjectIdentifier(touch)
        if let old = held[id] {
            if old == key { return }
            onNote?(old, false)
        }
        held[id] = key
        onNote?(key, true)
        setNeedsDisplay()
    }

    private func release(_ touch: UITouch) {
        let id = ObjectIdentifier(touch)
        guard let key = held.removeValue(forKey: id) else { return }
        onNote?(key, false)
        setNeedsDisplay()
    }

    private func releaseAll() {
        for key in held.values { onNote?(key, false) }
        held.removeAll()
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { touches.forEach(press) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { touches.forEach(press) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { touches.forEach(release) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { touches.forEach(release) }
}
