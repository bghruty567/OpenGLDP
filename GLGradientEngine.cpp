#include "GLGradientEngine.h"

#include <iostream>
#include <fstream>
#include <sstream>

/*
* @brief 从文件中读取文本内容
* @param p 文件路径
*/
static std::string readFileText(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLGradientEngine::GLGradientEngine() {}
GLGradientEngine::~GLGradientEngine() { release(); }

bool GLGradientEngine::setShaderDir(const std::string& dir)
{
    shaderDir = dir;
    return true;
}

void GLGradientEngine::ensureBuffer(GLuint& id, size_t bytes, GLenum usage)
{
    if (id == 0)
    {
		glGenBuffers(1, &id);//创建新缓冲区
    }

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);//绑定缓冲区对象到GL_SHADER_STORAGE_BUFFER目标
	glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);//分配或重新分配缓冲区数据存储，bytes为所需大小，usage为使用模式
}

GLuint GLGradientEngine::buildComputeFromFile(const std::string& path)
{
	//检查OpenGL上下文版本，确保支持计算着色器
    GLint major = 0;
    GLint minor = 0;

	glGetIntegerv(GL_MAJOR_VERSION, &major);//获取OpenGL主版本号
	glGetIntegerv(GL_MINOR_VERSION, &minor);//获取OpenGL次版本号

    if (major < 4 || (major == 4 && minor < 3))
    {
        std::cerr << "[GL] Context version "
            << major << "." << minor
            << " < 4.3, cannot create compute shader\n";
        return 0;
    }

	GLuint sh = glCreateShader(GL_COMPUTE_SHADER);///创建计算着色器对象，返回对象ID
    GLenum err = glGetError();

	if (sh == 0 || err != GL_NO_ERROR)//检查着色器对象创建是否成功，glGetError检查是否有错误发生
    {
        std::cerr << "[GL] glCreateShader(COMPUTE) failed. sh="
            << sh << " glError=0x" << std::hex << err << std::dec << "\n";
        return 0;
    }

	std::string src = readFileText(path);//从指定路径读取着色器源代码文本
    if (src.empty())
        return 0;

    const char* c = src.c_str();

	glShaderSource(sh, 1, &c, nullptr);//设置着色器源代码
	glCompileShader(sh);//编译着色器

    GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);//检查着色器编译是否成功
    if (!ok)
    {
        glDeleteShader(sh);
        return 0;
    }

	GLuint pr = glCreateProgram();//创建着色器程序对象，返回对象ID

	glAttachShader(pr, sh);//将编译好的着色器对象附加到程序对象上
	glLinkProgram(pr);//链接程序对象，准备执行
	glDeleteShader(sh);//链接完成后可以删除着色器对象

	glGetProgramiv(pr, GL_LINK_STATUS, &ok);//检查程序链接是否成功
    if (!ok)
    {
        glDeleteProgram(pr);
        return 0;
    }

    return pr;
}

bool GLGradientEngine::init()
{
    if (shaderDir.empty())
        return false;

    if (progRegular == 0)
    {
        progRegular = buildComputeFromFile(shaderDir + "\\FD.glsl");
    }

    if (progWLS == 0)
    {
        progWLS = buildComputeFromFile(shaderDir + "\\WLS.glsl");
    }

    return progRegular && progWLS;
}

void GLGradientEngine::release()
{
    if (progRegular)
    {
        glDeleteProgram(progRegular);
        progRegular = 0;
    }
    if (progWLS)
    {
        glDeleteProgram(progWLS);
        progWLS = 0;
    }

    if (ssbo0) { glDeleteBuffers(1, &ssbo0); ssbo0 = 0; }
    if (ssbo1) { glDeleteBuffers(1, &ssbo1); ssbo1 = 0; }
    if (ssbo2) { glDeleteBuffers(1, &ssbo2); ssbo2 = 0; }
    if (ssbo3) { glDeleteBuffers(1, &ssbo3); ssbo3 = 0; }
    if (ssbo4) { glDeleteBuffers(1, &ssbo4); ssbo4 = 0; }
}

