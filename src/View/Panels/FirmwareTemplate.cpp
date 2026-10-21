#include "FirmwareTemplate.h"

#include <QRegularExpression>

#include "Document/Project.h"
#include "Document/IdealBoard.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"

QString firmwareTemplate(Project* project, bool realLevel) {
    QString pinList, servoName, servoPin;
    int pulseMin = 1000, pulseMax = 2000;
    const bool real = project ? project->getLevel() == ProjectLevel::Real : realLevel;
    // At the Real level: the active board's pins from the wiring.
    const RealBoard board = real && project ? project->realBoard() : RealBoard();
    const bool esp32 = board.isValid() && board.def().fqbn.startsWith("esp32:");
    const QList<BoardPin> boardPins = real ? board.pins() : project ? project->idealBoard().pins() : QList<BoardPin>{};
    for (const BoardPin& p : boardPins) {
        const QString name = p.alias.isEmpty() ? p.constant : p.alias;
        if (real)
            pinList += QString("const int %1 = %2;   // %3 %4 -> %5 %6 (%7)\n")
                           .arg(name).arg(p.number).arg(board.instance()->name, p.boardPinId.toUpper(), p.partName, p.pinId, p.type);
        else
            pinList += QString("//   %1%2  %3 (%4), %5\n")
                           .arg(name, p.alias.isEmpty() ? QString() : " = " + p.constant, p.partName, p.modelId, p.type);
        if (servoPin.isEmpty() && p.type == "pwm") {
            servoPin = name;
            servoName = p.partName.isEmpty() ? QString("servo") : p.partName;
            ComponentInstance* comp = project->getComponentByUid(p.uid);
            ComponentBlueprint* bp = comp ? comp->getBlueprint() : nullptr;
            if (bp) {
                pulseMin = bp->emulatorDef.parameters.value("pulse_min_us", pulseMin).toInt();
                pulseMax = bp->emulatorDef.parameters.value("pulse_max_us", pulseMax).toInt();
            }
        }
    }

    QString t;
    if (real) {
        t = board.isValid() ? QString("// Robot firmware: an Arduino sketch for %1 (%2), as on the real board.\n"
                                      "// It uses the board's own pins (9, A0, LED_BUILTIN); what is wired where is\n"
                                      "// on the Wiring page and in the board's Inspector.\n").arg(board.instance()->name, board.def().fqbn)
                            : QString("// Robot firmware: an Arduino sketch for the board this project will have.\n"
                                      "// It uses the board's own pins (9, A0, LED_BUILTIN), as on the real board.\n");
        t += esp32 ? "\n#include <ESP32Servo.h>\n\n" : "\n#include <Servo.h>\n\n";
        t += pinList.isEmpty() ? (board.isValid() ? "// No part is wired to the board yet (Wiring page, Ctrl+3).\n\n"
                                                  : "// No board yet: add one from the library (Boards), then wire it.\n\n")
                               : "// The pins wired to the board so far:\n" + pinList + "\n";
    } else {
        t = QString("// Robot firmware: an Arduino sketch. It runs on the simulator's ideal board.\n"
                    "//\n"
                    "// robot_pins.h names each part's signal pin (also in the Inspector,\n"
                    "// \"Pins (ideal board)\"). In this project:\n");
        t += pinList.isEmpty() ? "//   (none yet: add a part with a signal pin, e.g. a servo)\n" : pinList;
        t += "\n#include <Servo.h>\n#include \"robot_pins.h\"\n\n";
    }
    if (servoPin.isEmpty()) {
        t += "void setup() {\n  Serial.begin(115200);\n}\n\n"
             "void loop() {\n  delay(20);\n}\n";
        return t;
    }
    QString var = servoName.toLower();
    var.replace(QRegularExpression("[^a-z0-9]+"), "_");
    var.remove(QRegularExpression("^_+|_+$"));
    if (var.isEmpty() || var[0].isDigit()) var = "servo";
    t += QString("Servo %1;\n\n").arg(var);
    t += "void setup() {\n  Serial.begin(115200);\n";
    t += QString("  %1.attach(%2, %3, %4);   // the part's pulse range, in microseconds\n").arg(var, servoPin).arg(pulseMin).arg(pulseMax);
    if (esp32) t += QString("  %1.setTimerWidth(16);   // after attach(): 0.3 us steps instead of the default 19.5 us\n").arg(var);
    t += "}\n\nvoid loop() {\n";
    t += QString("  %1.write(90);   // 0..180 degrees, 90 = centre\n").arg(var);
    t += "  delay(20);\n}\n";
    return t;
}

