#include "MainWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
std::string toStdString(const QString& s)
{
    return s.toLocal8Bit().toStdString();
}

QString gradientMethodLabel(CAEGradientMethod method)
{
    switch (method) {
    case CAEGradientMethod::FiniteDifference:
        return "有限差分";
    case CAEGradientMethod::AdaptiveWeightedLeastSquares:
        return "自适应WLS";
    case CAEGradientMethod::ShapeFunctionDerivatives:
        return "形函数导数";
    default:
        return "自动";
    }
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const QString shaderDir =
        QDir(QCoreApplication::applicationDirPath()).filePath("Shaders");

    m_initialized = m_facade.initialize(toStdString(shaderDir));

    buildUi();
    applyTheme();
    bindSignals();
    updateActionState();

    if (m_initialized) {
        appendLog("OpenGL 预处理引擎初始化完成。");
    } else {
        appendLog("初始化失败，梯度计算与数据优化功能已禁用。");
        QMessageBox::critical(
            this,
            "初始化失败",
            "CAEProcessingFacade 初始化失败。\n请检查 OpenGL 环境和 Shaders 目录。");
    }
}

void MainWindow::buildUi()
{
    setWindowTitle("OpenGLDP");

    auto* central = new QWidget(this);
    central->setObjectName("CentralWidget");
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(24, 20, 24, 24);
    rootLayout->setSpacing(18);
    setCentralWidget(central);

    auto* titleLabel = new QLabel("OpenGLDP", this);
    titleLabel->setObjectName("TitleLabel");

    rootLayout->addWidget(titleLabel);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(18);
    rootLayout->addLayout(contentLayout, 1);

    auto* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(18);
    contentLayout->addLayout(leftColumn, 4);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(18);
    contentLayout->addLayout(rightColumn, 5);

    auto* datasetCard = new QFrame(this);
    datasetCard->setObjectName("Card");
    auto* datasetLayout = new QVBoxLayout(datasetCard);
    datasetLayout->setContentsMargins(20, 20, 20, 20);
    datasetLayout->setSpacing(14);

    auto* datasetTitle = new QLabel("数据集工作区", this);
    datasetTitle->setObjectName("SectionTitle");

    auto* datasetButtonRow = new QHBoxLayout();
    datasetButtonRow->setSpacing(10);

    m_openBtn = new QPushButton("打开 VTK", this);
    m_openBtn->setProperty("role", "primary");

    m_exportBtn = new QPushButton("导出当前数据集", this);
    m_exportBtn->setProperty("role", "export");

    datasetButtonRow->addWidget(m_openBtn);
    datasetButtonRow->addWidget(m_exportBtn);

    auto* datasetListTitle = new QLabel("数据集列表", this);
    datasetListTitle->setObjectName("FieldLabel");

    m_datasetList = new QListWidget(this);
    m_datasetList->setAlternatingRowColors(true);
    m_datasetList->setMinimumHeight(280);

    datasetLayout->addWidget(datasetTitle);
    datasetLayout->addLayout(datasetButtonRow);
    datasetLayout->addWidget(datasetListTitle);
    datasetLayout->addWidget(m_datasetList, 1);
    leftColumn->addWidget(datasetCard, 3);

    auto* summaryCard = new QFrame(this);
    summaryCard->setObjectName("Card");
    auto* summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(20, 20, 20, 20);
    summaryLayout->setSpacing(12);

    auto* summaryTitle = new QLabel("数据集概览", this);
    summaryTitle->setObjectName("SectionTitle");

    m_summaryLabel = new QLabel("未选择数据集。", this);
    m_summaryLabel->setObjectName("SummaryLabel");
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextFormat(Qt::RichText);
    m_summaryLabel->setMinimumHeight(170);
    m_summaryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    summaryLayout->addWidget(summaryTitle);
    summaryLayout->addWidget(m_summaryLabel, 1);
    leftColumn->addWidget(summaryCard, 2);

    auto* processingCard = new QFrame(this);
    processingCard->setObjectName("Card");
    auto* processingLayout = new QVBoxLayout(processingCard);
    processingLayout->setContentsMargins(20, 20, 20, 20);
    processingLayout->setSpacing(14);

    auto* processingTitle = new QLabel("处理操作", this);
    processingTitle->setObjectName("SectionTitle");

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    m_assocBox = new QComboBox(this);
    m_assocBox->addItem("点数据");
    m_assocBox->addItem("单元数据");

    m_arrayBox = new QComboBox(this);
    m_arrayBox->setMinimumWidth(260);

    form->addRow("属性归属", m_assocBox);
    form->addRow("数组", m_arrayBox);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);

    m_computeBtn = new QPushButton("计算梯度", this);
    m_computeBtn->setProperty("role", "primary");

    m_optimizeBtn = new QPushButton("执行数据优化", this);
    m_optimizeBtn->setProperty("role", "accent");

    actionRow->addWidget(m_computeBtn);
    actionRow->addWidget(m_optimizeBtn);

    processingLayout->addWidget(processingTitle);
    processingLayout->addLayout(form);
    processingLayout->addLayout(actionRow);
    rightColumn->addWidget(processingCard, 2);

    auto* logCard = new QFrame(this);
    logCard->setObjectName("Card");
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(20, 20, 20, 20);
    logLayout->setSpacing(12);

    auto* logTitle = new QLabel("运行日志", this);
    logTitle->setObjectName("SectionTitle");

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(320);

    logLayout->addWidget(logTitle);
    logLayout->addWidget(m_log, 1);
    rightColumn->addWidget(logCard, 3);
}

