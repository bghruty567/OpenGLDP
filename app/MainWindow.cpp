#include "MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>

namespace
{
    std::string toStdString(const QString& s)
    {
        return s.toLocal8Bit().toStdString();
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const QString shaderDir =
        QDir(QCoreApplication::applicationDirPath()).filePath("Shaders");

    m_initialized = m_facade.initialize(toStdString(shaderDir));

    buildUi();
    bindSignals();

    if (!m_initialized) {
        appendLog("OpenGL/Shader 初始化失败，请先确认 Shaders 目录已复制到构建输出目录。");
        QMessageBox::critical(this, "初始化失败",
            "CAEProcessingFacade 初始化失败。\n请检查 OpenGL 环境和 Shaders 目录。");
    }
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);

    auto* splitter = new QSplitter(this);
    rootLayout->addWidget(splitter);
    setCentralWidget(central);

    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    m_openBtn = new QPushButton("打开 VTK", this);
    m_exportBtn = new QPushButton("导出当前数据集", this);
    m_datasetList = new QListWidget(this);
    m_summaryLabel = new QLabel("未加载数据集", this);

    leftLayout->addWidget(m_openBtn);
    leftLayout->addWidget(m_exportBtn);
    leftLayout->addWidget(new QLabel("数据集列表", this));
    leftLayout->addWidget(m_datasetList);
    leftLayout->addWidget(m_summaryLabel);

    auto* middlePanel = new QWidget(this);
    auto* form = new QFormLayout(middlePanel);

    m_assocBox = new QComboBox(this);
    m_assocBox->addItem("Point");
    m_assocBox->addItem("Cell");

    m_arrayBox = new QComboBox(this);

    m_methodBox = new QComboBox(this);
    m_methodBox->addItem("Auto");
    m_methodBox->addItem("FiniteDifference");
    m_methodBox->addItem("WeightedLeastSquares");

    m_wExpSpin = new QDoubleSpinBox(this);
    m_wExpSpin->setRange(0.1, 8.0);
    m_wExpSpin->setDecimals(3);
    m_wExpSpin->setValue(1.0);

    m_lambdaSpin = new QDoubleSpinBox(this);
    m_lambdaSpin->setRange(1e-8, 1.0);
    m_lambdaSpin->setDecimals(8);
    m_lambdaSpin->setValue(1e-3);

    m_componentSpin = new QSpinBox(this);
    m_componentSpin->setRange(0, 0);

    m_computeBtn = new QPushButton("计算梯度", this);

    form->addRow("关联类型", m_assocBox);
    form->addRow("数组", m_arrayBox);
    form->addRow("方法", m_methodBox);
    form->addRow("WLS 指数", m_wExpSpin);
    form->addRow("WLS 正则", m_lambdaSpin);
    form->addRow("显示分量", m_componentSpin);
    form->addRow("", m_computeBtn);

    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.15, 0.18, 0.22);
    m_renderWindow->AddRenderer(m_renderer);
    m_vtkWidget->setRenderWindow(m_renderWindow);

    rightLayout->addWidget(m_vtkWidget, 1);
    rightLayout->addWidget(m_log, 1);

    splitter->addWidget(leftPanel);
    splitter->addWidget(middlePanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(2, 1);
}

void MainWindow::bindSignals()
{
    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::exportCurrentDataset);
    connect(m_computeBtn, &QPushButton::clicked, this, &MainWindow::computeGradient);

    connect(m_datasetList, &QListWidget::currentRowChanged, this, &MainWindow::handleDatasetChanged);
    connect(m_assocBox, &QComboBox::currentIndexChanged, this, &MainWindow::handleAssociationChanged);
    connect(m_arrayBox, &QComboBox::currentIndexChanged, this, &MainWindow::handleArrayChanged);
    connect(m_componentSpin, &QSpinBox::valueChanged, this, [this]() { renderSelectedArray(); });
}

void MainWindow::appendLog(const QString& text)
{
    m_log->appendPlainText(text);
}

QString MainWindow::selectedDatasetId() const
{
    auto* item = m_datasetList->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

CAEFieldAssociation MainWindow::currentAssociation() const
{
    return m_assocBox->currentIndex() == 0
        ? CAEFieldAssociation::Point
        : CAEFieldAssociation::Cell;
}

CAEGradientMethod MainWindow::currentMethod() const
{
    switch (m_methodBox->currentIndex()) {
    case 1: return CAEGradientMethod::FiniteDifference;
    case 2: return CAEGradientMethod::WeightedLeastSquares;
    default: return CAEGradientMethod::Auto;
    }
}

void MainWindow::openFile()
{
    if (!m_initialized) return;

    const QString filePath = QFileDialog::getOpenFileName(
        this, "打开 VTK 文件", QString(), "VTK legacy (*.vtk)");

    if (filePath.isEmpty()) return;

    const std::string dsId = m_facade.loadDatasetFromVTKFile(toStdString(filePath));
    if (dsId.empty()) {
        QMessageBox::warning(this, "加载失败", "VTK 文件加载失败。");
        return;
    }

    auto* item = new QListWidgetItem(QFileInfo(filePath).fileName(), m_datasetList);
    item->setData(Qt::UserRole, QString::fromStdString(dsId));
    m_datasetList->setCurrentItem(item);

    appendLog("加载数据集: " + filePath);
}

void MainWindow::exportCurrentDataset()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) return;

    const QString outPath = QFileDialog::getSaveFileName(
        this, "导出 VTK 文件", QString(), "VTK legacy (*.vtk)");

    if (outPath.isEmpty()) return;

    const bool ok = m_facade.saveDatasetToVTKFile(
        toStdString(dsId), toStdString(outPath), true);

    if (!ok) {
        QMessageBox::warning(this, "导出失败", "当前数据集导出失败。");
        return;
    }

    appendLog("导出成功: " + outPath);
}

