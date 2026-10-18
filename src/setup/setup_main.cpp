// Hexwright setup / launcher / uninstaller: one exe, Squirrel-style layout.
//
//   %LOCALAPPDATA%\Hexwright\
//       Hexwright.exe          <- a copy of this binary; run from here it launches the newest app-X
//       app-2.0.0\Hexwright.exe  <- the real application (one folder per version)
//       app.ico
//
// Run from anywhere else it installs. Flags: --silent, --update, --wait-pid N, --no-launch, --uninstall (installed copy only).
// Test overrides: HX_INSTALL_DIR (install somewhere else), HX_NO_SHELL (no shortcuts / registry).
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "core/catalog.h"
#include "core/util.h"
#include "hx/version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")

using namespace hx;

namespace {

const wchar_t* kApp = L"Hexwright";
const wchar_t* kRegKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Hexwright";
const wchar_t* kOldRegKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WandLike";

fs::path install_dir() {
    std::string o = env_var("HX_INSTALL_DIR");
    return o.empty() ? local_app_data() / kApp : fs::path(widen(o));
}
bool skip_shell() { return !env_var("HX_NO_SHELL").empty(); }

std::wstring version_w() { return widen(HX_VERSION_STRING); }

// ------------------------------------------------------------------ payload
std::string load_payload() {
    HRSRC r = FindResourceW(nullptr, L"HEXWRIGHT_EXE", reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (!r) return {};
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return {};
    return std::string(static_cast<const char*>(LockResource(g)), SizeofResource(nullptr, r));
}

std::string load_icon_payload() {
    HRSRC r = FindResourceW(nullptr, L"HEXWRIGHT_ICO", reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (!r) return {};
    HGLOBAL g = LoadResource(nullptr, r);
    return g ? std::string(static_cast<const char*>(LockResource(g)), SizeofResource(nullptr, r)) : std::string();
}

bool write_file(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return write_file_atomic(p, data);
}

// ------------------------------------------------------------------- shell
fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR w = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w))) p = w;
    CoTaskMemFree(w);
    return p;
}
fs::path start_menu_lnk(const wchar_t* name) { return known_folder(FOLDERID_Programs) / (std::wstring(name) + L".lnk"); }
fs::path desktop_lnk(const wchar_t* name) { return known_folder(FOLDERID_Desktop) / (std::wstring(name) + L".lnk"); }

bool make_shortcut(const fs::path& lnk, const fs::path& target, const fs::path& icon) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(target.parent_path().c_str());
    link->SetIconLocation(icon.c_str(), 0);
    link->SetDescription(L"Hexwright");
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
        std::error_code ec;
        fs::create_directories(lnk.parent_path(), ec);
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

void set_reg_sz(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

void register_uninstall(const fs::path& root, const std::wstring& version) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    set_reg_sz(k, L"DisplayName", kApp);
    set_reg_sz(k, L"DisplayVersion", version);
    set_reg_sz(k, L"Publisher", kApp);
    set_reg_sz(k, L"InstallLocation", root.wstring());
    set_reg_sz(k, L"DisplayIcon", (root / L"app.ico").wstring());
    set_reg_sz(k, L"UninstallString", L"\"" + (root / L"Hexwright.exe").wstring() + L"\" --uninstall");
    DWORD one = 1;
    RegSetValueExW(k, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
    RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
    RegCloseKey(k);
}

// ---------------------------------------------------------------- process
void wait_for_pid(DWORD pid, DWORD timeout_ms = 40000) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return;
    WaitForSingleObject(h, timeout_ms);
    CloseHandle(h);
}

void run_detached(const std::wstring& cmdline, const fs::path& cwd = {}, bool hidden = false) {
    STARTUPINFOW si{sizeof si};
    if (hidden) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    if (CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | (hidden ? CREATE_NO_WINDOW : 0), nullptr,
                       cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Deletes a folder (that may contain the running exe) from a helper that outlives us.
void remove_dir_later(const fs::path& dir) {
    fs::path bat = fs::temp_directory_path() / L"hexwright-cleanup.cmd";
    std::string text = "@echo off\r\nping -n 4 127.0.0.1 >nul\r\nrmdir /s /q \"" + narrow(dir.wstring()) + "\"\r\ndel \"%~f0\"\r\n";
    write_file_atomic(bat, text);
    run_detached(L"cmd /c \"" + bat.wstring() + L"\"", {}, true);
}

// ---------------------------------------------------------------- versions
struct AppDir { fs::path dir; std::string version; };

std::vector<AppDir> app_dirs(const fs::path& root) {
    std::vector<AppDir> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec)) {
        std::string n = narrow(e.path().filename().wstring());
        if (n.rfind("app-", 0) != 0 || !fs::exists(e.path() / L"Hexwright.exe", ec)) continue;
        out.push_back({e.path(), n.substr(4)});
    }
    std::sort(out.begin(), out.end(), [](const AppDir& a, const AppDir& b) { return compare_versions(a.version, b.version) < 0; });
    return out;
}

