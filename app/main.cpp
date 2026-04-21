#include "MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char* argv[])
{
    // 先设置 Qt/VTK 兼容的默认 OpenGL 格式。
    // 这样后续主窗口里的 QVTKOpenGLNativeWidget 才能稳定创建渲染上下文。
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);

    // 主窗口负责把“文件加载、参数输入、结果显示”整合在一起。
    MainWindow w;
    w.resize(1400, 900);
    w.show();

    // Qt 事件循环启动后，按钮点击、文件对话框、VTK 重绘等事件
    // 都通过这里统一调度。
    return app.exec();
}
