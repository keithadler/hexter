import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing comes from a keystore outside the tree, named by environment variables in CI
// (HEXTER_KEYSTORE, HEXTER_KEYSTORE_PASSWORD, HEXTER_KEY_ALIAS, HEXTER_KEY_PASSWORD) or by
// android/keystore.properties locally. Without either, the release build is unsigned.
val keystoreProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
fun signingValue(name: String): String? = System.getenv(name) ?: keystoreProps.getProperty(name)
val haveSigning = signingValue("HEXTER_KEYSTORE") != null

android {
    namespace = "org.keithadler.hexter"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.keithadler.hexter"
        minSdk = 26            // AAudio, which Oboe uses for low latency, arrived in 8.0
        targetSdk = 35
        versionCode = 1
        versionName = rootProject.file("../CMakeLists.txt").readText()
            .let { Regex("""VERSION\s+(\d+\.\d+\.\d+)""").find(it)?.groupValues?.get(1) ?: "0.0.0" }
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        // Oboe's prebuilt package links the shared C++ runtime, so the app must too.
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_shared") } }
    }

    signingConfigs {
        if (haveSigning) {
            create("release") {
                storeFile = file(signingValue("HEXTER_KEYSTORE")!!)
                storePassword = signingValue("HEXTER_KEYSTORE_PASSWORD")
                keyAlias = signingValue("HEXTER_KEY_ALIAS")
                keyPassword = signingValue("HEXTER_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (haveSigning) signingConfig = signingConfigs.getByName("release")
        }
    }

    buildFeatures { prefab = true }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }

    // The six DX7 banks the desktop packages ship ride along as assets (extra/ also holds C
    // sources, so the banks are copied into a generated folder rather than the folder used as is).
    sourceSets["main"].assets.srcDirs(layout.buildDirectory.dir("generated/banks"))

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.google.oboe:oboe:1.11.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.activity:activity-ktx:1.10.1")
    implementation("com.google.android.material:material:1.12.0")
}

val copyBanks by tasks.registering(Copy::class) {
    from(rootProject.file("../extra")) { include("*.dx7") }
    into(layout.buildDirectory.dir("generated/banks/banks"))
}
tasks.named("preBuild") { dependsOn(copyBanks) }

