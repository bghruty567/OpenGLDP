#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glad/glad.h>


class GLGradientEngine {
public:
    //瑙勫垯缃戞牸鍙傛暟锛屽悗涓や釜鍙傛暟鐩墠涓嶇敤
    struct RegularParams {
        int dims[3]; // 缁村害淇℃伅 [nx, ny, nz]
        float origin[3]; // 鍘熺偣鍧愭爣 [ox, oy, oz]
        float spacing[3]; // 缃戞牸闂磋窛 [sx, sy, sz]
    };
    //闈炵粨鏋勫寲缃戞牸姊害璁＄畻鍙傛暟
    struct WLSParams {
        float wExponent = 1.0f; // 鏉冮噸鎸囨暟锛屾帶鍒舵潈閲嶈“鍑忛€熷害
        float lambda = 1e-3f; // 姝ｅ垯鍖栧弬鏁帮紝鎺у埗鏁板€肩ǔ瀹氭€у拰杩囨嫙鍚堢▼搴?
        float planeEigenRatio = 0.06f; // 灞€閮ㄨ繎鍏遍潰闃堝€?
        float lineEigenRatio = 0.02f; // 灞€閮ㄨ繎鍏辩嚎闃堝€?
        float lambdaAmplify = 4.0f; // 浣庤川閲忛偦鍩熸椂鐨勬鍒欏寲鏀惧ぇ鍊?
        int enableAdaptiveDimension = 1;
        int enableAdaptiveRegularization = 1;
    };
    GLGradientEngine();
    ~GLGradientEngine();
    /*
    * @brief 璁剧疆鐫€鑹插櫒鐩綍
    * @param dir 鐫€鑹插櫒鏂囦欢鎵€鍦ㄧ洰褰曡矾寰?
    */
    bool setShaderDir(const std::string& dir);
    /*
    * @brief 鍒濆鍖朑LGradientEngine锛岀紪璇戙€侀摼鎺ョ潃鑹插櫒绛?
    */
    bool init();
    /*
    * @brief 閲婃斁璧勬簮
    */
    void release();
    /*
    * @brief 璁＄畻瑙勫垯缃戞牸鐨勬搴?
    * @param positions 杈撳叆鐐逛綅缃暟缁勶紝鏍煎紡涓篬x0,y0,z0,x1,y1,z1,...]
    * @param values 杈撳叆鏍囬噺鍊兼暟缁勶紝鏍煎紡涓篬v0,v1,...]鎴朳v0_0,v0_1,...,v1_0,v1_1,...]锛堝崟缁勪欢鎴栧缁勪欢锛?
    * @param p 瑙勫垯缃戞牸鍙傛暟锛屽寘鍚淮搴︺€佸師鐐瑰拰闂磋窛淇℃伅
    * @param outGrad 杈撳嚭姊害鏁扮粍锛屾牸寮忎负[gx0,gy0,gz0,gx1,gy1,gz1,...]鎴朳gx0_0,gy0_0,gz0_0,gx0_1,gy0_1,gz0_1,...]锛堝崟缁勪欢鎴栧缁勪欢锛?
    */
    bool computeRegularFD(const std::vector<float>& positions,
                          const std::vector<float>& values,
                          const RegularParams& p,
                          std::vector<float>& outGrad);
    /*
    * @brief 璁＄畻闈炵粨鏋勫寲缃戞牸鐨勬搴︼紝浣跨敤浼犵粺鍔犳潈鏈€灏忎簩涔樻硶
    */
    bool computeUnstructuredWLS(const std::vector<float>& positions,
                                const std::vector<int>& offsets,
                                const std::vector<int>& neighbors,
                                const std::vector<float>& phi,
                                const WLSParams& p,
                                std::vector<float>& outGrad);
    /*
    * @brief 璁＄畻浼樺寲鍚庣殑闈炵粨鏋勫寲缃戞牸姊害锛屽寘鍚紭鍖栭偦鍩熴€佸眬閮ㄧ淮搴﹁嚜閫傚簲鍜屽姩鎬佹鍒欏寲
    */
    bool computeUnstructuredAdaptiveWLS(const std::vector<float>& positions,
                                        const std::vector<int>& offsets,
                                        const std::vector<int>& neighbors,
                                        const std::vector<float>& phi,
                                        const std::vector<float>& frames,
                                        const std::vector<std::uint32_t>& dimTags,
                                        const std::vector<float>& quality,
                                        const std::vector<float>& meanNeighborDistance,
                                        const WLSParams& p,
                                        std::vector<float>& outGrad);

