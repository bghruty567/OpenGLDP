#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glad/glad.h>


class GLGradientEngine {
public:
	//规则网格参数，后两个参数目前不用
    struct RegularParams { 
		int dims[3]; // 维度信息 [nx, ny, nz]
		float origin[3]; // 原点坐标 [ox, oy, oz]
		float spacing[3]; // 网格间距 [sx, sy, sz]
    };
	//非结构化网格梯度计算参数
    struct WLSParams { 
		float wExponent; // 权重指数，控制权重衰减速度
		float lambda; // 正则化参数，控制数值稳定性和过拟合程度
    };
    GLGradientEngine();
    ~GLGradientEngine();
    /*
	* @brief 设置着色器目录
	* @param dir 着色器文件所在目录路径
    */
    bool setShaderDir(const std::string& dir);
    /*
	* @brief 初始化GLGradientEngine，编译、链接着色器等
    */
    bool init();
    /*
	* @brief 释放资源
    */
    void release();
    /*
	* @brief 计算规则网格的梯度
	* @param positions 输入点位置数组，格式为[x0,y0,z0,x1,y1,z1,...]
	* @param values 输入标量值数组，格式为[v0,v1,...]或[v0_0,v0_1,...,v1_0,v1_1,...]（单组件或多组件）
	* @param p 规则网格参数，包含维度、原点和间距信息
	* @param outGrad 输出梯度数组，格式为[gx0,gy0,gz0,gx1,gy1,gz1,...]或[gx0_0,gy0_0,gz0_0,gx0_1,gy0_1,gz0_1,...]（单组件或多组件）
    */
    bool computeRegularFD(const std::vector<float>& positions, const std::vector<float>& values, const RegularParams& p, std::vector<float>& outGrad);
    /*
	* @brief 计算非结构化网格的梯度，使用加权最小二乘法
	* @param positions 输入点位置数组，格式为[x0,y0,z0,x1,y1,z1,...]
	* @param offsets 输入邻域偏移数组，长度为点数+1，格式为[offset0, offset1, ..., offsetN]，其中offsets[i]表示第i个点的邻域在neighbors数组中的起始索引，offsets[i+1] - offsets[i]表示第i个点的邻域大小
	* @param neighbors 输入邻域点索引数组，格式为[p1,p2,...]，包含所有点的邻域点索引，具体邻域信息由offsets数组定义
	* @param phi 输入数据数组，格式为[v0,v1,...]或[v0_0,v0_1,...,v1_0,v1_1,...]（单组件或多组件），长度应与点数的整数倍匹配
	* @param p WLS参数，包含权重指数和正则化参数
	* @param outGrad 输出梯度数组，格式为[gx0,gy0,gz0,gx1,gy1,gz1,...]或[gx0_0,gy0_0,gz0_0,gx0_1,gy0_1,gz0_1,...]（单组件或多组件），长度应与点数的3倍整数倍匹配
    */
    bool computeUnstructuredWLS(const std::vector<float>& positions, const std::vector<int>& offsets, const std::vector<int>& neighbors, const std::vector<float>& phi, const WLSParams& p, std::vector<float>& outGrad);

    /*
	* @brief 设置是否启用GPU计时，启用后computeRegularFD和computeUnstructuredWLS会在GPU上测量执行时间，并通过getLastGpuTimeMs返回结果
	* @param on 是否启用GPU计时
    */
    void   setEnableGpuTiming(bool on);

    /*
	* @brief 获取上一次GPU计算的执行时间，单位为毫秒，仅当启用GPU计时后有效
    */
    double getLastGpuTimeMs() const;
private:
	std::string shaderDir;//着色器目录路径
	GLuint progRegular = 0;//规则网格计算着色器程序ID
	GLuint progWLS = 0;//非结构化网格计算着色器程序ID
	//计算过程中使用的SSBO对象ID
	//FD计算：ssbo0存储输入点位置，ssbo1存储输入数据，ssbo2存储输出梯度
	//WLS计算：ssbo0存储输入点位置，ssbo1存储邻域偏移，ssbo2存储邻域点索引，ssbo3存储输入数据，ssbo4存储输出梯度
	GLuint ssbo0 = 0, ssbo1 = 0, ssbo2 = 0, ssbo3 = 0, ssbo4 = 0;
    /*
	* @brief 从文件编译并链接计算着色器，返回程序ID，失败返回0
	* @param path 着色器文件路径
    */
    GLuint buildComputeFromFile(const std::string& path);
    /*
	* @brief 确保SSBO缓冲区存在并具有足够大小，如果当前缓冲区不足则重新分配
	* @param id SSBO对象ID引用，如果当前为0则创建新缓冲区
	* @param bytes 需要的缓冲区大小，单位为字节
	* @param usage OpenGL缓冲区使用模式，如GL_DYNAMIC_DRAW等
    */
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);

	bool   enableGpuTiming = false;//是否启用GPU计时
	GLuint timeQuery = 0;//用于GPU计时的查询对象ID
	double lastGpuTimeMs = 0.0;//上一次GPU计算的执行时间，单位为毫秒

};