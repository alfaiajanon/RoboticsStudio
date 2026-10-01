#include "ComponentIOSource.h"

#include "Telemetry/Storage/TimeSeriesBuffer.h"
#include "TelemetrySource.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"



// Signal width from the interface (stated for emulator kind, otherwise derived
// from the target device type). Image signals (0) are not plottable here.
static int signalDim(ComponentInstance* comp, const QString& ioKey, bool isInput) {
    if (!comp || !comp->getBlueprint()) return 1;
    ComponentBlueprint* bp = comp->getBlueprint();
    const QMap<QString, InterfaceDef>& defs = isInput ? bp->interfaceInputs : bp->interfaceOutputs;
    auto it = defs.constFind(ioKey);
    if (it == defs.constEnd()) return 1;
    int dim = bp->interfaceDim(it.value());
    return dim > 0 ? dim : 1;
}


ComponentIOSource::ComponentIOSource(ComponentInstance* comp, QString ioKey, bool isInput) :
            storage(std::in_place_type<TimeSeriesBuffer>, 100, signalDim(comp, ioKey, isInput)){
    this->comp = comp;
    this->ioKey=ioKey;
    this->isInput=isInput;
}


void ComponentIOSource::capture(double time) {
    if (!isActive()) return;

    if (auto* tsBuf = std::get_if<TimeSeriesBuffer>(&storage)) {
        BasicIOValue data;
        if(isInput)
            data = comp->getActuatorValue(ioKey);
        else
            data = comp->getSensorValue(ioKey);
        // A key with no runtime slot comes back with empty data; push() also drops
        // vectors whose width does not match the buffer.
        if(!data.data.empty()){
            tsBuf->push(time, data.data);
        }
    }
    else if (auto* imgBuf = std::get_if<ImageBuffer>(&storage)) {
        // camera & display device maybe?
    }
}



std::any ComponentIOSource::snapshotAll() {
    TimeSeriesBuffer &buffer=std::get<TimeSeriesBuffer>(storage);
    return buffer.snapshot();
}
