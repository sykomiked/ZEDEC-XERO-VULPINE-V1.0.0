/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_launch_win.c — ZXV.exe, the Windows app's launcher (GUI subsystem).
 *
 * Starts zxvd.exe from its own folder with --desktop and no console
 * window, its output in %LOCALAPPDATA%\ZXV\logs\engine.log. zxvd then
 * serves its control socket (%LOCALAPPDATA%\ZXV\run\zxvd.sock) and opens
 * the window in the default browser; when ZXV is already running, zxvd
 * opens that one's window instead and exits. The launcher waits a few
 * seconds: if zxvd stops with an error in that time, it says so, with the
 * log's path. Quitting is in the window (or zxvd.shutdown over the socket).
 * Arguments are passed through to zxvd (for example --model FILE).
 *
 *   x86_64-w64-mingw32-gcc -municode -mwindows zxv_launch_win.c -lshell32 -o ZXV.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h> /* CommandLineToArgvW */
#include <wchar.h>

#define CMD_MAX 32768

static void say(const wchar_t *msg)
{
    MessageBoxW(NULL, msg, L"ZXV", MB_OK | MB_ICONERROR);
}

/* Append one argument with Windows' command-line quoting rules. */
static int append_arg(wchar_t *cmd, size_t cap, size_t *n, const wchar_t *arg)
{
    size_t len = wcslen(arg);
    if (*n + 2 * len + 4 >= cap) return -1;
    cmd[(*n)++] = L' ';
    cmd[(*n)++] = L'"';
    for (size_t i = 0; i < len;) {
        size_t bs = 0;
        while (i < len && arg[i] == L'\\') {
            bs++;
            i++;
        }
        if (i == len) {
            for (size_t k = 0; k < 2 * bs; k++) cmd[(*n)++] = L'\\';
        } else if (arg[i] == L'"') {
            for (size_t k = 0; k < 2 * bs + 1; k++) cmd[(*n)++] = L'\\';
            cmd[(*n)++] = L'"';
            i++;
        } else {
            for (size_t k = 0; k < bs; k++) cmd[(*n)++] = L'\\';
            cmd[(*n)++] = arg[i++];
        }
        if (*n + 4 >= cap) return -1;
    }
    cmd[(*n)++] = L'"';
    cmd[*n] = 0;
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR line, int show)
{
    (void) inst;
    (void) prev;
    (void) line;
    (void) show;
    static wchar_t self[MAX_PATH], engine[MAX_PATH], logs[MAX_PATH], log[MAX_PATH], cmd[CMD_MAX];
    DWORD k = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (k == 0 || k >= MAX_PATH) return 1;
    wchar_t *slash = wcsrchr(self, L'\\');
    if (!slash) return 1;
    *slash = 0;
    if (_snwprintf(engine, MAX_PATH, L"%ls\\zxvd.exe", self) < 0) return 1;

    /* the log: %LOCALAPPDATA%\ZXV\logs\engine.log (the folder is created
     * with the user's default permissions; it holds no secret) */
    log[0] = 0;
    DWORD la = GetEnvironmentVariableW(L"LOCALAPPDATA", logs, MAX_PATH - 32);
    if (la > 0 && la < MAX_PATH - 32) {
        wcscat(logs, L"\\ZXV");
        CreateDirectoryW(logs, NULL);
        wcscat(logs, L"\\logs");
        CreateDirectoryW(logs, NULL);
        _snwprintf(log, MAX_PATH, L"%ls\\engine.log", logs);
    }

    size_t n = 0;
    cmd[0] = 0;
    if (append_arg(cmd, CMD_MAX, &n, engine) != 0 ||
        append_arg(cmd, CMD_MAX, &n, L"--desktop") != 0)
        return 1;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++)
        if (append_arg(cmd, CMD_MAX, &n, argv[i]) != 0) {
            say(L"The command line is too long.");
            return 1;
        }
    if (argv) LocalFree(argv);

    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE}; /* the child inherits the log */
    HANDLE out = INVALID_HANDLE_VALUE;
    if (log[0])
        out = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    ZeroMemory(&pi, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = out;
    si.hStdError = out;
    BOOL ok = CreateProcessW(engine, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, self, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        say(L"ZXV could not start its engine (zxvd.exe). Keep ZXV.exe, zxvd.exe and "
            L"libzxv.dll together in one folder.");
        return 1;
    }
    CloseHandle(pi.hThread);
    DWORD code = STILL_ACTIVE;
    if (WaitForSingleObject(pi.hProcess, 4000) == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != STILL_ACTIVE && code != 0) {
        wchar_t msg[MAX_PATH + 200];
        _snwprintf(msg, MAX_PATH + 200,
                   L"ZXV's engine stopped at start (exit code %lu).\n\nThe reason is in %ls",
                   (unsigned long) code, log[0] ? log : L"its output");
        say(msg);
        return 1;
    }
    return 0;
}
