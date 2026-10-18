#include "core/session.h"

#include "core/hash.h"
#include "core/safety.h"

namespace hx {

Status Session::attach(uint32_t pid) { return open(pid, true); }
Status Session::attach_unchecked(uint32_t pid) { return open(pid, false); }

Status Session::open(uint32_t pid, bool check) {
    detach();
    std::string name;
    for (auto& p : list_processes())
        if (p.pid == pid) { name = p.name; break; }
    if (name.empty()) return Error{"That process is no longer running."};
    if (check) {
        SafetyVerdict v = check_attach(pid, name);
        if (!v.allowed) return Error{v.reason};
    }
    auto p = Process::open(pid);
    if (!p.ok()) return p.error();
    auto proc = std::make_shared<Process>(std::move(*p));
    Info info;
    info.pid = pid;
    info.name = proc->name();
    info.image = proc->image_path();
    info.is64 = proc->is64();
    if (auto m = proc->main_module()) { info.base = m->base; info.size = m->size; }
    if (!info.image.empty()) info.exe_sha256 = sha256_file(info.image).value_or("");
    proc_ = std::move(proc);
    info_ = std::move(info);
    return ok_status();
}

void Session::detach() {
    proc_.reset();
    info_.reset();
}

bool Session::alive() {
    if (proc_ && !proc_->alive()) detach();
    return proc_ != nullptr;
}

}  // namespace hx
