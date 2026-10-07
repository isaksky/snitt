/* Small Mach-O entry point: find the bundle and launch the standalone Jolt image. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void path(char *out, size_t size, const char *contents, const char *suffix) {
    if (snprintf(out, size, "%s/%s", contents, suffix) >= (int)size) {
        fputs("Glimmer Experiment bundle path is too long.\n", stderr);
        exit(1);
    }
}

int main(int argc, char **argv) {
    char executable[PATH_MAX], contents[PATH_MAX], plugins[PATH_MAX];
    char qml[PATH_MAX], frameworks[PATH_MAX], jolt[PATH_MAX], clip[PATH_MAX];
    if (!realpath(argv[0], executable)) {
        perror("Glimmer Experiment executable");
        return 1;
    }
    char *marker = strstr(executable, "/Contents/MacOS/");
    if (!marker || (size_t)(marker - executable) + strlen("/Contents") >= sizeof(contents)) {
        fputs("Glimmer Experiment must run from its app bundle.\n", stderr);
        return 1;
    }
    size_t length = (size_t)(marker - executable) + strlen("/Contents");
    memcpy(contents, executable, length);
    contents[length] = 0;
    path(plugins, sizeof(plugins), contents, "PlugIns");
    path(qml, sizeof(qml), contents, "Resources/qml");
    path(frameworks, sizeof(frameworks), contents, "Frameworks");
    path(jolt, sizeof(jolt), contents, "Resources/glimmer-jolt");
    path(clip, sizeof(clip), contents, "Resources/smoke.mp4");
    if (setenv("QT_PLUGIN_PATH", plugins, 1) || setenv("QML_IMPORT_PATH", qml, 1) ||
        setenv("DYLD_LIBRARY_PATH", frameworks, 1) ||
        setenv("QT_QUICK_CONTROLS_STYLE", "Material", 1) ||
        setenv("QT_MEDIA_BACKEND", "ffmpeg", 1) || unsetenv("DYLD_FRAMEWORK_PATH")) {
        perror("Glimmer Experiment environment");
        return 1;
    }
    if (argc == 1) {
        char *args[] = {jolt, clip, NULL};
        execv(jolt, args);
    } else if (argc == 2 && strcmp(argv[1], "--smoke-test") == 0) {
        char *args[] = {jolt, "--smoke-test", clip, NULL};
        execv(jolt, args);
    } else {
        fputs("Usage: GlimmerExperiment [--smoke-test]\n", stderr);
        return 2;
    }
    perror("Glimmer Experiment Jolt runtime");
    return 1;
}
