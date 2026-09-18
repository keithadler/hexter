// hexter for iOS
//
// Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.

import SwiftUI

@main
struct HexterApp: App {
    @StateObject private var engine = Engine()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(engine)
                .onAppear { engine.start() }
        }
    }
}
