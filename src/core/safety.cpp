#include "core/safety.h"

#include <windows.h>

#include <algorithm>

#include "core/process.h"
#include "core/util.h"

namespace hx {

namespace {

// All entries lower-case. Executables of multiplayer / competitive games that use anti-cheat or where cheating harms others.
const std::vector<std::string> kBlockedGames = {
    "valorant.exe", "valorant-win64-shipping.exe", "cs2.exe", "csgo.exe", "fortniteclient-win64-shipping.exe",
    "fortnitelauncher.exe", "r5apex.exe", "r5apex_dx12.exe", "rainbowsix.exe", "rainbowsix_vulkan.exe", "rainbowsixgame.exe",
    "tslgame.exe", "pubg.exe", "overwatch.exe", "league of legends.exe", "leagueclient.exe", "dota2.exe", "destiny2.exe",
    "cod.exe", "modernwarfare.exe", "blackopscoldwar.exe", "codbo6.exe", "bf2042.exe", "bfv.exe", "bf1.exe", "bf4.exe", "bf3.exe",
    "rocketleague.exe", "fallguys_client_game.exe", "escapefromtarkov.exe", "eft.exe", "thefinals.exe", "marvel-win64-shipping.exe",
    "deadbydaylight-win64-shipping.exe", "paladins.exe", "smite.exe", "warframe.x64.exe", "apexlegends.exe", "hunt.exe",
    "squad_be.exe", "hell let loose.exe", "hllgame-win64-shipping.exe", "wowclassic.exe", "wow.exe", "gta5.exe", "gtav.exe",
    "playgtav.exe", "fivem.exe", "ffxiv_dx11.exe", "gw2-64.exe", "lostark.exe", "palia.exe", "worldofwarships.exe",
    "worldoftanks.exe", "genshinimpact.exe", "yuanshen.exe", "starrail.exe", "zenlesszonezero.exe", "naraka.exe",
};

const std::vector<std::string> kAcProcesses = {
    "easyanticheat.exe", "easyanticheat_eos.exe", "easyanticheat_launcher.exe", "eac_launcher.exe", "eaclauncher.exe",
    "beservice.exe", "beservice_x64.exe", "battleye.exe", "beclient.exe",
    "vgc.exe", "vgtray.exe", "riotclientservices.exe",
    "faceit.exe", "faceitclient.exe", "faceitservice.exe", "faceit-ac.exe",
    "pnkbstra.exe", "pnkbstrb.exe", "pbsvc.exe", "punkbuster.exe",
    "xigncode.exe", "xhunter1.exe", "x3.xem",
    "gameguard.des", "gamemon.des", "gamemon64.des", "npggsvc.exe", "ggsvc.exe",
    "eaanticheat.gameservice.exe", "eaanticheat.installer.exe",
    "sguard64.exe", "sguardsvc64.exe", "ace-tray.exe", "tp3helper.exe", "tersafe.exe",
    "esea.exe", "eseaclient.exe", "ricochet.exe",
};

const std::vector<std::string> kAcModules = {
    "easyanticheat.dll", "easyanticheat_x64.dll", "easyanticheat_x86.dll", "easyanticheat_eos.dll",
    "beclient.dll", "beclient_x64.dll", "battleye.dll",
    "xigncode.dll", "x3.xem", "gameguard.des", "gamemon.des", "gamemon64.des", "npggnt.des",
    "pbcl.dll", "pbag.dll", "eaanticheat.dll", "ace-base.dll", "tersafe.dll", "vgc.dll",
};

const std::vector<std::string> kAcServices = {
    "easyanticheat", "easyanticheat_eos", "easyanticheatsys", "easyanticheat_eossys", "beservice", "bedaisy",
    "vgk", "vgc", "faceit", "faceitservice", "randgrid", "atvi-randgrid_sr", "atvi-randgrid_msstore",
    "xhunter1", "nppt", "npggsvc", "mhyprot2", "mhyprot3", "eseadriver2", "pnkbstrk", "pnkbstra", "pnkbstrb",
    "eaanticheat", "eaanticheat.gameservice", "aceservice", "acebase", "tp3helper", "tersafe", "sguard64", "neacsafe64", "neac",
};

bool contains(const std::vector<std::string>& list, const std::string& lowered) {
    return std::find(list.begin(), list.end(), lowered) != list.end();
}

}  // namespace

const std::vector<std::string>& blocked_game_exes() { return kBlockedGames; }
const std::vector<std::string>& anticheat_process_names() { return kAcProcesses; }
const std::vector<std::string>& anticheat_module_names() { return kAcModules; }
const std::vector<std::string>& anticheat_service_names() { return kAcServices; }

SafetyVerdict evaluate_safety(std::string_view target, const std::vector<std::string>& modules,
                              const std::vector<std::string>& processes, const std::vector<std::string>& services) {
    SafetyVerdict v;
    std::string t = lower(target);

    if (contains(kBlockedGames, t)) {
        v.allowed = false;
        v.matches.push_back(t);
        v.reason = "\"" + std::string(target) + "\" is a multiplayer or competitive game. Hexwright only works with single-player "
                   "games, so it will not attach.";
        return v;
    }
    for (auto& m : modules) {
        std::string lm = lower(m);
        if (contains(kAcModules, lm)) v.matches.push_back(lm);
    }
    if (!v.matches.empty()) {
        v.allowed = false;
        v.reason = "This game has an anti-cheat component loaded (" + v.matches.front() + "). Hexwright does not attach to "
                   "games protected by anti-cheat.";
        return v;
    }
    for (auto& p : processes) {
        std::string lp = lower(p);
        if (contains(kAcProcesses, lp) && std::find(v.matches.begin(), v.matches.end(), lp) == v.matches.end()) v.matches.push_back(lp);
    }
    for (auto& s : services) {
        std::string ls = lower(s);
        if (contains(kAcServices, ls) && std::find(v.matches.begin(), v.matches.end(), ls) == v.matches.end()) v.matches.push_back(ls);
    }
    if (!v.matches.empty()) {
        v.allowed = false;
        v.reason = "An anti-cheat is running on this PC (" + v.matches.front() + (v.matches.size() > 1 ? " and others" : "") +
                   "). To keep you safe, Hexwright refuses to attach to any game while one is active. Close the game or "
                   "program that started it (for kernel anti-cheats like Vanguard, exit them first) and try again.";
    }
    return v;
}

std::vector<std::string> running_service_names() {
    std::vector<std::string> out;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return out;
    DWORD needed = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_ACTIVE, nullptr, 0, &needed, &count, &resume, nullptr);
    std::vector<uint8_t> buf(needed + 4096);
    resume = 0;
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_ACTIVE, buf.data(), (DWORD)buf.size(),
                              &needed, &count, &resume, nullptr)) {
        auto* e = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
        for (DWORD i = 0; i < count; ++i) out.push_back(narrow(e[i].lpServiceName));
    }
    CloseServiceHandle(scm);
    return out;
}

SafetyVerdict check_attach(uint32_t pid, std::string_view target_name) {
    std::vector<std::string> modules, procs;
    for (auto& m : list_modules(pid)) modules.push_back(m.name);
    for (auto& p : list_processes()) procs.push_back(p.name);
    return evaluate_safety(target_name, modules, procs, running_service_names());
}

}  // namespace hx