void MainWindow::applyTheme()
{
    setStyleSheet(R"(
QWidget#CentralWidget {
    background: #edf3f8;
    color: #0f172a;
    font-family: "Microsoft YaHei UI";
    font-size: 14px;
}
QLabel#TitleLabel {
    font-size: 30px;
    font-weight: 700;
    color: #0f172a;
    letter-spacing: 0.5px;
}
QFrame#Card {
    background: #ffffff;
    border: 1px solid #dbe4ee;
    border-radius: 18px;
}
QLabel#SectionTitle {
    font-size: 18px;
    font-weight: 700;
    color: #0f172a;
}
QLabel#FieldLabel {
    color: #334155;
    font-weight: 600;
}
QLabel#SummaryLabel {
    background: #f8fbff;
    border: 1px solid #dbe4ee;
    border-radius: 14px;
    padding: 14px;
    color: #1e293b;
}
QPushButton {
    border: none;
    border-radius: 12px;
    padding: 10px 16px;
    background: #e2e8f0;
    color: #0f172a;
    font-weight: 600;
}
QPushButton:hover {
    background: #d8e1eb;
}
QPushButton:pressed {
    background: #cbd5e1;
}
QPushButton:disabled {
    background: #edf2f7;
    color: #94a3b8;
}
QPushButton[role="primary"] {
    background: #2563eb;
    color: #ffffff;
}
QPushButton[role="primary"]:hover {
    background: #1d4ed8;
}
QPushButton[role="accent"] {
    background: #0f766e;
    color: #ffffff;
}
QPushButton[role="accent"]:hover {
    background: #115e59;
}
QPushButton[role="export"] {
    background: #0891b2;
    color: #ffffff;
}
QPushButton[role="export"]:hover {
    background: #0e7490;
}
QComboBox,
QListWidget,
QPlainTextEdit {
    background: #f8fbff;
    border: 1px solid #d7e0ea;
    border-radius: 12px;
    padding: 8px 10px;
}
QComboBox {
    min-height: 22px;
}
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QListWidget {
    outline: none;
}
QListWidget::item {
    padding: 8px 6px;
    border-radius: 8px;
}
QListWidget::item:selected {
    background: #dbeafe;
    color: #1d4ed8;
}
QPlainTextEdit {
    background: #0f172a;
    color: #dbeafe;
    font-family: "Consolas";
    font-size: 13px;
    selection-background-color: #1d4ed8;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 4px;
}
QScrollBar::handle:vertical {
    background: #bfd0e1;
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0px;
}
)");
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
}

void MainWindow::appendLog(const QString& text)
{
    const QString time = QTime::currentTime().toString("HH:mm:ss");
    m_log->appendPlainText(QString("[%1] %2").arg(time, text));
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
    return CAEGradientMethod::Auto;
}

