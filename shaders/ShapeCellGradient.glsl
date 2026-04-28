#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer Points { float pos[]; };
layout(std430, binding = 1) readonly buffer CellOffsets { int cellOff[]; };
layout(std430, binding = 2) readonly buffer Cells { int cellConn[]; };
layout(std430, binding = 3) readonly buffer CellTypes { int cellType[]; };
layout(std430, binding = 4) readonly buffer PointValues { float pointVal[]; };
layout(std430, binding = 5) writeonly buffer Gradients { float grad[]; };

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

vec3 loadPoint(int pointId)
{
    int base = pointId * 3;
    return vec3(pos[base + 0], pos[base + 1], pos[base + 2]);
}

float pointValueAt(int pointId, int comp)
{
    return pointVal[pointId * uNumComponents + comp];
}

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

int supportedCellNodeCount(int type)
{
    switch (type) {
    case VTK_LINE:
        return 2;
    case VTK_TRIANGLE:
        return 3;
    case VTK_PIXEL:
    case VTK_QUAD:
    case VTK_TETRA:
        return 4;
    case VTK_WEDGE:
        return 6;
    case VTK_VOXEL:
    case VTK_HEXAHEDRON:
        return 8;
    default:
        return 0;
    }
}

bool cellCenterParametricCoords(int type, out vec3 pcoords)
{
    switch (type) {
    case VTK_LINE:
        pcoords = vec3(0.5, 0.0, 0.0);
        return true;
    case VTK_TRIANGLE:
        pcoords = vec3(1.0 / 3.0, 1.0 / 3.0, 0.0);
        return true;
    case VTK_PIXEL:
    case VTK_QUAD:
        pcoords = vec3(0.5, 0.5, 0.0);
        return true;
    case VTK_TETRA:
        pcoords = vec3(0.25, 0.25, 0.25);
        return true;
    case VTK_VOXEL:
    case VTK_HEXAHEDRON:
        pcoords = vec3(0.5, 0.5, 0.5);
        return true;
    case VTK_WEDGE:
        pcoords = vec3(1.0 / 3.0, 1.0 / 3.0, 0.5);
        return true;
    default:
        return false;
    }
}