void MainWindow::computeGradient()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty() || m_arrayBox->currentText().isEmpty()) return;

    CAEGradientRequest req;
    req.datasetId = toStdString(dsId);
    req.inputArrayName = toStdString(m_arrayBox->currentText());
    req.association = currentAssociation();
    req.method = currentMethod();
    req.wlsExponent = static_cast<float>(m_wExpSpin->value());
    req.wlsLambda = static_cast<float>(m_lambdaSpin->value());

    CAEGradientResultMeta meta;
    const bool ok = m_facade.computeGradient(req, meta);

    if (!ok) {
        QMessageBox::warning(this, "计算失败", "梯度计算失败。");
        return;
    }

    appendLog(QString("计算完成: %1, wall= %2 ms, gpu= %3 ms")
        .arg(QString::fromStdString(meta.resultArrayName))
        .arg(meta.computeWallMs, 0, 'f', 3)
        .arg(meta.computeGpuMs, 0, 'f', 3));

    refreshFieldList();
    refreshSummary();
    refreshResultLog();

    int idx = m_arrayBox->findText(QString::fromStdString(meta.resultArrayName));
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }

    renderSelectedArray();
}

void MainWindow::handleDatasetChanged()
{
    refreshFieldList();
    refreshSummary();
    refreshResultLog();
    renderSelectedArray();
}

void MainWindow::handleAssociationChanged()
{
    refreshFieldList();
    renderSelectedArray();
}

void MainWindow::handleArrayChanged()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) return;

    std::vector<CAEFieldInfo> fields;
    if (!m_facade.listFields(toStdString(dsId), currentAssociation(), fields)) return;

    const QString currentName = m_arrayBox->currentText();
    int comps = 1;
    for (const auto& f : fields) {
        if (QString::fromStdString(f.name) == currentName) {
            comps = f.numComponents;
            break;
        }
    }

    m_componentSpin->setMaximum(std::max(0, comps - 1));
    renderSelectedArray();
}

void MainWindow::refreshFieldList()
{
    m_arrayBox->clear();

    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) return;

    std::vector<CAEFieldInfo> fields;
    if (!m_facade.listFields(toStdString(dsId), currentAssociation(), fields)) return;

    for (const auto& f : fields) {
        m_arrayBox->addItem(QString::fromStdString(f.name));
    }

    handleArrayChanged();
}

void MainWindow::refreshSummary()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        m_summaryLabel->setText("未选择数据集");
        return;
    }

    CAEDatasetSummary s;
    if (!m_facade.getDatasetSummary(toStdString(dsId), s)) {
        m_summaryLabel->setText("读取摘要失败");
        return;
    }

    m_summaryLabel->setText(
        QString("名称: %1\n点数: %2\n单元数: %3\n网格类型: %4")
        .arg(QString::fromStdString(s.displayName))
        .arg(static_cast<qulonglong>(s.pointCount))
        .arg(static_cast<qulonglong>(s.cellCount))
        .arg(s.gridClass == CAEGridClass::Regular ? "Regular" : "Unstructured"));
}

void MainWindow::refreshResultLog()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) return;

    CAEDatasetSummary s;
    if (!m_facade.getDatasetSummary(toStdString(dsId), s)) return;

    appendLog("当前结果数量: " + QString::number(static_cast<int>(s.results.size())));
}

void MainWindow::renderSelectedArray()
{
    const QString dsId = selectedDatasetId();
    const QString arrayName = m_arrayBox->currentText();

    if (dsId.isEmpty() || arrayName.isEmpty()) return;

    vtkSmartPointer<vtkDataSet> ds;
    if (!m_facade.exportDatasetToVTK(toStdString(dsId), ds) || !ds) {
        return;
    }

    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputData(ds);

    if (currentAssociation() == CAEFieldAssociation::Point) {
        mapper->SetScalarModeToUsePointFieldData();
    }
    else {
        mapper->SetScalarModeToUseCellFieldData();
    }

    const std::string arrayStd = toStdString(arrayName);
    mapper->SelectColorArray(arrayStd.c_str());
    mapper->ColorByArrayComponent(arrayStd.c_str(), m_componentSpin->value());
    mapper->ScalarVisibilityOn();

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);

    m_renderer->RemoveAllViewProps();
    m_renderer->AddActor(actor);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}