void MainWindow::openFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, "打开 VTK 文件", QString(), "VTK 文件 (*.vtk)");

    if (filePath.isEmpty()) {
        return;
    }

    const std::string dsId = m_facade.loadDatasetFromVTKFile(toStdString(filePath));
    if (dsId.empty()) {
        QMessageBox::warning(this, "加载失败", "VTK 文件加载失败。");
        return;
    }

    auto* item = new QListWidgetItem(QFileInfo(filePath).fileName(), m_datasetList);
    item->setData(Qt::UserRole, QString::fromStdString(dsId));
    item->setToolTip(filePath);
    m_datasetList->setCurrentItem(item);

    appendLog("已加载数据集: " + filePath);
}

void MainWindow::exportCurrentDataset()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, "导出 VTK 文件", QString(), "VTK 文件 (*.vtk)");

    if (outPath.isEmpty()) {
        return;
    }

    const bool ok = m_facade.saveDatasetToVTKFile(
        toStdString(dsId), toStdString(outPath), true);
    if (!ok) {
        QMessageBox::warning(this, "导出失败", "当前数据集导出失败。");
        return;
    }

    appendLog("已导出数据集: " + outPath);
}

void MainWindow::computeGradient()
{
    const QString dsId = selectedDatasetId();
    const QString arrayName = m_arrayBox->currentText();
    if (dsId.isEmpty() || arrayName.isEmpty()) {
        return;
    }

    CAEGradientRequest req;
    req.datasetId = toStdString(dsId);
    req.inputArrayName = toStdString(arrayName);
    req.association = currentAssociation();
    req.method = currentMethod();
    req.wlsExponent = 1.0f;
    req.wlsLambda = 1e-3f;

    CAEGradientResultMeta meta;
    const bool ok = m_facade.computeGradient(req, meta);
    if (!ok) {
        QMessageBox::warning(this, "计算失败", "梯度计算失败。");
        return;
    }

    appendLog(QString("梯度计算完成: %1 | 方法=%2 | 总耗时=%3 ms | GPU耗时=%4 ms")
                  .arg(QString::fromStdString(meta.resultArrayName))
                  .arg(gradientMethodLabel(meta.method))
                  .arg(meta.computeWallMs, 0, 'f', 3)
                  .arg(meta.computeGpuMs, 0, 'f', 3));

    refreshFieldList();
    refreshSummary();
    refreshResultLog();

    const int idx = m_arrayBox->findText(QString::fromStdString(meta.resultArrayName));
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }
}

void MainWindow::computeMultiScaleOptimization()
{
    const QString dsId = selectedDatasetId();
    const QString arrayName = m_arrayBox->currentText();
    if (dsId.isEmpty() || arrayName.isEmpty()) {
        return;
    }

    CAEMultiScaleRequest req;
    req.datasetId = toStdString(dsId);
    req.inputArrayName = toStdString(arrayName);
    req.association = currentAssociation();
    req.levels = 3;
    req.iterationsPerLevel = 1;
    req.spatialSigmaFactor = 1.5f;
    req.rangeSigmaFactor = 0.5f;
    req.levelScale = 1.8f;
    req.edgeSigmaFactor = 0.35f;
    req.detailGain0 = 1.0f;
    req.detailGain1 = 0.75f;
    req.detailGain2 = 0.5f;
    req.storeIntermediate = true;

    CAEMultiScaleResultMeta meta;
    const bool ok = m_facade.computeMultiScaleDecompositionAndFusion(req, meta);
    if (!ok) {
        QMessageBox::warning(this, "处理失败", "数据优化失败。");
        return;
    }

    appendLog(QString("数据优化完成: %1 -> %2 | 总耗时=%3 ms | GPU耗时=%4 ms")
                  .arg(QString::fromStdString(meta.sourceArrayName))
                  .arg(QString::fromStdString(meta.fusedArrayName))
                  .arg(meta.computeWallMs, 0, 'f', 3)
                  .arg(meta.computeGpuMs, 0, 'f', 3));

    for (const auto& name : meta.smoothArrayNames) {
        appendLog("  平滑层数组: " + QString::fromStdString(name));
    }
    for (const auto& name : meta.detailArrayNames) {
        appendLog("  细节层数组: " + QString::fromStdString(name));
    }

    refreshFieldList();
    refreshSummary();
    refreshResultLog();

    const int idx = m_arrayBox->findText(QString::fromStdString(meta.fusedArrayName));
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }
}

