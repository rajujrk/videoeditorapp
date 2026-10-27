#pragma once

#include <optional>
#include <string>

namespace tve {

class Project;

// A reversible edit. Every change to a Project goes through a Command so it can be
// undone, redone and journaled.
class Command {
public:
    virtual ~Command() = default;

    virtual std::string label() const = 0;

    // Applies the edit. If the edit is invalid, returns an error message and leaves
    // the project unchanged.
    virtual std::optional<std::string> redo(Project& project) = 0;

    // Reverts a successful redo(). Only called with the project in the state redo() left it.
    virtual void undo(Project& project) = 0;
};

} // namespace tve