bool launch_newest(const fs::path& root, const std::wstring& args = L"") {
    auto dirs = app_dirs(root);
    if (dirs.empty()) return false;
    fs::path exe = dirs.back().dir / L"Hexwright.exe";
    run_detached(L"\"" + exe.wstring() + L"\"" + (args.empty() ? L"" : L" " + args), exe.parent_path());
    return true;
}

// ---------------------------------------------------------------- migration
// The first release of this app was called WandLike (Python). Move its settings across and remove it.
void migrate_old_install() {
    fs::path old_root = local_app_data() / L"WandLike";
    fs::path old_settings = roaming_app_data() / L"WandLike" / L"settings.json";
    std::error_code ec;
    fs::path new_settings = data_dir() / L"settings.json";
    if (fs::exists(old_settings, ec) && !fs::exists(new_settings, ec)) {
        fs::create_directories(new_settings.parent_path(), ec);
        fs::copy_file(old_settings, new_settings, ec);
    }
    if (skip_shell()) return;
    bool had_old = fs::exists(old_root / L"WandLike.exe", ec);
    if (!had_old) return;
    RegDeleteKeyW(HKEY_CURRENT_USER, kOldRegKey);
    fs::remove(start_menu_lnk(L"WandLike"), ec);
    fs::remove(desktop_lnk(L"WandLike"), ec);
    remove_dir_later(old_root);
}

// ------------------------------------------------------------------ install
using Progress = std::function<void(int, const std::wstring&)>;

bool install(const Progress& progress, bool update, std::wstring& error) {
    fs::path root = install_dir();
    std::string payload = load_payload();
    if (payload.empty()) { error = L"This installer is missing its payload."; return false; }
    std::error_code ec;
    progress(5, L"Preparing...");
    fs::create_directories(root, ec);
    std::wstring version = version_w();

    fs::path target = root / (L"app-" + version);
    fs::path tmp = root / (L".installing-" + version);
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    progress(20, L"Installing Hexwright " + version + L"...");
    if (!write_file(tmp / L"Hexwright.exe", payload)) { error = L"Couldn't write the program files to " + root.wstring(); return false; }
    progress(60, L"Finishing...");
    fs::remove_all(target, ec);
    fs::rename(tmp, target, ec);
    if (ec) { error = L"Couldn't put the new version in place: " + widen(ec.message()); return false; }

    std::string ico = load_icon_payload();
    if (!ico.empty()) write_file(root / L"app.ico", ico);
    // the launcher is a copy of this very exe; if it is in use the old one is identical in practice
    fs::path launcher = root / L"Hexwright.exe";
    fs::path staged = root / L"Hexwright.exe.new";
    fs::copy_file(exe_path(), staged, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        if (!MoveFileExW(staged.c_str(), launcher.c_str(), MOVEFILE_REPLACE_EXISTING)) fs::remove(staged, ec);
    }

    if (!skip_shell()) {
        progress(75, L"Creating shortcuts...");
        fs::path icon = fs::exists(root / L"app.ico", ec) ? root / L"app.ico" : launcher;
        make_shortcut(start_menu_lnk(kApp), launcher, icon);
        if (!update && !fs::exists(desktop_lnk(kApp), ec)) make_shortcut(desktop_lnk(kApp), launcher, icon);
        register_uninstall(root, version);
    }
    if (!update) migrate_old_install();
    else migrate_old_install();    // a 1.x updater runs us with --update; migration is idempotent

    // keep this and the previous version, drop the rest
    auto dirs = app_dirs(root);
    for (size_t i = 0; i + 2 < dirs.size(); ++i) fs::remove_all(dirs[i].dir, ec);
    progress(100, L"Done");
    return true;
}