void MainWindow::handleDatasetChanged()
{
    refreshFieldList();
    refreshSummary();
    refreshResultLog();
    updateActionState();
}

void MainWindow::handleAssociationChanged()
{
    refreshFieldList();
    updateActionState();
}

void MainWindow::handleArrayChanged()
{
    updateActionState();
}

void MainWindow::refreshFieldList()
{
    const QString previous = m_arrayBox->currentText();

    m_arrayBox->blockSignals(true);
    m_arrayBox->clear();

    const QString dsId = selectedDatasetId();
    if (!dsId.isEmpty()) {
        std::vector<CAEFieldInfo> fields;
        if (m_facade.listFields(toStdString(dsId), currentAssociation(), fields)) {
            for (const auto& field : fields) {
                m_arrayBox->addItem(QString::fromStdString(field.name));
            }
        }
    }

    int idx = previous.isEmpty() ? -1 : m_arrayBox->findText(previous);
    if (idx < 0 && m_arrayBox->count() > 0) {
        idx = 0;
    }
    if (idx >= 0) {
        m_arrayBox->setCurrentIndex(idx);
    }

    m_arrayBox->blockSignals(false);
    handleArrayChanged();
}

void MainWindow::refreshSummary()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        m_summaryLabel->setText("未选择数据集。");
        return;
    }

    CAEDatasetSummary summary;
    if (!m_facade.getDatasetSummary(toStdString(dsId), summary)) {
        m_summaryLabel->setText("数据集概览读取失败。");
        return;
    }

    int pointFieldCount = 0;
    int cellFieldCount = 0;
    for (const auto& field : summary.fields) {
        if (field.association == CAEFieldAssociation::Point) {
            ++pointFieldCount;
        } else {
            ++cellFieldCount;
        }
    }

    const QString gridLabel =
        summary.gridClass == CAEGridClass::Regular ? "规则" : "非结构";

    m_summaryLabel->setText(
        QString(
            "<div style='font-size:18px; font-weight:700; color:#0f172a;'>%1</div>"
            "<div style='margin-top:6px; color:#64748b;'>%2网格</div>"
            "<div style='margin-top:14px; line-height:1.8;'>"
            "<b>点数:</b> %3<br/>"
            "<b>单元数:</b> %4<br/>"
            "<b>点数组数:</b> %5<br/>"
            "<b>单元数组数:</b> %6<br/>"
            "<b>结果数组数:</b> %7"
            "</div>")
            .arg(QString::fromStdString(summary.displayName))
            .arg(gridLabel)
            .arg(static_cast<qulonglong>(summary.pointCount))
            .arg(static_cast<qulonglong>(summary.cellCount))
            .arg(pointFieldCount)
            .arg(cellFieldCount)
            .arg(static_cast<qulonglong>(summary.results.size())));
}

void MainWindow::refreshResultLog()
{
    const QString dsId = selectedDatasetId();
    if (dsId.isEmpty()) {
        return;
    }

    CAEDatasetSummary summary;
    if (!m_facade.getDatasetSummary(toStdString(dsId), summary)) {
        return;
    }

    appendLog(QString("当前数据集: %1 | 结果数组=%2")
                  .arg(QString::fromStdString(summary.displayName))
                  .arg(static_cast<qulonglong>(summary.results.size())));
}

void MainWindow::updateActionState()
{
    const bool hasDataset = !selectedDatasetId().isEmpty();
    const bool hasArray = hasDataset && !m_arrayBox->currentText().isEmpty();

    m_exportBtn->setEnabled(hasDataset);
    m_assocBox->setEnabled(hasDataset);
    m_arrayBox->setEnabled(hasDataset);
    m_computeBtn->setEnabled(m_initialized && hasArray);
    m_optimizeBtn->setEnabled(m_initialized && hasArray);
}
