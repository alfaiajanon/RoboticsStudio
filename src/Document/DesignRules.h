#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

class Project;


// One finding of the design-rule check, tied to a part (and pin) when it has one.
struct RuleIssue {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Warning;
    QString message;
    int uid = -1;        // the part to select; -1 = the project
    QString pin;         // the pin to highlight; empty = the whole part
    int net = -1;        // index into Netlist::nets(), for net-level faults

    bool isError() const { return severity == Severity::Error; }
};


/*
 * The Real level's wiring checks, on the project's nets: what a real board
 * would do wrong with this wiring. None of them stops Play (a real board also
 * runs with wrong wiring); the run then does what the hardware would: an
 * unpowered part stays dead, a part off the I2C pins doesn't answer.
 *
 * At the Prototype level the ideal board wires everything: no findings.
 */
namespace DesignRules {

    QList<RuleIssue> check(Project& project);

    // Parts that get no supply at the Real level: a typed power pin not on a
    // net with a source (power_out), or ground pins with none on a board's
    // GND. Value = why. Parts without typed power/ground pins count as powered.
    QHash<int, QString> unpoweredParts(Project& project);
}
