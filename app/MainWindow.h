#pragma once

#include "CAEProcessingFacade.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QString;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void applyTheme();
    void bindSignals();
    void appendLog(const QString& text);

    QString selectedDatasetId() const;
    CAEFieldAssociation currentAssociation() const;
    CAEGradientMethod currentMethod() const;

    void refreshFieldList();
    void refreshSummary();
    void refreshResultLog();
    void updateActionState();

    void openFile();
    void exportCurrentDataset();
    void computeGradient();
    void computeMultiScaleOptimization();
    void handleDatasetChanged();
    void handleAssociationChanged();
    void handleArrayChanged();

    CAEProcessingFacade m_facade;
    bool m_initialized = false;

    QListWidget* m_datasetList = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QComboBox* m_assocBox = nullptr;
    QComboBox* m_arrayBox = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_computeBtn = nullptr;
    QPushButton* m_optimizeBtn = nullptr;
    QPlainTextEdit* m_log = nullptr;
};
