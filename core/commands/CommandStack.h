#pragma once

#include "commands/Command.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

class CommandStack {
public:
    explicit CommandStack(Project& project) : m_project(project) {}

    // Executes the command. On success it becomes the newest undo step and any redo
    // history is discarded. On failure the stack and project are unchanged.
    std::optional<std::string> push(std::unique_ptr<Command> command);

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_commands.size(); }
    void undo();
    void redo();

    std::string undoLabel() const;
    std::string redoLabel() const;
    std::size_t count() const { return m_commands.size(); }
    std::size_t index() const { return m_index; }

    // "Clean" marks the state that matches the saved file.
    bool isClean() const { return m_cleanIndex == m_index; }
    void setClean() { m_cleanIndex = m_index; }
    // No state matches a saved file (e.g. a project recovered from an autosave).
    void setUnsaved() { m_cleanIndex.reset(); }

private:
    Project& m_project;
    std::vector<std::unique_ptr<Command>> m_commands;
    std::size_t m_index = 0;
    std::optional<std::size_t> m_cleanIndex = 0;
};

} // namespace tve
