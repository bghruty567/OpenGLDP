#include "MainWindow.h"

#include <algorithm>

#include <QCoreApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
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
        appendLog("Failed to initialize OpenGL or shaders.");
        QMessageBox::critical(
            this,
            "Initialization Failed",
            "CAEProcessingFacade initialization failed.\nCheck OpenGL and the Shaders folder.");
    }
}

void MainWindow::buildUi()
{
    setWindowTitle("OpenGLDP");

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);

    auto* splitter = new QSplitter(this);
    rootLayout->addWidget(splitter);
    setCentralWidget(central);

    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    m_openBtn = new QPushButton("Open VTK", this);
    m_exportBtn = new QPushButton("Export Current Dataset", this);
    m_datasetList = new QListWidget(this);
    m_summaryLabel = new QLabel("No dataset loaded", this);

    leftLayout->addWidget(m_openBtn);
    leftLayout->addWidget(m_exportBtn);
    leftLayout->addWidget(new QLabel("Datasets", this));
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

    m_computeBtn = new QPushButton("Compute Gradient", this);

        m_msLevelsSpin = new QSpinBox(this);
    m_msLevelsSpin->setRange(1, 3);
    m_msLevelsSpin->setValue(3);

    m_msIterSpin = new QSpinBox(this);
    m_msIterSpin->setRange(1, 8);
    m_msIterSpin->setValue(1);

    m_msSpatialSigmaFactorSpin = new QDoubleSpinBox(this);
    m_msSpatialSigmaFactorSpin->setRange(0.1, 20.0);
    m_msSpatialSigmaFactorSpin->setDecimals(3);
    m_msSpatialSigmaFactorSpin->setValue(1.5);

    m_msRangeSigmaFactorSpin = new QDoubleSpinBox(this);
    m_msRangeSigmaFactorSpin->setRange(0.01, 10.0);
    m_msRangeSigmaFactorSpin->setDecimals(3);
    m_msRangeSigmaFactorSpin->setValue(0.5);

    m_msLevelScaleSpin = new QDoubleSpinBox(this);
    m_msLevelScaleSpin->setRange(1.1, 5.0);
    m_msLevelScaleSpin->setDecimals(3);
    m_msLevelScaleSpin->setValue(1.8);

    m_msEdgeSigmaFactorSpin = new QDoubleSpinBox(this);
    m_msEdgeSigmaFactorSpin->setRange(0.01, 10.0);
    m_msEdgeSigmaFactorSpin->setDecimals(3);
    m_msEdgeSigmaFactorSpin->setValue(0.35);

    m_msGain0Spin = new QDoubleSpinBox(this);
    m_msGain0Spin->setRange(0.0, 3.0);
    m_msGain0Spin->setDecimals(3);
    m_msGain0Spin->setValue(1.0);

    m_msGain1Spin = new QDoubleSpinBox(this);
    m_msGain1Spin->setRange(0.0, 3.0);
    m_msGain1Spin->setDecimals(3);
    m_msGain1Spin->setValue(0.75);

    m_msGain2Spin = new QDoubleSpinBox(this);
    m_msGain2Spin->setRange(0.0, 3.0);
    m_msGain2Spin->setDecimals(3);
    m_msGain2Spin->setValue(0.5);

    m_msStoreIntermediateCheck = new QCheckBox("Store intermediate smooth/detail arrays", this);
    m_msStoreIntermediateCheck->setChecked(true);

    m_optimizeBtn = new QPushButton("Run MultiScale Optimization", this);


    form->addRow("Association", m_assocBox);
    form->addRow("Array", m_arrayBox);
    form->addRow("Method", m_methodBox);
    form->addRow("WLS Exponent", m_wExpSpin);
    form->addRow("WLS Lambda", m_lambdaSpin);
    form->addRow("Visible Component", m_componentSpin);
    form->addRow("", m_computeBtn);

    auto* optTitle = new QLabel("Data Optimization", this);
    optTitle->setStyleSheet("font-weight: bold;");
    form->addRow(optTitle);

    form->addRow("MS Levels", m_msLevelsSpin);
    form->addRow("MS Iter/Level", m_msIterSpin);
    form->addRow("Spatial Sigma Factor", m_msSpatialSigmaFactorSpin);
    form->addRow("Range Sigma Factor", m_msRangeSigmaFactorSpin);
    form->addRow("Level Scale", m_msLevelScaleSpin);
    form->addRow("Edge Sigma Factor", m_msEdgeSigmaFactorSpin);
    form->addRow("Detail Gain L0", m_msGain0Spin);
    form->addRow("Detail Gain L1", m_msGain1Spin);
    form->addRow("Detail Gain L2", m_msGain2Spin);
    form->addRow("", m_msStoreIntermediateCheck);
    form->addRow("", m_optimizeBtn);


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
    connect(m_optimizeBtn, &QPushButton::clicked, this, &MainWindow::computeMultiScaleOptimization);


    connect(m_datasetList, &QListWidget::currentRowChanged, this, &MainWindow::handleDatasetChanged);
    connect(m_assocBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::handleAssociationChanged);
    connect(m_arrayBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::handleArrayChanged);
    connect(m_componentSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { renderSelectedArray(); });
}

