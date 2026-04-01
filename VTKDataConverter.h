#pragma once
// VTKDataConverter.h
/*--------------------------------------------------------------
用于将VTK数据集转换为内部数据对象
 --------------------------------------------------------------*/
#pragma once
#include "DataObject.h"
#include "vtkDataSet.h"
#include "vtkPoints.h"
#include "vtkDataArray.h"
#include "vtkCellArray.h"
#include "vtkUnstructuredGrid.h"
#include "vtkImageData.h"
#include "vtkStructuredGrid.h"
#include "vtkRectilinearGrid.h"
#include "vtkStaticCellLinks.h"
#include <vector>
#include <string>

/*
* @class VTKDataConverter
* @brief 用于将VTK数据集转换为内部数据对象的类
* 
*/
class VTKDataConverter {
public:
    VTKDataConverter();
    ~VTKDataConverter();

    /*
	* @brief 执行VTK到内部数据对象的转换
	* 里面调用各个具体的数据转换函数。比如转换点坐标、点数据等
	* @todo 1.函数具体实现 2.这里可以考虑添加错误处理机制
    */
    int convertVTKToInternal();

	/*
	* @brief 绑定VTK数据集和内部数据对象
	* @param vtkData 输入的VTK数据集指针
	* @param internalData 输出的内部数据对象指针
    */
    void bindVTKDataAndInternalData(vtkDataSet* vtkData, DataObject* internalData);

    /*
	* @brief 执行内部数据对象到VTK数据集的转换
    */
	int convertInternalToVTK();

    vtkDataSet* vtkData;
    DataObject* internalData;

public:
    /*
	* @brief 转换点坐标数据
	* @todo 测试函数
    */
    int convertPoints();

    /*
	* @brief 转换数组数据
    */
    int convertDataArrays();

    /*
	* @brief 转换点所属单元信息
    */
    int convertPointInCellNeighbors();

    /*
	* @brief 转换点邻域点信息
    */
    int convertPointNeighbors();

    /*
	* @brief 转换单元连接关系和单元类型信息
    */
    int convertCell();

    /*
	* @brief 转换系统数据类型
    */
    int convertType();

    /*
    * @brief 转换规则网格维度
    */
	int convertDimensions();

    /*
	* @brief 转换单元中心坐标
    */
	int convertCellCenters();
    
    /*
	* @brief 转换规则网格数据
    */
    int convertRegularGrid();

    /*
	* @brief 转换非结构化网格数据
    */
	int convertUnstructuredGrid();

    /*
	* @brief 转换点邻域信息，使用KNN算法计算每个点的邻域点
    */
    int convertPointNeighborsByKNN(int k);

    /*
	* @brief 转换单元中心点的邻域信息，邻域由单元的邻域单元的中心点组成
    */
    int convertCellNeighbors();

    /*
	* @brief 转换单元中心点的邻域信息，邻域由单元的邻域单元的中心点组成，使用KNN算法计算每个单元中心点的邻域单元中心点
    */
	int convertCellNeighborsByKNN(int k);

    int convertPointNeighborsRobust(int minK, int knnK);
};

