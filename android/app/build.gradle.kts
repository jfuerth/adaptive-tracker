plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "io.github.jfuerth.adaptivetracker"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "io.github.jfuerth.adaptivetracker"
        minSdk = 26
        targetSdk = 37
        versionCode = 21
        versionName = "0.15.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Sideloadable without a release keystore. Replace before publishing anywhere.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    // Songs are shared with the desktop tools: songs/ at the repository root.
    sourceSets.getByName("main").assets.srcDir("../../songs")

    buildFeatures {
        compose = true
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    testOptions {
        // Required by AGP's Compose Preview tasks, which share the unit-test setup.
        unitTests.isIncludeAndroidResources = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
    implementation(libs.oboe)
    testImplementation(libs.junit)
}