void MainWindow::appendLog(const QString& text)
{
    m_log->appendPlainText(text);
}

QString MainWindow::selectedDatasetId() const
{
    auto* item = m_datasetList->currentItem();
    if (!item) {
        return {};
    }
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
    case 1:
        return CAEGradientMethod::FiniteDifference;
    case 2:
        return CAEGradientMethod::WeightedLeastSquares;
    default:
        return CAEGradientMethod::Auto;
    }
}

void MainWindow::openFile()
{
    if (!m_initialized) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, "Open VTK File", QString(), "VTK legacy (*.vtk)");

    if (filePath.isEmpty()) {
        return;
    }

    const std::string dsId = m_facade.loadDatasetFromVTKFile(toStdString(filePath));
    if (dsId.empty()) {
        QMessageBox::warning(this, "Load Failed", "Failed to load the VTK file.");
        return;
    }

    auto* item = new QListWidgetItem(QFileInfo(filePath).fileName(), m_datasetList);
    item->setData(Qt::UserRole, QString::fromStdString(dsId));
    m_datasetList->setCurrentItem(item);

    appendLog("Loaded dataset: " + filePath);
}

void MainWindow::exportCurrentDataset()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, "Export VTK File", QString(), "VTK legacy (*.vtk)");

    if (outPath.isEmpty()) {
        return;
    }

    const bool ok = m_facade.saveDatasetToVTKFile(
        toStdString(dsId), toStdString(outPath), false);

    if (!ok) {
        QMessageBox::warning(this, "Export Failed", "Failed to export the current dataset.");
        return;
    }

    appendLog("Exported dataset: " + outPath);
}

void MainWindow::computeGradient()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty() || m_arrayBox->currentText().isEmpty()) {
        return;
    }

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
        QMessageBox::warning(this, "Compute Failed", "Gradient computation failed.");
        return;
    }

    appendLog(QString("Computed: %1, wall=%2 ms, gpu=%3 ms")
                  .arg(QString::fromStdString(meta.resultArrayName))
                  .arg(meta.computeWallMs, 0, 'f', 3)
                  .arg(meta.computeGpuMs, 0, 'f', 3));

    refreshFieldList();
    refreshSummary();
    refreshResultLog();

    const int idx = m_arrayBox->findText(QString::fromStdString(meta.resultArrayName));
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }

    renderSelectedArray();
}

