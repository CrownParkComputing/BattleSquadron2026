import org.gradle.api.GradleException
import org.gradle.api.tasks.Sync

plugins {
    id("com.android.application")
}

val nativeRoot = rootProject.projectDir.parentFile
val gameDataDir = providers.gradleProperty("battleSquadronDataDir")
    .orElse(providers.environmentVariable("BATTLE_SQUADRON_DATA_DIR"))
val raylibSourceDir = providers.gradleProperty("raylibDir")
    .orElse(providers.environmentVariable("RAYLIB_DIR"))
    .orElse(nativeRoot.parentFile.resolve("raylib-src").absolutePath)
val generatedAssets = layout.buildDirectory.dir("generated/native-assets").get().asFile

val stageNativeAssets by tasks.registering(Sync::class) {
    into(generatedAssets)
    from(nativeRoot.resolve("assets")) {
        into("assets")
    }
    from(gameDataDir.map { file(it) }) {
        into("data")
    }
    doFirst {
        if (!gameDataDir.isPresent) {
            throw GradleException(
                "Set -PbattleSquadronDataDir=/path/to/BattleSquadron/data " +
                    "or BATTLE_SQUADRON_DATA_DIR"
            )
        }
        val loader = file(gameDataDir.get()).resolve("LOADER")
        if (!loader.isFile) {
            throw GradleException("Battle Squadron LOADER not found in ${loader.parent}")
        }
    }
}

android {
    namespace = "uk.co.crownpark.battlesquadron"
    compileSdk = 36
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "uk.co.crownpark.battlesquadron"
        minSdk = 30
        targetSdk = 35
        versionCode = 3
        versionName = "0.3"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DRAYLIB_DIR=${raylibSourceDir.get()}"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    sourceSets["main"].assets.directories.add(generatedAssets.absolutePath)

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}

tasks.named("preBuild").configure {
    dependsOn(stageNativeAssets)
}
