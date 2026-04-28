#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer PointInCellOffsets { int pointCellOff[]; };
layout(std430, binding = 1) readonly buffer PointInCellNeighbors { int pointCellNbr[]; };
layout(std430, binding = 2) readonly buffer CellTypes { int cellType[]; };
layout(std430, binding = 3) readonly buffer CellValues { float cellVal[]; };
layout(std430, binding = 4) writeonly buffer PointValues { float pointVal[]; };

uniform int uPointCount;
uniform int uCellCount;
uniform int uNumComponents;

const int VTK_LINE = 3;
const int VTK_TRIANGLE = 5;
const int VTK_PIXEL = 8;
const int VTK_QUAD = 9;
const int VTK_TETRA = 10;
const int VTK_VOXEL = 11;
const int VTK_HEXAHEDRON = 12;
const int VTK_WEDGE = 13;

int supportedCellDimension(int type)
{
    switch (type) {
    case VTK_LINE:
        return 1;
    case VTK_TRIANGLE:
    case VTK_PIXEL:
    case VTK_QUAD:
        return 2;
    case VTK_TETRA:
    case VTK_VOXEL:
    case VTK_HEXAHEDRON:
    case VTK_WEDGE:
        return 3;
    default:
        return 0;
    }
}

float cellValueAt(int cellId, int comp)
{
    return cellVal[cellId * uNumComponents + comp];
}

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uPointCount) {
        return;
    }

    int pointId = int(iu);
    int outBase = pointId * uNumComponents;
    for (int comp = 0; comp < uNumComponents; ++comp) {
        pointVal[outBase + comp] = 0.0;
    }

    int begin = pointCellOff[pointId];
    int end = pointCellOff[pointId + 1];
    int maxDim = 0;
    for (int idx = begin; idx < end; ++idx) {
        int cellId = pointCellNbr[idx];
        if (cellId < 0 || cellId >= uCellCount) {
            continue;
        }
        maxDim = max(maxDim, supportedCellDimension(cellType[cellId]));
    }

    if (maxDim <= 0) {
        return;
    }

    int usedCells = 0;
    for (int idx = begin; idx < end; ++idx) {
        int cellId = pointCellNbr[idx];
        if (cellId < 0 || cellId >= uCellCount) {
            continue;
        }
        if (supportedCellDimension(cellType[cellId]) != maxDim) {
            continue;
        }

        usedCells += 1;
        for (int comp = 0; comp < uNumComponents; ++comp) {
            pointVal[outBase + comp] += cellValueAt(cellId, comp);
        }
    }

    if (usedCells <= 0) {
        return;
    }

    float invUsed = 1.0 / float(usedCells);
    for (int comp = 0; comp < uNumComponents; ++comp) {
        pointVal[outBase + comp] *= invUsed;
    }
}
