#include <QApplication>
#include <QTextStream>
#include <QWidget>

// A separate process is essential on macOS: ScreenCaptureKit excludes all
// windows owned by the recording test process, including its own fixtures.
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 5) return 2;
    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    marker.setStyleSheet("background: #123456;");
    marker.setGeometry(app.arguments()[1].toInt(), app.arguments()[2].toInt(),
                       app.arguments()[3].toInt(), app.arguments()[4].toInt());
    marker.show();
    marker.raise();
    QTextStream(stdout) << "ready\n" << Qt::flush;
    return app.exec();
}
