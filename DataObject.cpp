#include "DataObject.h"

DataArray* DataObject::findDataArray(const std::string& name, DataArrayType type)
{
    for (auto& a : dataArrays) {
        if (a.name == name && a.dataType == type) return &a;
    }
    return nullptr;
}

const DataArray* DataObject::findDataArray(const std::string& name, DataArrayType type) const
{
    for (const auto& a : dataArrays) {
        if (a.name == name && a.dataType == type) return &a;
    }
    return nullptr;
}

bool DataObject::upsertDataArray(const std::string& name, const std::vector<float>& data, int numComponents, DataArrayType type)
{
    if (numComponents <= 0) return false;
    DataArray* old = findDataArray(name, type);
    //∏¸–¬
    if (old) {
        old->data = data;
        old->numComponents = numComponents;
        return true;
    }
    //≤Â»Î
    DataArray a;
    a.name = name;
    a.data = data;
    a.numComponents = numComponents;
    a.dataType = type;
    dataArrays.push_back(std::move(a));
    return true;
}

size_t DataObject::pointCount() const
{
    return points.size() / 3;
}

size_t DataObject::cellCount() const
{
    if (cellOffsets.size() < 2) return 0;
    return cellOffsets.size() - 1;
}


