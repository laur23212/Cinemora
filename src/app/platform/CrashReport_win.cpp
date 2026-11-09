// Dafin: on Windows, a crash leaves %LOCALAPPDATA%\Dafin\crash\crash-<time>.txt behind: the
// exception, where it happened and the call stack, each frame as module + offset. With the
// dafin.pdb of the same build (the windows-symbols-<arch> artifact) the offsets read back as
// function, file and line, e.g. llvm-symbolizer --obj=dafin.exe 0x140000000+<offset>.
// The last Qt messages before the crash go to dafin-log.txt beside it, rewritten every start.

#include "app/platform/CrashReport.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>

namespace {

wchar_t g_dir[MAX_PATH] = {};
HANDLE g_log = INVALID_HANDLE_VALUE;
std::mutex g_logMutex;
QtMessageHandler g_previousHandler = nullptr;

void writeAll(HANDLE file, const char *text, int length)
{
    DWORD written = 0;
    if (length > 0)
        WriteFile(file, text, DWORD(length), &written, nullptr);
}

void writeLine(HANDLE file, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int n = _vsnprintf_s(buffer, sizeof buffer, _TRUNCATE, format, args);
    va_end(args);
    writeAll(file, buffer, n < 0 ? int(strlen(buffer)) : n);
    writeAll(file, "\r\n", 2);
}

// "Qt6Quick.dll+0x1a2b3" for an address, from the module that holds it.
void describe(HANDLE file, int index, DWORD64 address)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module)
        && module) {
        GetModuleFileNameW(module, path, MAX_PATH);
    }
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, int(sizeof utf8), nullptr, nullptr);
    const DWORD64 offset = module ? address - reinterpret_cast<DWORD64>(module) : address;
    writeLine(file, "  #%02d  %s+0x%llx", index, utf8, static_cast<unsigned long long>(offset));
}

LONG WINAPI onCrash(EXCEPTION_POINTERS *info)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t path[MAX_PATH + 64];
    swprintf_s(path, L"%ls\\crash-%04d%02d%02d-%02d%02d%02d.txt", g_dir, now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;

    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    writeLine(file, "Dafin %s crashed", DRIFT_VERSION);
    writeLine(file, "Exception 0x%08lx at 0x%p, thread %lu", record->ExceptionCode, record->ExceptionAddress,
              GetCurrentThreadId());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        writeLine(file, "%s address 0x%llx", record->ExceptionInformation[0] == 8 ? "Executing"
                                             : record->ExceptionInformation[0] == 1 ? "Writing" : "Reading",
                  static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    writeLine(file, "Stack:");

#if defined(_M_X64)
    CONTEXT context = *info->ContextRecord;
    // A call through a null pointer lands at 0 with the caller's return address on top of the
    // stack: start from there.
    if (context.Rip < 0x10000) {
        describe(file, 0, context.Rip);
        context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
        context.Rsp += 8;
    }
    for (int i = 1; i < 64 && context.Rip; ++i) {
        describe(file, i, context.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) {
            // A leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
            context.Rsp += 8;
            continue;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                         &establisherFrame, nullptr);
    }
#else
    describe(file, 0, reinterpret_cast<DWORD64>(record->ExceptionAddress));
#endif
    CloseHandle(file);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_log != INVALID_HANDLE_VALUE)
            FlushFileBuffers(g_log);
    }
    wchar_t message[MAX_PATH + 256];
    swprintf_s(message, L"Dafin stopped because of an error.\n\nA report was saved here:\n%ls\n\n"
                        L"Please send it, together with dafin-log.txt from the same folder.", path);
    MessageBoxW(nullptr, message, L"Dafin", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    // Windows still records it (Event Viewer) and ends the process.
    return EXCEPTION_CONTINUE_SEARCH;
}

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
        const QByteArray line = QByteArray(kinds[qBound(0, int(type), 4)]) + ": " + message.toUtf8() + "\r\n";
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_log != INVALID_HANDLE_VALUE)
            writeAll(g_log, line.constData(), int(line.size()));
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

} // namespace

namespace dafin {

void installCrashReport()
{
    wchar_t base[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    swprintf_s(g_dir, L"%ls\\Dafin", base);
    CreateDirectoryW(g_dir, nullptr);
    wcscat_s(g_dir, L"\\crash");
    CreateDirectoryW(g_dir, nullptr);

    wchar_t logPath[MAX_PATH + 32];
    swprintf_s(logPath, L"%ls\\dafin-log.txt", g_dir);
    // Write-through, so the lines just before a crash are on disk.
    g_log = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    g_previousHandler = qInstallMessageHandler(onMessage);
    SetUnhandledExceptionFilter(onCrash);
}

} // namespace dafin
