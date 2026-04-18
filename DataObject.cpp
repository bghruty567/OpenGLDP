#include "DataObject.h"

/// 线性扫描查找字段。
///
/// 当前字段数量通常不大，这里保持实现简单直接。
/// 如果后续数组数量明显增多，再考虑引入名字到索引的缓存结构。
DataArray* DataObject::findDataArray(const std::string& name, DataArrayType type)
{
    for (auto& a : dataArrays) {
        if (a.name == name && a.dataType == type) return &a;
    }
    return nullptr;
}

/// 只读版本查找，逻辑与可写版本相同。
const DataArray* DataObject::findDataArray(const std::string& name, DataArrayType type) const
{
    for (const auto& a : dataArrays) {
        if (a.name == name && a.dataType == type) return &a;
    }
    return nullptr;
}

/// 以“同名则更新，不存在则插入”的方式维护字段数组。
///
/// 这样可以保证测试程序反复运行时，结果字段能够被覆盖更新，
/// 而不用手动先删除旧字段。
bool DataObject::upsertDataArray(const std::string& name, const std::vector<float>& data, int numComponents, DataArrayType type)
{
    if (numComponents <= 0) return false;
    DataArray* old = findDataArray(name, type);
    // 先尝试原位更新，避免无意义地新增同名数组。
    if (old) {
        old->data = data;
        old->numComponents = numComponents;
        return true;
    }

    // 若不存在，则构造新数组并追加到末尾。
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
    // 三个浮点数表示一个点坐标。
    return points.size() / 3;
}

size_t DataObject::cellCount() const
{
    // cellOffsets 的长度应为“单元数 + 1”。
    if (cellOffsets.size() < 2) return 0;
    return cellOffsets.size() - 1;
}


