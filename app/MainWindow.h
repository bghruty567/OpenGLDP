#pragma once

#include <QMainWindow>
#include "CAEProcessingFacade.h"
#include <vtkSmartPointer.h>

class QListWidget;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;

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
    void handleDatasetChanged();
    void handleAssociationChanged();
    void handleArrayChanged();

private:
    CAEProcessingFacade m_facade;
    bool m_initialized = false;

    QListWidget* m_datasetList = nullptr;
    QLabel* m_summaryLabel = nullptr;

    QComboBox* m_assocBox = nullptr;
    QComboBox* m_arrayBox = nullptr;
    QComboBox* m_methodBox = nullptr;
    QDoubleSpinBox* m_wExpSpin = nullptr;
    QDoubleSpinBox* m_lambdaSpin = nullptr;
    QSpinBox* m_componentSpin = nullptr;

    QPushButton* m_openBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_computeBtn = nullptr;

    QVTKOpenGLNativeWidget* m_vtkWidget = nullptr;
    QPlainTextEdit* m_log = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
};