void MainWindow::computeMultiScaleOptimization()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty() || m_arrayBox->currentText().isEmpty()) {
        return;
    }

    CAEMultiScaleRequest req;
    req.datasetId = toStdString(dsId);
    req.inputArrayName = toStdString(m_arrayBox->currentText());
    req.association = currentAssociation();

    req.levels = m_msLevelsSpin->value();
    req.iterationsPerLevel = m_msIterSpin->value();
    req.spatialSigmaFactor = static_cast<float>(m_msSpatialSigmaFactorSpin->value());
    req.rangeSigmaFactor = static_cast<float>(m_msRangeSigmaFactorSpin->value());
    req.levelScale = static_cast<float>(m_msLevelScaleSpin->value());
    req.edgeSigmaFactor = static_cast<float>(m_msEdgeSigmaFactorSpin->value());

    req.detailGain0 = static_cast<float>(m_msGain0Spin->value());
    req.detailGain1 = static_cast<float>(m_msGain1Spin->value());
    req.detailGain2 = static_cast<float>(m_msGain2Spin->value());

    req.storeIntermediate = m_msStoreIntermediateCheck->isChecked();

    CAEMultiScaleResultMeta meta;
    const bool ok = m_facade.computeMultiScaleDecompositionAndFusion(req, meta);
    if (!ok) {
        QMessageBox::warning(this, "Optimization Failed", "Multi-scale optimization failed.");
        return;
    }

    appendLog(QString("Optimized: %1 -> %2, levels=%3, wall=%4 ms, gpu=%5 ms")
                  .arg(QString::fromStdString(meta.sourceArrayName))
                  .arg(QString::fromStdString(meta.fusedArrayName))
                  .arg(meta.numLevels)
                  .arg(meta.computeWallMs, 0, 'f', 3)
                  .arg(meta.computeGpuMs, 0, 'f', 3));

    for (const auto& name : meta.smoothArrayNames) {
        appendLog("  smooth: " + QString::fromStdString(name));
    }
    for (const auto& name : meta.detailArrayNames) {
        appendLog("  detail: " + QString::fromStdString(name));
    }

    refreshFieldList();
    refreshSummary();
    refreshResultLog();

    const int idx = m_arrayBox->findText(QString::fromStdString(meta.fusedArrayName));
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }

    m_componentSpin->setValue(0);
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
    if (dsId.isEmpty()) {
        return;
    }

    std::vector<CAEFieldInfo> fields;
    if (!m_facade.listFields(toStdString(dsId), currentAssociation(), fields)) {
        return;
    }

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
    if (dsId.isEmpty()) {
        return;
    }

    std::vector<CAEFieldInfo> fields;
    if (!m_facade.listFields(toStdString(dsId), currentAssociation(), fields)) {
        return;
    }

    for (const auto& f : fields) {
        m_arrayBox->addItem(QString::fromStdString(f.name));
    }

    handleArrayChanged();
}

void MainWindow::refreshSummary()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        m_summaryLabel->setText("No dataset selected");
        return;
    }

    CAEDatasetSummary s;
    if (!m_facade.getDatasetSummary(toStdString(dsId), s)) {
        m_summaryLabel->setText("Failed to read dataset summary");
        return;
    }

    m_summaryLabel->setText(
        QString("Name: %1\nPoints: %2\nCells: %3\nGrid: %4")
            .arg(QString::fromStdString(s.displayName))
            .arg(static_cast<qulonglong>(s.pointCount))
            .arg(static_cast<qulonglong>(s.cellCount))
            .arg(s.gridClass == CAEGridClass::Regular ? "Regular" : "Unstructured"));
}

void MainWindow::refreshResultLog()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        return;
    }

    CAEDatasetSummary s;
    if (!m_facade.getDatasetSummary(toStdString(dsId), s)) {
        return;
    }

    appendLog("Result count: " + QString::number(static_cast<int>(s.results.size())));
}

void MainWindow::renderSelectedArray()
{
    const QString dsId = selectedDatasetId();
    const QString arrayName = m_arrayBox->currentText();

    if (dsId.isEmpty() || arrayName.isEmpty()) {
        return;
    }

    vtkSmartPointer<vtkDataSet> ds;
    if (!m_facade.exportDatasetToVTK(toStdString(dsId), ds) || !ds) {
        return;
    }

    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputData(ds);

    if (currentAssociation() == CAEFieldAssociation::Point) {
        mapper->SetScalarModeToUsePointFieldData();
    } else {
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
