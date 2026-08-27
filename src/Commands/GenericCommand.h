#pragma once

#include <QUndoCommand>
#include <QString>
#include <functional>

class GenericCommand : public QUndoCommand {
public:
    GenericCommand(std::function<void()> doFn, 
                   std::function<void()> undoFn, 
                   const QString& text = "", 
                   const QString& mergeKey = "", 
                   QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent), 
          m_doFn(std::move(doFn)), 
          m_undoFn(std::move(undoFn)), 
          m_mergeKey(mergeKey) 
    {
    }

    void undo() override {
        if (m_undoFn) {
            m_undoFn();
        }
    }

    void redo() override {
        if (m_doFn) {
            m_doFn();
        }
    }

    // Returning -1 means the command will never merge. 
    // Returning a positive ID enables the mergeWith() function.
    int id() const override {
        return m_mergeKey.isEmpty() ? -1 : 1337; // Arbitrary ID for GenericCommands
    }

    // Handles merging rapid successive commands (e.g., slider drags)
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        
        const GenericCommand* otherCmd = static_cast<const GenericCommand*>(other);
        if (m_mergeKey != otherCmd->m_mergeKey) return false;

        // CRITICAL: For a continuous action (like a slider drag), we keep our ORIGINAL undo action 
        // (which returns to the very first value) but adopt the NEWEST do action (the latest value).
        m_doFn = otherCmd->m_doFn;
        
        return true;
    }

private:
    std::function<void()> m_doFn;
    std::function<void()> m_undoFn;
    QString m_mergeKey;
};