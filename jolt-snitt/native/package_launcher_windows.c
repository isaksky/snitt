/* Windows entry point for the packaged Jolt application and its deployed Qt runtime.
 * jolt build produces a console program; this GUI-subsystem launcher runs it
 * without a console window and stays alive as the `snitt` process the
 * installers look for. Closing the job ends the runtime with the launcher. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

static int child_path(wchar_t *out, size_t size, const wchar_t *directory, const wchar_t *name) {
    int written = _snwprintf(out, size, L"%ls\\%ls", directory, name);
    return written > 0 && (size_t)written < size;
}

/* The command line after argv[0], with the caller's quoting preserved. */
static const wchar_t *arguments_after_program(const wchar_t *line) {
    if (*line == L'"') {
        ++line;
        while (*line && *line != L'"') ++line;
        if (*line) ++line;
    } else {
        while (*line && *line != L' ' && *line != L'\t') ++line;
    }
    while (*line == L' ' || *line == L'\t') ++line;
    return line;
}

static int fail(const wchar_t *message) {
    MessageBoxW(NULL, message, L"Snitt", MB_ICONERROR | MB_OK);
    return 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR ignored, int show) {
    (void)instance; (void)previous; (void)ignored; (void)show;
    wchar_t directory[MAX_PATH], plugins[MAX_PATH], qml[MAX_PATH], jolt[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return fail(L"Snitt could not locate its executable.");
    wchar_t *separator = wcsrchr(directory, L'\\');
    if (!separator) return fail(L"Snitt could not locate its executable.");
    *separator = 0;
    if (!child_path(plugins, MAX_PATH, directory, L"plugins") ||
        !child_path(qml, MAX_PATH, directory, L"qml") ||
        !child_path(jolt, MAX_PATH, directory, L"snitt-jolt.exe"))
        return fail(L"Snitt installation path is too long.");
    if (!SetEnvironmentVariableW(L"QT_PLUGIN_PATH", plugins) ||
        !SetEnvironmentVariableW(L"QML_IMPORT_PATH", qml) ||
        !SetEnvironmentVariableW(L"SNITT_JOLT_PRODUCTION", L"1") ||
        (!GetEnvironmentVariableW(L"SNITT_JOLT_IPC_ID", NULL, 0) &&
         !SetEnvironmentVariableW(L"SNITT_JOLT_IPC_ID", L"production")))
        return fail(L"Snitt could not prepare its environment.");

    const wchar_t *arguments = arguments_after_program(GetCommandLineW());
    size_t size = wcslen(jolt) + wcslen(arguments) + 4;
    wchar_t *command = HeapAlloc(GetProcessHeap(), 0, size * sizeof(wchar_t));
    if (!command) return fail(L"Snitt is out of memory.");
    _snwprintf(command, size, L"\"%ls\" %ls", jolt, arguments);
    command[size - 1] = 0;

    /* Never forward a hidden show state: Windows applies it to the first
     * editor or picker window, which would then stay invisible. */
    STARTUPINFOW startup = {sizeof(startup)};
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE), error = GetStdHandle(STD_ERROR_HANDLE);
    BOOL redirected = output && output != INVALID_HANDLE_VALUE && error && error != INVALID_HANDLE_VALUE;
    if (redirected) {
        /* Keep diagnostics when a caller captured this launcher's output. */
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input == INVALID_HANDLE_VALUE ? NULL : input;
        startup.hStdOutput = output;
        startup.hStdError = error;
        if (startup.hStdInput) SetHandleInformation(startup.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(error, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return fail(L"Snitt could not create its process job.");
    PROCESS_INFORMATION process;
    /* Keep the caller's directory: relative image arguments resolve against it. */
    if (!CreateProcessW(jolt, command, NULL, NULL, redirected, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        NULL, NULL, &startup, &process))
        return fail(L"Snitt could not start its Jolt runtime (snitt-jolt.exe).");
    HeapFree(GetProcessHeap(), 0, command);
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        return fail(L"Snitt could not supervise its Jolt runtime.");
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return (int)code;
}
