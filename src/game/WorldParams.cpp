#include "game/WorldParams.h"

namespace mc {

std::string_view difficultyName(Difficulty d) {
    switch (d) {
    case Difficulty::Peaceful: return "peaceful";
    case Difficulty::Easy: return "easy";
    case Difficulty::Normal: return "normal";
    case Difficulty::Hard: return "hard";
    }
    return "normal";
}

std::optional<Difficulty> difficultyFromName(std::string_view name) {
    for (Difficulty d : {Difficulty::Peaceful, Difficulty::Easy, Difficulty::Normal, Difficulty::Hard})
        if (difficultyName(d) == name) return d;
    return std::nullopt;
}

} // namespace mc
