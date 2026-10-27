// Copyright (C) 2022 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include <windows.h>

#include "win/main.hpp"

#undef min
#undef max
#undef interface
#include <GLFW/glfw3.h>
#include <shellapi.h>
#include <time.h>
#include <future>
#include <thread>
#include <pn/output>
#include <pn/string>
#include <sfz/sfz.hpp>

#include "config/dirs.hpp"
#include "config/file-prefs-driver.hpp"
#include "config/ledger.hpp"
#include "config/plugins.hpp"
#include "config/preferences.hpp"
#include "data/extractor.hpp"
#include "game/sys.hpp"
#include "glfw/video-driver.hpp"
#include "ui/flows/master.hpp"

#ifdef _MSC_VER
#include "sound/xaudio2-driver.hpp"
#else
#include "sound/driver.hpp"
#endif

using sfz::range;

namespace args = sfz::args;

namespace antares {
namespace {

pn::string_view default_config_path() {
    static pn::string path = pn::format("{0}/config.pn", dirs().root);
    return path;
}

void usage(pn::output_view out, pn::string_view progname, int retcode) {
    out.format(
            "usage: {0} [OPTIONS] [scenario]\n"
            "\n"
            "  Antares: a tactical space combat game\n"
            "\n"
            "  arguments:\n"
            "    scenario            path to plugin file (default: factory scenario)\n"
            "\n"
            "  options:\n"
            "    -a, --app-data      set path to application data\n"
            "                        (default: {1})\n"
            "    -c, --config        set path to config file\n"
            "                        (default: {2})\n"
            "        --console       allocate console\n"
            "    -f, --factory       set path to factory scenario\n"
            "                        (default: {3})\n"
            "    -p, --plugins       also list plugins found in this directory\n"
            "                        (may be repeated; {4} is always searched)\n"
            "    -h, --help          display this help screen\n"
            "        --host          host a net game\n"
            "        --join HOST     join a net game\n"
            "        --port PORT     set the net game port (default: {5})\n",
            progname, default_application_path(), default_config_path(),
            default_factory_scenario_path(), dirs().scenarios, net::kDefaultPort);
    exit(retcode);
}

class NullStatusObserver : public DataExtractor::Observer {
  public:
    virtual void status(pn::string_view) {}
};

void extract_data() {
    DataExtractor extractor(dirs().downloads, dirs().scenarios);
    if (extractor.current()) {
        return;
    }
    NullStatusObserver observer;
    extractor.extract(&observer);
}

// The Windows keys are fire keys, but Windows opens the Start menu when one is pressed and
// released alone, taking the game out of the foreground. While a window of ours has the
// keyboard, take them from Windows and hand them to that window, where GLFW reads them as
// GLFW_KEY_LEFT_SUPER and GLFW_KEY_RIGHT_SUPER. (Linux and macOS leave this to the desktop.)
static const UINT    kWindowsKeyMessage = WM_APP + 1;  // like WM_KEYDOWN or WM_KEYUP
static const wchar_t kGlfwWindowProc[]  = L"Antares GLFW window procedure";

// Each frame, GLFW releases any Windows key that GetKeyState() says is up, which it always would
// be, since Windows never saw it go down. So mark it in the window thread's key state first.
LRESULT CALLBACK
windows_key_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WNDPROC glfw_proc = reinterpret_cast<WNDPROC>(GetPropW(window, kGlfwWindowProc));
    if (message == kWindowsKeyMessage) {
        const bool up = HIWORD(lparam) & KF_UP;
        BYTE       keys[256];
        if (GetKeyboardState(keys)) {
            keys[wparam] = up ? 0x00 : 0x80;
            SetKeyboardState(keys);
        }
        message = up ? WM_KEYUP : WM_KEYDOWN;
    }
    return CallWindowProcW(glfw_proc, window, message, wparam, lparam);
}

// The foreground window, if it's ours.
HWND our_foreground_window() {
    HWND  window = GetForegroundWindow();
    DWORD pid    = 0;
    GetWindowThreadProcessId(window, &pid);
    return (pid == GetCurrentProcessId()) ? window : nullptr;
}

LRESULT CALLBACK windows_key_hook(int code, WPARAM wparam, LPARAM lparam) {
    // The window each Windows key went to, while it's down, so its release goes there too.
    static HWND held[2] = {nullptr, nullptr};
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if ((k->vkCode == VK_LWIN) || (k->vkCode == VK_RWIN)) {
            HWND&      held_window = held[k->vkCode == VK_RWIN];
            const bool up          = k->flags & LLKHF_UP;
            HWND       window      = up ? held_window : our_foreground_window();
            if (window) {
                held_window = up ? nullptr : window;
                if (!GetPropW(window, kGlfwWindowProc)) {
                    SetPropW(
                            window, kGlfwWindowProc,
                            reinterpret_cast<HANDLE>(GetWindowLongPtrW(window, GWLP_WNDPROC)));
                    SetWindowLongPtrW(
                            window, GWLP_WNDPROC,
                            reinterpret_cast<LONG_PTR>(windows_key_window_proc));
                }
                WORD flags = static_cast<WORD>(k->scanCode & 0xff);
                if (k->flags & LLKHF_EXTENDED) {
                    flags |= KF_EXTENDED;
                }
                if (up) {
                    flags |= KF_REPEAT | KF_UP;
                }
                PostMessageW(window, kWindowsKeyMessage, k->vkCode, MAKELPARAM(1, flags));
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

// Runs windows_key_hook on a thread of its own. Windows silently removes a low-level hook that
// doesn't answer within about a second, which the game's own thread can't promise: it doesn't
// read messages while it's busy, such as when loading a level.
class WindowsKeyHook {
  public:
    WindowsKeyHook() {
        std::promise<DWORD> started;
        std::future<DWORD> thread_id = started.get_future();
        _thread = std::thread([&started] {
            MSG msg;
            PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // creates this thread's queue
            HHOOK hook = SetWindowsHookExW(
                    WH_KEYBOARD_LL, windows_key_hook, GetModuleHandleW(nullptr), 0);
            started.set_value(GetCurrentThreadId());
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                DispatchMessageW(&msg);
            }
            if (hook) {
                UnhookWindowsHookEx(hook);
            }
        });
        _thread_id = thread_id.get();
    }

    ~WindowsKeyHook() {
        PostThreadMessageW(_thread_id, WM_QUIT, 0, 0);
        _thread.join();
    }

  private:
    std::thread _thread;
    DWORD       _thread_id;
};

// Quotes an argument so that CommandLineToArgvW() reads it back unchanged.
std::wstring quote_argument(pn::string_view arg) {
    std::wstring in          = arg.cpp_wstr();
    std::wstring out         = L"\"";
    int          backslashes = 0;
    for (wchar_t c : in) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes + 1, L'\\');
            backslashes = 0;
        } else {
            backslashes = 0;
        }
        out += c;
    }
    out.append(backslashes, L'\\');
    return out + L"\"";
}