bool shapeFunctionDerivatives(int type,
                              vec3 pcoords,
                              out vec3 dNdXi[8],
                              out int cellDim,
                              out int nodeCount)
{
    for (int i = 0; i < 8; ++i) {
        dNdXi[i] = vec3(0.0);
    }

    float r = pcoords.x;
    float s = pcoords.y;
    float t = pcoords.z;

    switch (type) {
    case VTK_LINE:
        cellDim = 1;
        nodeCount = 2;
        dNdXi[0] = vec3(-1.0, 0.0, 0.0);
        dNdXi[1] = vec3( 1.0, 0.0, 0.0);
        return true;

    case VTK_TRIANGLE:
        cellDim = 2;
        nodeCount = 3;
        dNdXi[0] = vec3(-1.0, -1.0, 0.0);
        dNdXi[1] = vec3( 1.0,  0.0, 0.0);
        dNdXi[2] = vec3( 0.0,  1.0, 0.0);
        return true;

    case VTK_QUAD:
        cellDim = 2;
        nodeCount = 4;
        dNdXi[0] = vec3(-(1.0 - s), -(1.0 - r), 0.0);
        dNdXi[1] = vec3( (1.0 - s), -r,          0.0);
        dNdXi[2] = vec3( s,          r,          0.0);
        dNdXi[3] = vec3(-s,          1.0 - r,    0.0);
        return true;

    case VTK_PIXEL:
        cellDim = 2;
        nodeCount = 4;
        dNdXi[0] = vec3(-(1.0 - s), -(1.0 - r), 0.0);
        dNdXi[1] = vec3( (1.0 - s), -r,          0.0);
        dNdXi[2] = vec3(-s,          1.0 - r,    0.0);
        dNdXi[3] = vec3( s,          r,          0.0);
        return true;

    case VTK_TETRA:
        cellDim = 3;
        nodeCount = 4;
        dNdXi[0] = vec3(-1.0, -1.0, -1.0);
        dNdXi[1] = vec3( 1.0,  0.0,  0.0);
        dNdXi[2] = vec3( 0.0,  1.0,  0.0);
        dNdXi[3] = vec3( 0.0,  0.0,  1.0);
        return true;

    case VTK_HEXAHEDRON:
        cellDim = 3;
        nodeCount = 8;
        dNdXi[0] = vec3(-(1.0 - s) * (1.0 - t), -(1.0 - r) * (1.0 - t), -(1.0 - r) * (1.0 - s));
        dNdXi[1] = vec3( (1.0 - s) * (1.0 - t), -r * (1.0 - t),         -r * (1.0 - s));
        dNdXi[2] = vec3( s * (1.0 - t),          r * (1.0 - t),         -r * s);
        dNdXi[3] = vec3(-s * (1.0 - t),          (1.0 - r) * (1.0 - t), -(1.0 - r) * s);
        dNdXi[4] = vec3(-(1.0 - s) * t,          -(1.0 - r) * t,         (1.0 - r) * (1.0 - s));
        dNdXi[5] = vec3( (1.0 - s) * t,          -r * t,                  r * (1.0 - s));
        dNdXi[6] = vec3( s * t,                   r * t,                  r * s);
        dNdXi[7] = vec3(-s * t,                   (1.0 - r) * t,          (1.0 - r) * s);
        return true;

    case VTK_VOXEL:
        cellDim = 3;
        nodeCount = 8;
        dNdXi[0] = vec3(-(1.0 - s) * (1.0 - t), -(1.0 - r) * (1.0 - t), -(1.0 - r) * (1.0 - s));
        dNdXi[1] = vec3( (1.0 - s) * (1.0 - t), -r * (1.0 - t),         -r * (1.0 - s));
        dNdXi[2] = vec3(-s * (1.0 - t),          (1.0 - r) * (1.0 - t), -(1.0 - r) * s);
        dNdXi[3] = vec3( s * (1.0 - t),          r * (1.0 - t),         -r * s);
        dNdXi[4] = vec3(-(1.0 - s) * t,          -(1.0 - r) * t,         (1.0 - r) * (1.0 - s));
        dNdXi[5] = vec3( (1.0 - s) * t,          -r * t,                  r * (1.0 - s));
        dNdXi[6] = vec3(-s * t,                   (1.0 - r) * t,          (1.0 - r) * s);
        dNdXi[7] = vec3( s * t,                   r * t,                  r * s);
        return true;

    case VTK_WEDGE:
        cellDim = 3;
        nodeCount = 6;
        dNdXi[0] = vec3(-(1.0 - t), -(1.0 - t), -(1.0 - r - s));
        dNdXi[1] = vec3( (1.0 - t),  0.0,       -r);
        dNdXi[2] = vec3( 0.0,        (1.0 - t), -s);
        dNdXi[3] = vec3(-t,         -t,          (1.0 - r - s));
        dNdXi[4] = vec3( t,          0.0,        r);
        dNdXi[5] = vec3( 0.0,        t,          s);
        return true;

    default:
        cellDim = 0;
        nodeCount = 0;
        return false;
    }
}

