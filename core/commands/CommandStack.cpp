#include "commands/CommandStack.h"

#include <stdexcept>

namespace tve {

std::optional<std::string> CommandStack::push(std::unique_ptr<Command> command)
{
    if (auto error = command->redo(m_project))
        return error;

    m_commands.resize(m_index);
    if (m_cleanIndex && *m_cleanIndex > m_index)
        m_cleanIndex.reset(); // the saved state is no longer reachable
    m_commands.push_back(std::move(command));
    ++m_index;
    return std::nullopt;
}

void CommandStack::undo()
{
    if (!canUndo())
        return;
    --m_index;
    m_commands[m_index]->undo(m_project);
}

void CommandStack::redo()
{
    if (!canRedo())
        return;
    if (auto error = m_commands[m_index]->redo(m_project))
        throw std::logic_error("redo of '" + m_commands[m_index]->label() + "' failed: " + *error);
    ++m_index;
}

std::string CommandStack::undoLabel() const
{
    return canUndo() ? m_commands[m_index - 1]->label() : std::string{};
}

std::string CommandStack::redoLabel() const
{
    return canRedo() ? m_commands[m_index]->label() : std::string{};
}

} // namespace tve
