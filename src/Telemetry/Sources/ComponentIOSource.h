#include "TelemetrySource.h"
#include "Telemetry/Storage/TimeSeriesBuffer.h"
#include "Telemetry/Storage/ImageBuffer.h"
#include <any>

class ComponentInstance;
using namespace std;


class ComponentIOSource : public TelemetrySource {
    ComponentInstance* comp;
    QString ioKey;
    bool isInput;
    std::variant<TimeSeriesBuffer, ImageBuffer> storage; // chosen once, at construction, from channel_type

    public:
        ComponentIOSource(ComponentInstance* comp, QString ioKey, bool isInput);
        void capture(double time) override;
        any snapshotAll() override;
};