    /*
    * @brief 璁剧疆鏄惁鍚敤GPU璁℃椂锛屽惎鐢ㄥ悗computeRegularFD銆乧omputeUnstructuredWLS鍜孋omputeUnstructuredAdaptiveWLS浼氬湪GPU涓婃祴閲忔墽琛屾椂闂?
    * @param on 鏄惁鍚敤GPU璁℃椂
    */
    void   setEnableGpuTiming(bool on);

    /*
    * @brief 鑾峰彇涓婁竴娆PU璁＄畻鐨勬墽琛屾椂闂达紝鍗曚綅涓烘绉掞紝浠呭綋鍚敤GPU璁℃椂鍚庢湁鏁?
    */
    double getLastGpuTimeMs() const;
private:
    std::string shaderDir;//鐫€鑹插櫒鐩綍璺緞
    GLuint progRegular = 0;//瑙勫垯缃戞牸璁＄畻鐫€鑹插櫒绋嬪簭ID
    GLuint progWLS = 0;//闈炵粨鏋勫寲缃戞牸浼犵粺WLS璁＄畻鐫€鑹插櫒绋嬪簭ID
    GLuint progAdaptiveWLS = 0;//闈炵粨鏋勫寲缃戞牸浼樺寲WLS璁＄畻鐫€鑹插櫒绋嬪簭ID
    //璁＄畻杩囩▼涓娇鐢ㄧ殑SSBO瀵硅薄ID
    //FD璁＄畻锛歴sbo0瀛樺偍杈撳叆鐐逛綅缃紝ssbo1瀛樺偍杈撳叆鏁版嵁锛宻sbo2瀛樺偍杈撳嚭姊害
    //WLS璁＄畻锛歴sbo0瀛樺偍杈撳叆鐐逛綅缃紝ssbo1瀛樺偍閭诲煙鍋忕Щ锛宻sbo2瀛樺偍閭诲煙鐐圭储寮曪紝ssbo3瀛樺偍杈撳叆鏁版嵁锛宻sbo4瀛樺偍杈撳嚭姊害
    //AdaptiveWLS鍦ㄦ鍩虹涓婃柊澧瀍rame/dim/quality/distance绛夎緟鍔╃紦鍐插尯
    GLuint ssbo0 = 0, ssbo1 = 0, ssbo2 = 0, ssbo3 = 0, ssbo4 = 0, ssbo5 = 0, ssbo6 = 0, ssbo7 = 0, ssbo8 = 0;
    /*
    * @brief 浠庢枃浠剁紪璇戝苟閾炬帴璁＄畻鐫€鑹插櫒锛岃繑鍥炵▼搴廔D锛屽け璐ヨ繑鍥?
    * @param path 鐫€鑹插櫒鏂囦欢璺緞
    */
    GLuint buildComputeFromFile(const std::string& path);
    /*
    * @brief 纭繚SSBO缂撳啿鍖哄瓨鍦ㄥ苟鍏锋湁瓒冲澶у皬锛屽鏋滃綋鍓嶇紦鍐插尯涓嶈冻鍒欓噸鏂板垎閰?
    * @param id SSBO瀵硅薄ID寮曠敤锛屽鏋滃綋鍓嶄负0鍒欏垱寤烘柊缂撳啿鍖?
    * @param bytes 闇€瑕佺殑缂撳啿鍖哄ぇ灏忥紝鍗曚綅涓哄瓧鑺?
    * @param usage OpenGL缂撳啿鍖轰娇鐢ㄦā寮忥紝濡侴L_DYNAMIC_DRAW绛?
    */
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);

    bool   enableGpuTiming = false;//鏄惁鍚敤GPU璁℃椂
    GLuint timeQuery = 0;//鐢ㄤ簬GPU璁℃椂鐨勬煡璇㈠璞D
    double lastGpuTimeMs = 0.0;//涓婁竴娆PU璁＄畻鐨勬墽琛屾椂闂达紝鍗曚綅涓烘绉?
};
