#pragma once

#include <QUndoCommand>
#include <QString>

#include "Application/Application.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "Simulation/ErrorSystem/Emulator.h" // complete type needed for `delete emulator`
#include "Utils/Log.h"


/*
 * Undoable "create component and attach it to a connector" action
 * (drag-and-drop from the Component Library onto a connector slot).
 *
 * Lifetime rule: while the command is in the undone state, the command
 * owns the ComponentInstance (Project has fully unlinked it); while it is
 * in the done state, the Project owns it again. The destructor frees the
 * instance -- and its unparented emulator -- only if it is unlinked at
 * that point (stack cleared or command dropped past the undo limit).
 */
class AddComponentCommand : public QUndoCommand {
public:
    AddComponentCommand(Project* project,
                        int parentUid,
                        const QString& parentConnector,
                        const QString& modelId,
                        const QString& selfConnector,
                        float snapAngle,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand("Add component", parent),
          m_project(project),
          m_parentUid(parentUid),
          m_parentConnector(parentConnector),
          m_modelId(modelId),
          m_selfConnector(selfConnector),
          m_snapAngle(snapAngle)
    {
    }

    ~AddComponentCommand() override {
        if (m_instance && !m_linked) {
            delete m_instance->emulator; // emulators are unparented heap QObjects
            delete m_instance;
        }
    }

    void redo() override {
        if (!m_project) return;

        if (!m_instance) {
            // First execution: create a fresh instance (this also links it).
            Log::info("Do: " + text());
            m_instance = m_project->createComponentInstance(
                m_parentUid, m_parentConnector, m_modelId, m_selfConnector, m_snapAngle);
            if (!m_instance) return;
        } else {
            // Redo after an undo: re-link the same instance (keeps uid + emulator).
            // takeComponent() cleared the parent info, so restore it first.
            Log::info("Redo: " + text());
            m_instance->parentUid = m_parentUid;
            m_instance->parentConnector = m_parentConnector;
            m_project->adoptComponent(m_instance);
        }
        m_linked = true;
        Application::getInstance()->reloadSimulation();
    }

    void undo() override {
        if (!m_project || !m_instance) return;

        Log::info("Undo: " + text());
        // Unlink without deleting; the command retains ownership.
        m_project->takeComponent(m_instance->uid);
        m_linked = false;
        Application::getInstance()->reloadSimulation();
    }

private:
    Project* m_project;
    int m_parentUid;
    QString m_parentConnector;
    QString m_modelId;
    QString m_selfConnector;
    float m_snapAngle;

    ComponentInstance* m_instance = nullptr;
    bool m_linked = false;
};