bool GLGradientEngine::computeRegularFD(const std::vector<float>& positions,
    const std::vector<float>& values,
    const RegularParams& p,
    std::vector<float>& outGrad)
{
    if (progRegular == 0) return false;
	int64_t n = int64_t(p.dims[0]) * p.dims[1] * p.dims[2];//计算网格点总数
	//检查输入数据的有效性，确保点数大于0，位置数组大小与点数匹配，值数组大小与点数的整数倍匹配
    if (n <= 0) return false;
    if (positions.size() != size_t(n) * 3) return false;
    if (values.empty() || (int64_t)(values.size() % n) != 0) return false;
    
    int comps = int((int64_t)values.size() / n);//确定数据分量数
	outGrad.resize(size_t(n) * size_t(3 * comps));//调整输出梯度数组大小

    ensureBuffer(ssbo0, positions.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, positions.size() * sizeof(float), positions.data());

    ensureBuffer(ssbo1, values.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, values.size() * sizeof(float), values.data());

    ensureBuffer(ssbo2, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);

    glUseProgram(progRegular);
	GLint ld = glGetUniformLocation(progRegular, "uDims");//获取着色器中uDims变量的位置
	GLint lc = glGetUniformLocation(progRegular, "uNumComponents");//获取着色器中uNumComponents变量的位置
	glUniform3ui(ld, (GLuint)p.dims[0], (GLuint)p.dims[1], (GLuint)p.dims[2]);//将网格维度信息传递给着色器
	glUniform1i(lc, comps);//将数据分量数传递给着色器

	//计算工作组数量，假设每个工作组处理8x8x8的网格点，根据网格维度计算需要多少个工作组
    GLuint gx = (p.dims[0] + 7) / 8;
    GLuint gy = (p.dims[1] + 7) / 8;
    GLuint gz = (p.dims[2] + 7) / 8;
	if (enableGpuTiming) {//如果启用GPU计时，开始计时
		if (timeQuery == 0) glGenQueries(1, &timeQuery);//如果计时查询对象不存在则创建
		glBeginQuery(GL_TIME_ELAPSED, timeQuery);//开始GPU时间查询
    }

	glDispatchCompute(gx, gy, gz);//启动计算着色器，指定工作组数量
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); //确保计算着色器写入的结果对后续操作可见

	if (enableGpuTiming) {//如果启用GPU计时，结束计时并获取结果
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6; // ns -> ms
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());//从GPU缓冲区读取计算结果到outGrad数组
    return true;
}

bool GLGradientEngine::computeUnstructuredWLS(const std::vector<float>& positions,
    const std::vector<int>& offsets,
    const std::vector<int>& neighbors,
    const std::vector<float>& phi,
    const WLSParams& p,
    std::vector<float>& outGrad)
{
    size_t np = positions.size() / 3;
    if (np == 0)
        return false;

    if (offsets.size() != np + 1)
        return false;

    if (progWLS == 0)
        return false;

    if (phi.empty() || (phi.size() % np) != 0)
        return false;

    int comps = int(phi.size() / np);

    outGrad.resize(np * size_t(3 * comps));

	// WLS计算着色器中每个点的位置使用vec4格式存储，第四分量填充为0，以满足std140布局要求
    std::vector<float> pos4(np * 4);
    for (size_t i = 0; i < np; ++i)
    {
        pos4[i * 4 + 0] = positions[i * 3 + 0];
        pos4[i * 4 + 1] = positions[i * 3 + 1];
        pos4[i * 4 + 2] = positions[i * 3 + 2];
        pos4[i * 4 + 3] = 0.0f;
    }

    ensureBuffer(ssbo0, pos4.size() * sizeof(float), GL_DYNAMIC_DRAW);//点
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        pos4.size() * sizeof(float), pos4.data());

	ensureBuffer(ssbo1, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);//邻域偏移
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        offsets.size() * sizeof(int), offsets.data());

	ensureBuffer(ssbo2, neighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);//邻域点索引
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        neighbors.size() * sizeof(int), neighbors.data());

	ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);//输入数据
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        phi.size() * sizeof(float), phi.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progWLS);

    GLint luN = glGetUniformLocation(progWLS, "uN");
    GLint luE = glGetUniformLocation(progWLS, "uWExp");
    GLint luL = glGetUniformLocation(progWLS, "uLambda");
    GLint luC = glGetUniformLocation(progWLS, "uNumComponents");

	glUniform1i(luN, (int)np);//将点数传递给着色器
	glUniform1f(luE, p.wExponent);//将权重指数传递给着色器
	glUniform1f(luL, p.lambda);//将正则化参数传递给着色器
	glUniform1i(luC, comps);//将数据分量数传递给着色器

    GLuint gx = (GLuint)((np + 255) / 256);

	if (enableGpuTiming) {//如果启用GPU计时，开始计时
        if (timeQuery == 0) glGenQueries(1, &timeQuery);
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	if (enableGpuTiming) {//如果启用GPU计时，结束计时并获取结果
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6; // ns -> ms
    }

	//从GPU缓冲区读取计算结果到outGrad数组
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        outGrad.size() * sizeof(float), outGrad.data());

    return true;
}

void GLGradientEngine::setEnableGpuTiming(bool on)
{
    enableGpuTiming = on;
	//如果启用GPU计时但查询对象尚未创建，则创建一个新的查询对象
    if (enableGpuTiming && timeQuery == 0) {
        glGenQueries(1, &timeQuery);
    }
}

double GLGradientEngine::getLastGpuTimeMs() const
{
    return lastGpuTimeMs;
}