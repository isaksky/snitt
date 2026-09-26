#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QProcess>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

class StartupBench : public QWidget {
public:
    explicit StartupBench(bool tuned) : m_tuned(tuned) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setStyleSheet("background: #123456;");
        const QRect screen = QApplication::primaryScreen()->geometry();
        setGeometry(screen.x() + 140, screen.y() + 180, 360, 260);
        connect(&m_process, &QProcess::started, this, [this] { m_processMs = m_clock.elapsed(); });
        connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
            m_stderr += m_process.readAllStandardError();
            int newline;
            while ((newline = m_stderr.indexOf('\n')) >= 0) {
                const QByteArray line = m_stderr.left(newline).trimmed();
                m_stderr.remove(0, newline + 1);
                if (m_firstPacketMs < 0 && line.contains("demuxer ->") && line.contains("type:video"))
                    m_firstPacketMs = m_clock.elapsed();
                if (m_debugLines.size() < 16 && (line.contains("demuxer") || line.contains("gdigrab") || line.contains("avfoundation")))
                    m_debugLines.append(line);
            }
        });
        connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
            m_stdout += m_process.readAllStandardOutput();
            int newline;
            while ((newline = m_stdout.indexOf('\n')) >= 0) {
                const QByteArray line = m_stdout.left(newline).trimmed();
                m_stdout.remove(0, newline + 1);
                if (m_readyMs < 0 && line.startsWith("frame=") && line.mid(6).trimmed().toLongLong() > 0) {
                    m_readyMs = m_clock.elapsed();
                    QTimer::singleShot(1500, this, [this] { m_process.write("q\n"); });
                }
            }
        });
        connect(&m_process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
            qInfo().noquote() << QString("mode=%1 process_ms=%2 raw_packet_ms=%3 ready_ms=%4 exit=%5 size=%6")
                .arg(m_tuned ? "tuned" : "baseline").arg(m_processMs).arg(m_firstPacketMs).arg(m_readyMs)
                .arg(code).arg(QFileInfo(m_output).size());
            if (m_firstPacketMs < 0 || m_readyMs < 0 || code != 0 || status != QProcess::NormalExit) {
                for (const QByteArray &line : m_debugLines) qWarning().noquote() << line;
                qWarning().noquote() << m_stderr.right(1000);
            }
            QApplication::exit(m_firstPacketMs >= 0 && m_readyMs >= 0 && code == 0 ? 0 : 1);
        });
        QTimer::singleShot(15000, this, [this] {
            if (m_process.state() != QProcess::NotRunning) {
                qWarning("benchmark timed out");
                m_process.kill();
            }
        });
    }

    void trigger() {
        show(); raise(); activateWindow();
        QTimer::singleShot(400, this, [this] {
            QTest::mousePress(this, Qt::LeftButton, Qt::NoModifier, rect().center());
            QTest::mouseRelease(this, Qt::LeftButton, Qt::NoModifier, rect().center());
        });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        QWidget::mouseReleaseEvent(event);
        const QRect screen = QApplication::primaryScreen()->geometry();
        const QRect region = QRect(geometry().topLeft() + QPoint(20, 20), QSize(240, 160));
        m_output = m_temp.filePath("clip.mp4");
        QStringList args {"-hide_banner", "-loglevel", "info", "-debug_ts", "-n", "-nostats",
                          "-progress", "pipe:1", "-stats_period", m_tuned ? "0.05" : "0.2"};
#ifdef Q_OS_WIN
        args << "-f" << "gdigrab" << "-framerate" << "30" << "-draw_mouse" << "1"
             << "-offset_x" << QString::number(region.x()) << "-offset_y" << QString::number(region.y())
             << "-video_size" << "240x160";
        if (m_tuned) args << "-probesize" << "32" << "-analyzeduration" << "0";
        args << "-i" << "desktop";
        const QString filter = "pad=ceil(iw/2)*2:ceil(ih/2)*2";
#else
        args << "-f" << "avfoundation" << "-framerate" << "30" << "-capture_cursor" << "1"
             << "-pixel_format" << "bgr0";
        if (m_tuned) args << "-probesize" << "32" << "-analyzeduration" << "0";
        args << "-i" << "Capture screen 0:none";
        const auto fraction = [](qreal n) { return QString::number(n, 'f', 10); };
        const QRectF relative((region.x() - screen.x()) / qreal(screen.width()),
                              (region.y() - screen.y()) / qreal(screen.height()),
                              region.width() / qreal(screen.width()), region.height() / qreal(screen.height()));
        const QString filter = QString("crop=round(iw*%1):round(ih*%2):round(iw*%3):round(ih*%4):exact=1,pad=ceil(iw/2)*2:ceil(ih/2)*2")
            .arg(fraction(relative.width()), fraction(relative.height()), fraction(relative.x()), fraction(relative.y()));
#endif
        args << "-an" << "-vf" << filter << "-r" << "30" << "-c:v" << "libx264" << "-preset" << "veryfast"
             << "-crf" << "18" << "-pix_fmt" << "yuv420p"
             << "-movflags" << "+frag_keyframe+empty_moov+default_base_moof" << "-g" << "30" << m_output;
        m_clock.start();
        m_process.start("ffmpeg", args);
    }

private:
    bool m_tuned;
    QTemporaryDir m_temp;
    QProcess m_process;
    QElapsedTimer m_clock;
    QByteArray m_stderr, m_stdout;
    QList<QByteArray> m_debugLines;
    QString m_output;
    qint64 m_processMs = -1, m_firstPacketMs = -1, m_readyMs = -1;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 2 || !QStringList{"baseline", "tuned"}.contains(app.arguments().at(1)))
        qFatal("usage: recording_startup baseline|tuned");
    StartupBench bench(app.arguments().at(1) == "tuned");
    bench.trigger();
    return app.exec();
}