int uninstall(bool silent) {
    fs::path root = install_dir();
    if (!silent && MessageBoxW(nullptr, L"Remove Hexwright from this computer?\n\nYour settings in %APPDATA%\\Hexwright are kept.", kApp,
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;
    if (!skip_shell()) {
        RegDeleteKeyW(HKEY_CURRENT_USER, kRegKey);
        std::error_code ec;
        fs::remove(start_menu_lnk(kApp), ec);
        fs::remove(desktop_lnk(kApp), ec);
    }
    remove_dir_later(root);
    return 0;
}

// ---------------------------------------------------------------------- GUI
struct GuiState {
    HWND wnd = nullptr, bar = nullptr, text = nullptr;
    std::atomic<bool> done{false};
    std::wstring error;
    bool ok = false;
};

LRESULT CALLBACK gui_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<GuiState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
        case WM_APP + 1:   // progress
            if (st) {
                SendMessageW(st->bar, PBM_SETPOS, w, 0);
                auto* msg = reinterpret_cast<std::wstring*>(l);
                if (msg) { SetWindowTextW(st->text, msg->c_str()); delete msg; }
            }
            return 0;
        case WM_APP + 2:   // finished
            DestroyWindow(h);
            return 0;
        case WM_CLOSE:
            return 0;      // no cancelling halfway through
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(w);
            SetTextColor(dc, RGB(0xe9, 0xeb, 0xf0));
            SetBkColor(dc, RGB(0x0b, 0x0c, 0x10));
            static HBRUSH br = CreateSolidBrush(RGB(0x0b, 0x0c, 0x10));
            return reinterpret_cast<LRESULT>(br);
        }
        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(h, &r);
            static HBRUSH br = CreateSolidBrush(RGB(0x0b, 0x0c, 0x10));
            FillRect(reinterpret_cast<HDC>(w), &r, br);
            return 1;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

int run_gui_install(bool update) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = gui_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"HexwrightSetup";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    GuiState st;
    int w = 420, h = 150;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    st.wnd = CreateWindowW(L"HexwrightSetup", L"Hexwright Setup", WS_CAPTION | WS_SYSMENU, (sx - (r.right - r.left)) / 2, (sy - (r.bottom - r.top)) / 2,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(st.wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
    st.text = CreateWindowW(L"STATIC", L"Starting...", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 34, w - 40, 22, st.wnd, nullptr, wc.hInstance, nullptr);
    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(st.text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    st.bar = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 40, 74, w - 80, 14, st.wnd, nullptr, wc.hInstance, nullptr);
    SendMessageW(st.bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    ShowWindow(st.wnd, SW_SHOW);
    UpdateWindow(st.wnd);

    std::thread worker([&] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        st.ok = install(
            [&](int pct, const std::wstring& msg) { PostMessageW(st.wnd, WM_APP + 1, pct, reinterpret_cast<LPARAM>(new std::wstring(msg))); }, update, st.error);
        CoUninitialize();
        PostMessageW(st.wnd, WM_APP + 2, 0, 0);
    });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    worker.join();
    if (!st.ok) {
        MessageBoxW(nullptr, (L"Installation failed:\n" + st.error).c_str(), L"Hexwright Setup", MB_ICONERROR);
        return 1;
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false, update = false, no_launch = false, un = false;
    DWORD wait_pid = 0;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--silent") silent = true;
        else if (a == L"--update") update = true;
        else if (a == L"--no-launch") no_launch = true;
        else if (a == L"--uninstall") un = true;
        else if (a == L"--wait-pid" && i + 1 < argc) wait_pid = static_cast<DWORD>(_wtoi(argv[++i]));
    }
    LocalFree(argv);

    fs::path root = install_dir();
    std::error_code ec;
    bool from_root = fs::equivalent(exe_path().parent_path(), root, ec);
    if (from_root) {                       // installed launcher mode
        if (un) return uninstall(silent);
        if (!launch_newest(root)) {
            MessageBoxW(nullptr, L"Hexwright isn't installed correctly. Run the installer again.", kApp, MB_ICONERROR);
            return 1;
        }
        return 0;
    }
    if (wait_pid) wait_for_pid(wait_pid);
    int rc = 0;
    if (silent) {
        std::wstring err;
        rc = install([](int, const std::wstring&) {}, update, err) ? 0 : 1;
    } else {
        rc = run_gui_install(update);
    }
    if (rc == 0 && !no_launch) launch_newest(root);
    return rc;
}
