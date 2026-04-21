#pragma once

#include <QMainWindow>
#include "CAEProcessingFacade.h"
#include <vtkSmartPointer.h>

class QListWidget;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;

/// 项目的桌面主窗口。
///
/// 这个类本身不直接实现梯度算法或滤波算法，
/// 它的职责更像是“界面层适配器”：
/// 1. 从界面控件收集参数；
/// 2. 组装成 `CAEGradientRequest` / `CAEMultiScaleRequest`；
/// 3. 调用 `CAEProcessingFacade`；
/// 4. 再把结果交给 VTK 做显示，并写入日志。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void bindSignals();
    void appendLog(const QString& text);
    QString selectedDatasetId() const;
    CAEFieldAssociation currentAssociation() const;
    CAEGradientMethod currentMethod() const;
    void refreshFieldList();
    void refreshSummary();
    void refreshResultLog();
    void renderSelectedArray();

private slots:
    void openFile();
    void exportCurrentDataset();
    void computeGradient();
    void computeMultiScaleOptimization();
    void handleDatasetChanged();
    void handleAssociationChanged();
    void handleArrayChanged();

private:
    CAEProcessingFacade m_facade; ///< 统一的数据加载/计算/导出门面
    bool m_initialized = false;   ///< OpenGL/Shader 是否初始化成功

    // 左侧：数据集列表与摘要
    QListWidget* m_datasetList = nullptr;
    QLabel* m_summaryLabel = nullptr;

    // 中间：梯度计算参数
    QComboBox* m_assocBox = nullptr;
    QComboBox* m_arrayBox = nullptr;
    QComboBox* m_methodBox = nullptr;
    QDoubleSpinBox* m_wExpSpin = nullptr;
    QDoubleSpinBox* m_lambdaSpin = nullptr;
    QSpinBox* m_componentSpin = nullptr;

    QPushButton* m_openBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_computeBtn = nullptr;

    // 中间：多尺度优化参数
    QPushButton* m_optimizeBtn = nullptr;

    QSpinBox* m_msLevelsSpin = nullptr;
    QSpinBox* m_msIterSpin = nullptr;

    QDoubleSpinBox* m_msSpatialSigmaFactorSpin = nullptr;
    QDoubleSpinBox* m_msRangeSigmaFactorSpin = nullptr;
    QDoubleSpinBox* m_msLevelScaleSpin = nullptr;
    QDoubleSpinBox* m_msEdgeSigmaFactorSpin = nullptr;

    QDoubleSpinBox* m_msGain0Spin = nullptr;
    QDoubleSpinBox* m_msGain1Spin = nullptr;
    QDoubleSpinBox* m_msGain2Spin = nullptr;

    QCheckBox* m_msStoreIntermediateCheck = nullptr;


    // 右侧：VTK 视图与运行日志
    QVTKOpenGLNativeWidget* m_vtkWidget = nullptr;
    QPlainTextEdit* m_log = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
};