// Windows has no exec(), so run the new game as a child and stand in for it until it exits: it
// gets this process's output, and whatever started Antares (a launcher, a shell) sees it run on
// and gets its exit status, as with execvp() on Linux and macOS.
[[noreturn]] void restart(pn::string_view program, const std::vector<pn::string>& arguments) {
    std::wstring command_line = quote_argument(program);
    for (const pn::string& arg : arguments) {
        command_line += L" " + quote_argument(arg);
    }
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        throw std::runtime_error(
                pn::format("GetModuleFileName: error {0}", static_cast<int64_t>(GetLastError()))
                        .c_str());
    }
    STARTUPINFOW startup_info{sizeof(STARTUPINFOW)};
    startup_info.dwFlags    = STARTF_USESTDHANDLES;
    startup_info.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup_info.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process_info;
    if (!CreateProcessW(
                path, &command_line[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                &startup_info, &process_info)) {
        throw std::runtime_error(pn::format(
                                         "CreateProcess: {0}: error {1}", program,
                                         static_cast<int64_t>(GetLastError()))
                                         .c_str());
    }
    CloseHandle(process_info.hThread);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);
    exit(static_cast<int>(exit_code));
}

}  // namespace

void main(int argc, char* const* argv) {
    pn::string_view progname = sfz::path::basename(argv[0]);

    args::callbacks callbacks;

    sfz::optional<pn::string_view> scenario;
    callbacks.argument = [&scenario](pn::string_view arg) {
        if (!scenario.has_value()) {
            scenario.emplace(arg);
        } else {
            return false;
        }
        return true;
    };

    sfz::optional<NetStart> net;
    sfz::optional<int64_t>  port;

    pn::string_view config_path = default_config_path();
    // Options to pass on when restarting with another plugin.
    std::vector<pn::string> restart_options;
    callbacks.short_option = [&progname, &config_path, &restart_options](
                                     pn::rune opt, const args::callbacks::get_value_f& get_value) {
        pn::string_view value;
        switch (opt.value()) {
            case 'a': set_application_path(value = get_value()); break;
            case 'c': config_path = value = get_value(); break;
            case 'f': set_factory_scenario_path(value = get_value()); break;
            case 'p': add_plugin_directory(value = get_value()); break;
            case 'h': usage(pn::out, progname, 0); return true;
            default: return false;
        }
        restart_options.push_back(pn::format("-{0}", opt));
        restart_options.push_back(value.copy());
        return true;
    };

    callbacks.long_option =
            [&](pn::string_view opt, const args::callbacks::get_value_f& get_value) {
                if (opt == "app-data") {
                    return callbacks.short_option(pn::rune{'a'}, get_value);
                } else if (opt == "config") {
                    return callbacks.short_option(pn::rune{'c'}, get_value);
                } else if (opt == "factory-scenario") {
                    return callbacks.short_option(pn::rune{'f'}, get_value);
                } else if (opt == "plugins") {
                    return callbacks.short_option(pn::rune{'p'}, get_value);
                } else if (opt == "help") {
                    return callbacks.short_option(pn::rune{'h'}, get_value);
                } else if (opt == "console") {
                    FILE* f;
                    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
                        AllocConsole();
                    }
                    freopen_s(&f, "CONIN$", "r", stdin);
                    freopen_s(&f, "CONOUT$", "w", stdout);
                    freopen_s(&f, "CONOUT$", "w", stderr);
                    return true;
                } else if (opt == "host") {
                    net.emplace();
                    net->mode = NetSetupScreen::HOST;
                    return true;
                } else if (opt == "join") {
                    net.emplace();
                    net->mode    = NetSetupScreen::JOIN;
                    net->address = get_value().copy();
                    return true;
                } else if (opt == "port") {
                    int64_t p;
                    if (!pn::strtoll(get_value(), &p, nullptr) || (p <= 0) || (p > 65535)) {
                        return false;
                    }
                    port.emplace(p);
                    return true;
                } else {
                    return false;
                }
            };

    args::parse(argc - 1, argv + 1, callbacks);
    if (net.has_value() && port.has_value()) {
        net->port = *port;
    }

    if (!sfz::path::isdir(application_path())) {
        throw std::runtime_error(
                "Application data not found."
                " Please keep Antares next to the data folder.");
    }

    extract_data();

    {
        FilePrefsDriver prefs(config_path);

        DirectoryLedger ledger;
#ifdef _MSC_VER
        XAudio2SoundDriver sound;
#else
        NullSoundDriver sound;
#endif
        GLFWVideoDriver video;
        enable_plugin_switching();
        WindowsKeyHook  windows_keys;
        video.loop(new Master(scenario, time(nullptr), std::move(net)));
    }

    if (plugin_requested()) {
        // Start over with the plugin chosen on the plugins screen, keeping the other options.
        if (requested_plugin().has_value()) {
            restart_options.push_back(requested_plugin()->copy());
        }
        restart(argv[0], restart_options);
    }
}

}  // namespace antares
