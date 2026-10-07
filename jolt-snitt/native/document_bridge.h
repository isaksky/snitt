#pragma once
#include "../../src/editorcanvas.h"
#include <QHash>
#include <QPainter>
#include <QSet>
#include <QVariantMap>
#include <limits>
#include <stdexcept>

// Session-owned storage for immutable images and prepared command samples.
// Jolt supplies every operation batch, undo position and retained handle set.
// No native undo history or command-order decisions live here.
class DocumentCanvas final : public EditorCanvas {
    Q_OBJECT
public:
    DocumentCanvas() {
        connect(this, &EditorCanvas::imageChanged, this, [this] {
            if (m_enabled && m_serial != documentProjection().resetSerial()) {
                m_serial = documentProjection().resetSerial();
                m_source = 0;
                emit documentSourceChanged();
            }
        });
    }
    Q_INVOKABLE void enableDocument() {
        m_enabled = true;
        useExternalDocument();
    }
    Q_INVOKABLE QVariantMap documentSource() {
        try {
            if (!m_enabled) return {};
            const QImage &image = documentProjection().source();
            if (!m_source && !image.isNull()) {
                const int id = next();
                m_images.insert(id, image);
                m_source = id;
            }
            return {{"generation", QVariant::fromValue(m_serial)}, {"image", m_source},
                    {"bytes", image.sizeInBytes()}};
        } catch (...) { return {}; }
    }
    Q_INVOKABLE QVariantMap prepareDocument(int frame, const QVariantMap &command) {
        int imageId = 0, operationId = 0;
        try {
            if (!m_enabled || !m_images.contains(frame)) return {};
            if (m_failNext) { m_failNext = false; return {}; }
            ImageRenderer render;
            render.reset(m_images.value(frame));
            const auto point = [&](const char *key) {
                const auto values = command.value(QLatin1String(key)).toList();
                if (values.size() != 2) return QPointF(qQNaN(), qQNaN());
                return QPointF(values[0].toDouble(), values[1].toDouble());
            };
            const QString kind = command.value("kind").toString();
            const QPointF start = point("start"), end = point("end");
            const QColor color = command.value("color").value<QColor>();
            bool ok = false;
            if (kind == "cut")
                ok = render.cut(command.value("vertical?").toBool(),
                                command.value("from").toInt(), command.value("to").toInt());
            else if (kind == "blur")
                ok = render.blur(QRectF(start, end), command.value("block-size", 12).toInt());
            else if (kind == "erase")
                ok = render.erase(QRectF(start, end), start);
            else if (kind == "annotation")
                ok = render.annotate(command.value("tool").toString(), start, end, color,
                                     command.value("stroke-width", 4).toDouble(),
                                     command.value("good-mode").toBool());
            else if (kind == "text")
                ok = render.text(QRectF(start, end), command.value("text").toString(), color,
                                 command.value("font-size", 24).toInt());
            if (!ok) return {};
            imageId = next(); operationId = next();
            m_images.insert(imageId, render.image());
            m_operations.insert(operationId, render.operations().last());
            return {{"image", imageId}, {"operation", operationId},
                    {"bytes", render.image().sizeInBytes()}};
        } catch (...) {
            m_images.remove(imageId); m_operations.remove(operationId);
            return {};
        }
    }
    Q_INVOKABLE bool projectDocumentBatch(int source, int frame, const QVariantList &operations,
                                          bool undo, bool redo) {
        try {
            if (!m_enabled || source != m_source || !m_images.contains(source)
                || !m_images.contains(frame)) return false;
            if (m_failProjection) { m_failProjection = false; return false; }
            QVector<ImageRenderer::Operation> batch;
            batch.reserve(operations.size());
            for (const auto &value : operations) {
                const auto entry = m_operations.constFind(value.toInt());
                if (entry == m_operations.cend()) return false;
                batch.append(*entry);
            }
            ImageRenderer projection;
            projection.project(m_images.value(source), m_images.value(frame), std::move(batch));
            projectDocument(projection, undo, redo);
            return true;
        } catch (...) { return false; }
    }
    Q_INVOKABLE void retainDocument(const QVariantList &handles) {
        QSet<int> retained;
        for (const auto &value : handles) retained.insert(value.toInt());
        // The current source remains valid during queued reset notifications.
        retained.insert(m_source);
        for (auto i = m_images.begin(); i != m_images.end();)
            if (!retained.contains(i.key())) i = m_images.erase(i); else ++i;
        for (auto i = m_operations.begin(); i != m_operations.end();)
            if (!retained.contains(i.key())) i = m_operations.erase(i); else ++i;
    }
    Q_INVOKABLE QVariantMap documentResources() const {
        qint64 bytes = 0, samples = 0;
        for (const auto &image : m_images) bytes += image.sizeInBytes();
        for (const auto &op : m_operations) samples += op.pixelGrid.sizeInBytes();
        return {{"images", m_images.size()}, {"operations", m_operations.size()},
                {"bytes", bytes}, {"sampleBytes", samples}};
    }
    Q_INVOKABLE bool saveDocumentRender(const QString &path, double scale, bool preview) {
        if (qEnvironmentVariable("SNITT_JOLT_DOCUMENT_TEST") != "1") return false;
        const auto &doc = documentProjection();
        if (!preview) return doc.render(scale).save(path);
        QImage image(qRound(doc.image().width() * scale), qRound(doc.image().height() * scale),
                     QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return false;
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.scale(scale, scale);
        doc.paint(painter, scale);
        painter.end();
        return image.save(path);
    }
    Q_INVOKABLE void failNextDocumentProjection() {
        if (qEnvironmentVariable("SNITT_JOLT_DOCUMENT_TEST") == "1") m_failProjection = true;
    }
    Q_INVOKABLE void failNextDocumentEdit() {
        if (qEnvironmentVariable("SNITT_JOLT_DOCUMENT_TEST") == "1") m_failNext = true;
    }
signals:
    void documentSourceChanged();
private:
    int next() {
        if (m_next == std::numeric_limits<int>::max()) throw std::overflow_error("image handles exhausted");
        return ++m_next;
    }
    bool m_enabled = false, m_failNext = false, m_failProjection = false;
    int m_next = 0, m_source = 0;
    quint64 m_serial = 0;
    QHash<int, QImage> m_images;
    QHash<int, ImageRenderer::Operation> m_operations;
};
