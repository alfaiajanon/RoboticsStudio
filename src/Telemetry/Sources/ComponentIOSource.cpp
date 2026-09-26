#include "ComponentIOSource.h"

#include "Telemetry/Storage/TimeSeriesBuffer.h"
#include "TelemetrySource.h"
#include "Document/Components/ComponentInstance.h"



ComponentIOSource::ComponentIOSource(ComponentInstance* comp, QString ioKey, bool isInput) :
            storage(std::in_place_type<TimeSeriesBuffer>, 100, 1){
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
        if(data.dim==1){
            vector<double> vec;
            vec.push_back(data.data[0]);
            tsBuf->push(time, vec);
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
