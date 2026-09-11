#pragma once

#include <QUndoCommand>
#include <QString>
#include <functional>

#include "Utils/Log.h"

class GenericCommand : public QUndoCommand {
public:
    GenericCommand(std::function<void()> doFn,
                   std::function<void()> undoFn,
                   const QString& text = "",
                   const QString& mergeKey = "",
                   bool affectsStructure = true,
                   QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent),
          m_doFn(std::move(doFn)),
          m_undoFn(std::move(undoFn)),
          m_mergeKey(mergeKey),
          m_affectsStructure(affectsStructure)
    {
    }

    void undo() override {
        Log::info("Undo: " + text());
        if (m_undoFn) {
            m_undoFn();
        }
    }

    void redo() override {
        Log::info(QString(m_hasRun ? "Redo: " : "Do: ") + text());
        m_hasRun = true;
        if (m_doFn) {
            m_doFn();
        }
    }

    // True when the command changes the assembly/MJCF (attach/detach, snap
    // angle, ...) and the editor must refresh + the simulation must reload.
    // False for live-synced value edits (joint targets) -- EditorWindow uses
    // this to skip pointless rebuilds that would steal widget focus.
    bool affectsStructure() const { return m_affectsStructure; }

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
    bool m_affectsStructure;
    bool m_hasRun = false;
};
