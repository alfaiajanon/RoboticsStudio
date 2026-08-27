#include <QString>
#include <QColor>


enum class PlotTargetType {
    SENSOR,
    ACTUATOR
};

struct PlotTarget {
    int compUid;          // Which component?
    PlotTargetType type;  // Sensor or Actuator?
    QString ioKey;        // The dictionary key (e.g., "target_angle")
    QColor color;         // Line color on the graph
    bool isVisible = true;// UI Checkbox state
};