/* Bundle entry point for the Jolt application and its deployed Qt runtime. */
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int child_path(char *out, size_t size, const char *contents, const char *name) {
    return snprintf(out, size, "%s/%s", contents, name) < (int)size;
}

int main(int argc, char **argv) {
    char launch_path[PATH_MAX], executable[PATH_MAX], contents[PATH_MAX], plugins[PATH_MAX];
    char qml[PATH_MAX], frameworks[PATH_MAX], jolt[PATH_MAX];
    uint32_t launch_path_size = sizeof(launch_path);
    if (_NSGetExecutablePath(launch_path, &launch_path_size) != 0 ||
        !realpath(launch_path, executable)) {
        perror("Snitt executable");
        return 1;
    }
    char *marker = strstr(executable, "/Contents/MacOS/");
    if (!marker || (size_t)(marker - executable) + strlen("/Contents") >= sizeof(contents)) {
        fputs("Snitt must run from its application bundle.\n", stderr);
        return 1;
    }
    size_t length = (size_t)(marker - executable) + strlen("/Contents");
    memcpy(contents, executable, length);
    contents[length] = 0;
    if (!child_path(plugins, sizeof(plugins), contents, "PlugIns") ||
        !child_path(qml, sizeof(qml), contents, "Resources/qml") ||
        !child_path(frameworks, sizeof(frameworks), contents, "Frameworks") ||
        !child_path(jolt, sizeof(jolt), contents, "MacOS/snitt-jolt")) {
        fputs("Snitt bundle path is too long.\n", stderr);
        return 1;
    }
    if (setenv("QT_PLUGIN_PATH", plugins, 1) ||
        setenv("QML_IMPORT_PATH", qml, 1) ||
        setenv("DYLD_LIBRARY_PATH", frameworks, 1) ||
        setenv("QT_MEDIA_BACKEND", "ffmpeg", 1) ||
        setenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",", 1) ||
        setenv("SNITT_JOLT_PRODUCTION", "1", 1) ||
        setenv("SNITT_JOLT_IPC_ID", "production", 0) ||
        unsetenv("DYLD_FRAMEWORK_PATH")) {
        perror("Snitt environment");
        return 1;
    }
    argv[0] = jolt;
    execv(jolt, argv);
    perror("Snitt Jolt runtime");
    return 1;
}
