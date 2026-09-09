plugins {
    id("com.android.application")
}

android {
    namespace = "uk.co.crownpark.battlesquadron"
    compileSdk = 36
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "uk.co.crownpark.battlesquadron"
        minSdk = 30
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DRAYLIB_DIR=/home/jon/raylib-src"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    sourceSets["main"].assets.srcDirs(
        "../../original/whdload/BattleSquadron",
        "../../assets"
    )

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}