bool invertMetric(float g[9], int dim, out float invG[9])
{
    for (int i = 0; i < 9; ++i) {
        invG[i] = 0.0;
    }

    if (dim == 1) {
        if (abs(g[0]) <= 1e-20) {
            return false;
        }
        invG[0] = 1.0 / g[0];
        return true;
    }

    if (dim == 2) {
        float det = g[0] * g[4] - g[1] * g[3];
        if (abs(det) <= 1e-20) {
            return false;
        }
        float invDet = 1.0 / det;
        invG[0] =  g[4] * invDet;
        invG[1] = -g[1] * invDet;
        invG[3] = -g[3] * invDet;
        invG[4] =  g[0] * invDet;
        return true;
    }

    if (dim == 3) {
        float det =
            g[0] * (g[4] * g[8] - g[5] * g[7]) -
            g[1] * (g[3] * g[8] - g[5] * g[6]) +
            g[2] * (g[3] * g[7] - g[4] * g[6]);
        if (abs(det) <= 1e-24) {
            return false;
        }
        float invDet = 1.0 / det;
        invG[0] =  (g[4] * g[8] - g[5] * g[7]) * invDet;
        invG[1] = -(g[1] * g[8] - g[2] * g[7]) * invDet;
        invG[2] =  (g[1] * g[5] - g[2] * g[4]) * invDet;
        invG[3] = -(g[3] * g[8] - g[5] * g[6]) * invDet;
        invG[4] =  (g[0] * g[8] - g[2] * g[6]) * invDet;
        invG[5] = -(g[0] * g[5] - g[2] * g[3]) * invDet;
        invG[6] =  (g[3] * g[7] - g[4] * g[6]) * invDet;
        invG[7] = -(g[0] * g[7] - g[1] * g[6]) * invDet;
        invG[8] =  (g[0] * g[4] - g[1] * g[3]) * invDet;
        return true;
    }

    return false;
}

bool invertJacobian3D(vec3 tangents[3], out float invJ[9])
{
    float j00 = tangents[0].x;
    float j01 = tangents[0].y;
    float j02 = tangents[0].z;
    float j10 = tangents[1].x;
    float j11 = tangents[1].y;
    float j12 = tangents[1].z;
    float j20 = tangents[2].x;
    float j21 = tangents[2].y;
    float j22 = tangents[2].z;

    float c00 = j11 * j22 - j12 * j21;
    float c01 = j02 * j21 - j01 * j22;
    float c02 = j01 * j12 - j02 * j11;
    float c10 = j12 * j20 - j10 * j22;
    float c11 = j00 * j22 - j02 * j20;
    float c12 = j02 * j10 - j00 * j12;
    float c20 = j10 * j21 - j11 * j20;
    float c21 = j01 * j20 - j00 * j21;
    float c22 = j00 * j11 - j01 * j10;

    float det = j00 * c00 + j01 * c10 + j02 * c20;
    if (abs(det) <= 1e-20) {
        return false;
    }

    float invDet = 1.0 / det;
    invJ[0] = c00 * invDet;
    invJ[1] = c01 * invDet;
    invJ[2] = c02 * invDet;
    invJ[3] = c10 * invDet;
    invJ[4] = c11 * invDet;
    invJ[5] = c12 * invDet;
    invJ[6] = c20 * invDet;
    invJ[7] = c21 * invDet;
    invJ[8] = c22 * invDet;
    return true;
}

bool prepareCellGeometry(int cellId,
                         vec3 pcoords,
                         out int cellDim,
                         out int nodeCount,
                         out int pointIds[8],
                         out vec3 tangents[3],
                         out vec3 dNdXi[8],
                         out float invMap[9])
{
    if (cellId < 0 || cellId >= uCellCount) {
        return false;
    }

    int type = cellType[cellId];
    int expectedNodes = supportedCellNodeCount(type);
    cellDim = supportedCellDimension(type);
    if (expectedNodes <= 0 || cellDim <= 0) {
        return false;
    }

    int begin = cellOff[cellId];
    int end = cellOff[cellId + 1];
    if (end < begin || end - begin != expectedNodes) {
        return false;
    }
    int originPointId = cellConn[begin];
    if (originPointId < 0 || originPointId >= uPointCount) {
        return false;
    }
    vec3 xOrigin = loadPoint(originPointId);

    int derivDim = 0;
    int derivNodeCount = 0;
    if (!shapeFunctionDerivatives(type, pcoords, dNdXi, derivDim, derivNodeCount) ||
        derivDim != cellDim || derivNodeCount != expectedNodes) {
        return false;
    }

    for (int axis = 0; axis < 3; ++axis) {
        tangents[axis] = vec3(0.0);
    }

    for (int localId = 0; localId < expectedNodes; ++localId) {
        int pointId = cellConn[begin + localId];
        if (pointId < 0 || pointId >= uPointCount) {
            return false;
        }
        pointIds[localId] = pointId;
        vec3 x = loadPoint(pointId) - xOrigin;
        for (int axis = 0; axis < cellDim; ++axis) {
            tangents[axis] += x * dNdXi[localId][axis];
        }
    }

    if (cellDim == 3) {
        // Use the inverse Jacobian directly for 3D cells to avoid squaring
        // the condition number on thin or distorted hexahedra.
        nodeCount = expectedNodes;
        return invertJacobian3D(tangents, invMap);
    }

    float g[9];
    for (int i = 0; i < 9; ++i) {
        g[i] = 0.0;
    }
    for (int i = 0; i < cellDim; ++i) {
        for (int j = 0; j < cellDim; ++j) {
            g[i * 3 + j] = dot(tangents[i], tangents[j]);
        }
    }

    nodeCount = expectedNodes;
    return invertMetric(g, cellDim, invMap);
}

