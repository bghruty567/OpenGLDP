// DataObject.h 
/*
* 系统内部使用的数据对象
*/
#pragma once
#include <vector>
#include <string>

/*
* @enum DataArrayType
* @brief 数据数组类型枚举
* 点数据和单元数据
*/
enum DataArrayType {
	POINT_DATA = 0,
	CELL_DATA = 1
};

/*
* @struct DataArray
* @brief 数据数组结构体
* 名称、数据、组件数和数据类型,用于存储点数据或单元数据
*/
struct DataArray {
    std::string name; 
    std::vector<float> data;
	int numComponents;
    DataArrayType dataType;

};

/*
* @enum GridType
* @brief 网格类型枚举
* 规则网格和非结构化网格
*/
enum GridType {
    DATA_OBJECT_TYPE_RegularGrid = 0,
    DATA_OBJECT_TYPE_UNSTRUCTURED = 1
};

/*
* @class DataObject
* @brief 内部数据对象类
*/
class DataObject {
public:
    
    DataObject()=default;
    ~DataObject()=default;

	GridType gridType;  
    std::vector<float> points;  // 点坐标 [x0,y0,z0,x1,y1,z1,...]
	std::vector<float> cellCenters; // 单元中心坐标 [cx0,cy0,cz0,cx1,cy1,cz1,...]
	std::vector<DataArray> dataArrays; // 数据数组
	std::vector<int> pointNeighbors; // 点邻域信息 [p1,p2,p5,p0,p3...]
	std::vector<int> pointNeighborOffsets; // 点邻域偏移信息[0,3...]
	std::vector<int> pointInCellNeighbors; // 点所属单元邻域信息[c0,c2,c5,c1,c3...]  ？
	std::vector<int> pointInCellNeighborOffsets; // 点所属单元邻域偏移信息[0,3...]  ?
	std::vector<int> cells; // 单元连接关系 [p0,p1,p2,...]
	std::vector<int> cellTypes; // 单元类型信息 [type0,type1,...]
	std::vector<int> cellOffsets; // 单元偏移信息 [0,4,8,...]
	std::vector<int> cellNeighbors; // 单元邻域信息 [p1,p2,p5,p0,p3...]单元中心点的邻域信息(邻域点也为单元中心点)
	std::vector<int> cellNeighborsOffsets;//单元邻域偏移信息[0,3...]索引与单元中心坐标对应
	int dimensions[3]; // 规则网格的维度信息(仅规则网格设置)
	

	/*
	* @brief查找数据数组
	* @param name 数据数组名称
	* @param type 数据数组类型（点数据或单元数据）
	*/
	DataArray* findDataArray(const std::string& name, DataArrayType type);

	/*
	* @brief 查找数据数组（返回的数据不能修改）
	*/
	const DataArray* findDataArray(const std::string& name, DataArrayType type) const;

	/*
	* @brief 更新或插入数据数组
	* @param name 数据数组名称
	* @param data 数据数组内容
	* @param numComponents 数据数组组件数
	* @param type 数据数组类型（点数据或单元数据）
	*/
	bool upsertDataArray(const std::string& name, const std::vector<float>& data, int numComponents, DataArrayType type);
	/*
	* @brief 获取点数量
	*/
	size_t pointCount() const;
	/*
	* @brief 获取单元数量
	*/
	size_t cellCount() const;
    //bool hasGhostData;
    //std::vector<unsigned char> ghostValues;

    /*
	* @brief 添加数据数组到内部数据对象
	* @param name 数据数组名称
	* @param data 数据数组内容
	* @param numComponents 数据数组的组件数
	* @param dataType 数据数组类型（点数据或单元数据）
    */
    //void addDataArray(const std::string& name, const std::vector<float>& data, int numComponents, DataArrayType dataType);
};