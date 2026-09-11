#pragma once

#include <QUndoCommand>
#include <QString>

#include "Application/Application.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "Simulation/ErrorSystem/Emulator.h" // complete type needed for `delete emulator`
#include "Utils/Log.h"


/*
 * Undoable "permanently delete a component and its whole subtree" action
 * (the cross button on an attached connector -> "Delete Permanently").
 *
 * Mirror image of AddComponentCommand. Lifetime rule: while the command is
 * in the done state, the command owns the subtree (Project has fully
 * unlinked it); while it is in the undone state, the Project owns it again.
 * The destructor frees the subtree -- instances and their unparented
 * emulators -- only if it is unlinked at that point (stack cleared or
 * command dropped past the undo limit).
 *
 * For simply detaching without deleting, use a plain Commands::push() that
 * flips parentUid/parentConnector -- the instance stays in the project as
 * an orphan and no ownership transfer is needed.
 */
class RemoveComponentCommand : public QUndoCommand {
public:
    RemoveComponentCommand(Project* project, int uid, QUndoCommand* parent = nullptr)
        : QUndoCommand("Delete component", parent),
          m_project(project),
          m_uid(uid)
    {
    }

    ~RemoveComponentCommand() override {
        if (m_root && m_taken) {
            deleteSubtree(m_root);
        }
    }

    void redo() override {
        if (!m_project) return;

        if (!m_root) {
            // First execution: remember the external link before takeSubtree clears it.
            Log::info("Do: " + text());
            ComponentInstance* comp = m_project->getComponentByUid(m_uid);
            if (!comp) return;
            m_parentUid = comp->parentUid;
            m_parentConnector = comp->parentConnector;
            m_root = m_project->takeSubtree(m_uid);
            if (!m_root) return;
        } else {
            // Redo after an undo: unlink the same subtree again (keeps uids + emulators).
            Log::info("Redo: " + text());
            m_project->takeSubtree(m_root->uid);
        }
        m_taken = true;
        Application::getInstance()->reloadSimulation();
    }

    void undo() override {
        if (!m_project || !m_root) return;

        Log::info("Undo: " + text());
        // takeComponent() cleared the root's parent info, so restore it first.
        m_root->parentUid = m_parentUid;
        m_root->parentConnector = m_parentConnector;
        m_project->adoptSubtree(m_root);
        m_taken = false;
        Application::getInstance()->reloadSimulation();
    }

private:
    static void deleteSubtree(ComponentInstance* comp) {
        for (ComponentInstance* child : comp->children) {
            deleteSubtree(child);
        }
        delete comp->emulator; // emulators are unparented heap QObjects
        delete comp;
    }

    Project* m_project;
    int m_uid;
    int m_parentUid = -1;
    QString m_parentConnector;

    ComponentInstance* m_root = nullptr;
    bool m_taken = false;
};
