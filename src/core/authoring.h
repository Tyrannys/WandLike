#pragma once
// Helpers for people writing game definitions: scaffold one from a detected game, and build the catalog index.
#include <string>

#include <nlohmann/json.hpp>

#include "core/library.h"
#include "core/util.h"

namespace hx {

// The game's process name: recorded by Epic/GOG, otherwise the largest non-installer .exe in its folder.
std::string guess_exe(const LibraryGame& g);
std::string slugify(const std::string& name);
// A starter definition (match, exe, console defaults and two placeholder cheats) for a detected game.
nlohmann::json scaffold_definition(const LibraryGame& g);

// wandlike/games/_index.json: id, name, version, file, sha256 (line-ending independent), match, cheats, min_app.
// Returns an error message in `error` (and an empty string) if any definition in the folder is invalid.
std::string build_index_text(const fs::path& games_dir, std::string* error = nullptr);

}  // namespace hx