vec3 evaluateComponentGradient(int comp,
                               int cellDim,
                               int nodeCount,
                               int pointIds[8],
                               vec3 tangents[3],
                               vec3 dNdXi[8],
                               float invMap[9])
{
    vec3 gradRef = vec3(0.0);
    float valueOrigin = pointValueAt(pointIds[0], comp);
    for (int localId = 0; localId < nodeCount; ++localId) {
        gradRef += (pointValueAt(pointIds[localId], comp) - valueOrigin) * dNdXi[localId];
    }

    if (cellDim == 3) {
        return vec3(
            gradRef.x * invMap[0] + gradRef.y * invMap[1] + gradRef.z * invMap[2],
            gradRef.x * invMap[3] + gradRef.y * invMap[4] + gradRef.z * invMap[5],
            gradRef.x * invMap[6] + gradRef.y * invMap[7] + gradRef.z * invMap[8]
        );
    }

    float c0 = 0.0;
    float c1 = 0.0;
    float c2 = 0.0;
    if (cellDim >= 1) {
        c0 = invMap[0] * gradRef.x + invMap[1] * gradRef.y + invMap[2] * gradRef.z;
    }
    if (cellDim >= 2) {
        c1 = invMap[3] * gradRef.x + invMap[4] * gradRef.y + invMap[5] * gradRef.z;
    }
    if (cellDim >= 3) {
        c2 = invMap[6] * gradRef.x + invMap[7] * gradRef.y + invMap[8] * gradRef.z;
    }

    vec3 g = vec3(0.0);
    if (cellDim >= 1) {
        g += tangents[0] * c0;
    }
    if (cellDim >= 2) {
        g += tangents[1] * c1;
    }
    if (cellDim >= 3) {
        g += tangents[2] * c2;
    }
    return g;
}

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uCellCount) {
        return;
    }

    int cellId = int(iu);
    int outBase = cellId * (3 * uNumComponents);
    for (int comp = 0; comp < uNumComponents; ++comp) {
        grad[outBase + comp * 3 + 0] = 0.0;
        grad[outBase + comp * 3 + 1] = 0.0;
        grad[outBase + comp * 3 + 2] = 0.0;
    }

    vec3 pcoords;
    if (!cellCenterParametricCoords(cellType[cellId], pcoords)) {
        return;
    }

    int cellDim = 0;
    int nodeCount = 0;
    int pointIds[8];
    vec3 tangents[3];
    vec3 dNdXi[8];
    float invMap[9];
    if (!prepareCellGeometry(cellId, pcoords, cellDim, nodeCount, pointIds, tangents, dNdXi, invMap)) {
        return;
    }

    for (int comp = 0; comp < uNumComponents; ++comp) {
        vec3 g = evaluateComponentGradient(comp, cellDim, nodeCount, pointIds, tangents, dNdXi, invMap);
        grad[outBase + comp * 3 + 0] = g.x;
        grad[outBase + comp * 3 + 1] = g.y;
        grad[outBase + comp * 3 + 2] = g.z;
    }
}
