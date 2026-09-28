#include "regionselector.h"
#include "appsettings.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QTextLayout>
#include <QToolButton>
#include <QSvgRenderer>
#include <QKeySequence>
#include <cmath>

namespace {
class ModeButton final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRectF contents = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        option.text.clear();
        option.icon = QIcon();
        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);

        const QString label = QString(text()).remove('&');
        QTextLayout layout(label, font());
        if (text().startsWith('&')) {
            QTextCharFormat shortcut;
            shortcut.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            layout.setFormats({{0, 1, shortcut}});
        }
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(contents.width());
        layout.endLayout();
        const qreal iconWidth = icon().isNull() ? 0 : iconSize().width() + 4;
        const qreal x = contents.center().x() - (iconWidth + line.naturalTextWidth()) / 2;
        if (!icon().isNull())
            icon().paint(&painter, QRect(qRound(x), qRound(contents.center().y() - iconSize().height() / 2.0),
                                        iconSize().width(), iconSize().height()), Qt::AlignCenter,
                         isEnabled() ? QIcon::Normal : QIcon::Disabled, isChecked() ? QIcon::On : QIcon::Off);
        painter.setPen(!isEnabled() ? QColor("#727a86") : isChecked() ? QColor("#15283d") : QColor("#e9edf3"));
        // Draw the first-letter underline explicitly: macOS hides native
        // button mnemonics, even when SH_UnderlineShortcut is enabled.
        layout.draw(&painter, QPointF(x + iconWidth, contents.center().y() - line.height() / 2));
    }
};

QString actionHint(const QString &action, const QString &hint) {
    return hint.isEmpty() ? action : QStringLiteral("%1 (%2)").arg(action, hint);
}

QIcon toolbarIcon(const QString &name) {
    QSvgRenderer svg(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    QIcon icon;
    for (const auto &state : {QPair<QColor, QIcon::State>{QColor("#e9edf3"), QIcon::Off},
                              {QColor("#15283d"), QIcon::On}}) {
        QPixmap pixmap(96, 96);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        svg.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), state.first);
        painter.end();
        pixmap.setDevicePixelRatio(4);
        icon.addPixmap(pixmap, QIcon::Normal, state.second);
    }
    return icon;
}
}

RegionSelector::RegionSelector(QImage image, const QRect &geometry, AppSettings *settings)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_image(std::move(image)), m_settings(settings) {
    setObjectName("regionSelector");
    setWindowTitle("xshot — Select a region");
    // paintEvent covers the entire window with the frozen desktop.
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_image.setDevicePixelRatio(1);
    setGeometry(geometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("captureToolbar");
    m_toolbar->setAttribute(Qt::WA_StyledBackground);
    m_toolbar->setAttribute(Qt::WA_NoMousePropagation);
    m_toolbar->setCursor(Qt::ArrowCursor);
    m_toolbar->setStyleSheet(
        "QWidget#captureToolbar { background: #1b1e23; border: 1px solid #414751; border-radius: 12px; }"
        "QLabel { color: #b9c1cd; background: transparent; }"
        "QPushButton { color: #e9edf3; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 6px 10px; }"
        "QPushButton:hover { background: #323842; }"
        "QPushButton:checked { color: #15283d; background: #91bff0; }"
        "QPushButton#arrangeCaptureButton { color: #142820; background: #a3e6ca; }"
        "QPushButton:disabled { color: #727a86; background: transparent; }"
        "QPushButton#arrangeCaptureButton:disabled { background: #2b3238; }");
    auto *layout = new QVBoxLayout(m_toolbar);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto *modes = new QHBoxLayout;
    modes->setSpacing(4);
    const auto button = [this](const QString &text, const QString &name, bool mode = false) {
        QPushButton *control = mode ? new ModeButton(text, m_toolbar) : new QPushButton(text, m_toolbar);
        control->setObjectName(name);
        control->setFocusPolicy(Qt::NoFocus);
        control->setCursor(Qt::PointingHandCursor);
        control->setFixedHeight(36);
        return control;
    };
    m_singleButton = button("Region", "singleCaptureButton", true);
    m_multipleButton = button(QString(), "multipleCaptureButton", true);
    m_videoButton = button(QString(), "videoCaptureButton", true);
    m_singleButton->setIcon(toolbarIcon("square-dashed"));
    m_multipleButton->setIcon(toolbarIcon("copy"));
    m_videoButton->setIcon(toolbarIcon("video"));
    m_singleButton->setToolTip("Select a region");
    m_multipleButton->setToolTip(QString());
    m_videoButton->setToolTip(QString());
    for (auto *control : {m_singleButton, m_multipleButton, m_videoButton}) {
        control->setCheckable(true);
        control->setIconSize(QSize(22, 22));
        modes->addWidget(control);
    }
    modes->addStretch();
    m_cancelButton = button("Cancel (Esc)", "cancelCaptureButton");
    m_cancelButton->setAccessibleName("Cancel selection (Esc)");
    m_cancelButton->setToolTip("Cancel selection (Esc)");
    modes->addWidget(m_cancelButton);
    layout->addLayout(modes);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_instruction = new QLabel(m_toolbar);
    m_instruction->setObjectName("captureInstruction");
    m_instruction->setMinimumWidth(0);
    actions->addWidget(m_instruction, 1);
    m_count = new QLabel(m_toolbar);
    m_count->setObjectName("captureCount");
    m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    actions->addWidget(m_count);
    m_arrangeButton = button("Arrange (Enter)", "arrangeCaptureButton");
    m_arrangeButton->setToolTip("Continue to Arrange (Enter)");
    auto policy = m_arrangeButton->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    m_arrangeButton->setSizePolicy(policy);
    actions->addWidget(m_arrangeButton);
    layout->addLayout(actions);
    m_noticeLabel = new QLabel(this);
    m_noticeLabel->setObjectName("captureNotice");
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->setAlignment(Qt::AlignCenter);
    m_noticeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_noticeLabel->setStyleSheet("color: #f1d4a2; background: #1b1e23; border-radius: 6px; padding: 10px;");
    connect(m_cancelButton, &QPushButton::clicked, this, &RegionSelector::canceled);
    connect(m_arrangeButton, &QPushButton::clicked, this, &RegionSelector::accepted);
    connect(m_singleButton, &QPushButton::clicked, this, [this] {
        if (m_multiple || m_video) { m_dragging = false; emit singleRequested(); }
        updateToolbar();
    });
    connect(m_multipleButton, &QPushButton::clicked, this, [this] {
        if (!m_multiple && !m_video) { m_dragging = false; emit multipleRequested(); }
        updateToolbar();
    });
    connect(m_videoButton, &QPushButton::clicked, this, [this] {
        if (!m_video) { m_dragging = false; emit videoRequested(); }
        updateToolbar();
    });
    updateToolbar();
    layoutControls();
    if (m_settings) connect(m_settings, &AppSettings::settingsChanged, this, &RegionSelector::updateToolbar);
}

void RegionSelector::updateToolbar() {
    m_singleButton->setChecked(!m_multiple && !m_video);
    m_multipleButton->setChecked(m_multiple && !m_video);
    m_multipleButton->setEnabled(!m_video);
    m_videoButton->setChecked(m_video);
    m_instruction->setText(m_video ? "Drag to record." : m_multiple ? "Drag to add regions." : "Drag to capture.");
    const bool hasSelections = m_multiple && !m_video && m_total > 0;
    m_count->setText(hasSelections ? QStringLiteral("%1 %2").arg(m_total).arg(m_total == 1 ? "region" : "regions") : QString());
    m_arrangeButton->setVisible(hasSelections);
    m_arrangeButton->setEnabled(hasSelections);
    m_noticeLabel->setText(m_notice);
    m_noticeLabel->setVisible(!m_notice.isEmpty());
    layoutControls();
}

void RegionSelector::layoutControls() {
    if (!m_toolbar) return;
    const bool compact = width() < 560;
    QFont font = m_toolbar->font();
    font.setPixelSize(compact ? 12 : 14);
    m_toolbar->setFont(font);
    // Stylesheet-backed controls can keep a resolved font of their own.
    for (auto *control : m_toolbar->findChildren<QWidget *>()) control->setFont(font);
    m_count->setFixedWidth(compact ? 64 : 86);
    m_arrangeButton->setFixedWidth(compact ? 132 : 164);
    const auto setHint = [this, compact](QPushButton *control, const QString &action,
                                       const QString &description, const QString &name,
                                       const QString &fallback) {
        const QString hint = shortcutHint(name, fallback);
        const QString firstHint = hint.section(QStringLiteral(" / "), 0, 0);
        const bool mode = control == m_singleButton || control == m_multipleButton || control == m_videoButton;
        if (mode) {
            const QStringList bindings = m_settings ? m_settings->shortcuts().value(name).toStringList()
                                                   : QStringList{fallback};
            control->setText(bindings.contains(action.left(1).toUpper()) ? QLatin1Char('&') + action : action);
            // The selector handles bare-letter and configured shortcuts. Keep
            // the visible mnemonic without adding an implicit Alt shortcut.
            control->setShortcut(QKeySequence());
        } else {
            // Keep alternatives discoverable without making the compact toolbar wider.
            control->setText(compact && control == m_cancelButton && !firstHint.isEmpty()
                ? firstHint : actionHint(action, firstHint));
        }
        control->setToolTip(actionHint(description, hint));
        control->setAccessibleName(control->toolTip());
    };
    setHint(m_singleButton, QStringLiteral("Region"), QStringLiteral("Select a region"),
            QStringLiteral("regionSingle"), QStringLiteral("R"));
    setHint(m_multipleButton, QStringLiteral("Multi"), QStringLiteral("Select multiple regions"),
            QStringLiteral("regionMultiple"), QStringLiteral("M"));
    setHint(m_videoButton, QStringLiteral("Video"), QStringLiteral("Record a region"),
            QStringLiteral("regionVideo"), QStringLiteral("V"));
    setHint(m_cancelButton, QStringLiteral("Cancel"), QStringLiteral("Cancel selection"),
            QStringLiteral("regionCancel"), QStringLiteral("Escape"));
    setHint(m_arrangeButton, QStringLiteral("Arrange"), QStringLiteral("Continue to Arrange"),
            QStringLiteral("regionArrange"), QStringLiteral("Enter"));
    if (m_arrangeButton->fontMetrics().horizontalAdvance(m_arrangeButton->text()) + 22 > m_arrangeButton->width())
        m_arrangeButton->setText(QStringLiteral("Arrange"));
    const int panelWidth = qMin(760, qMax(1, width() - 24));
    const QList<QPair<QPushButton *, QString>> modeLabels = {
        {m_cancelButton, QStringLiteral("Cancel")}};
    // Long custom bindings may still need to live only in the tooltip. Drop the
    // widest hint first while preserving each action's label and the panel size.
    for (int attempt = 0; attempt < modeLabels.size(); ++attempt) {
        int requiredWidth = 24 + 3 * 4;
        for (auto *control : {m_singleButton, m_multipleButton, m_videoButton, m_cancelButton})
            requiredWidth += control->sizeHint().width();
        if (requiredWidth <= panelWidth) break;
        QPushButton *widest = nullptr;
        QString label;
        int mostSaved = 0;
        for (const auto &mode : modeLabels) {
            const auto metrics = mode.first->fontMetrics();
            const int saved = metrics.horizontalAdvance(mode.first->text()) - metrics.horizontalAdvance(mode.second);
            if (saved > mostSaved) { widest = mode.first; label = mode.second; mostSaved = saved; }
        }
        if (!widest) break;
        widest->setText(label);
    }
    m_toolbar->setGeometry((width() - panelWidth) / 2, 24, panelWidth, 104);
    m_noticeLabel->setGeometry(m_toolbar->x(), m_toolbar->geometry().bottom() + 8,
                               panelWidth, m_noticeLabel->heightForWidth(panelWidth));
    for (int i = 0; i < m_removeButtons.size(); ++i) {
        const QRectF area = m_areas[i].second;
        // Keep a full-sized target even for tiny selections, away from the number badge.
        const qreal x = area.width() >= 60 ? area.right() - 28 : area.right() + 4;
        m_removeButtons[i]->setGeometry(qBound(0, qRound(x), qMax(0, width() - 28)),
                                        qBound(0, qRound(area.top()), qMax(0, height() - 28)), 28, 28);
    }
    m_toolbar->raise();
    m_noticeLabel->raise();
}

QString RegionSelector::shortcutHint(const QString &name, const QString &fallback) const {
    if (!m_settings) return fallback;
    return m_settings->shortcutHints().value(name).toString();
}

bool RegionSelector::matchesShortcut(const QKeyEvent *event, const QString &name) const {
    if (!m_settings) return false;
    const QStringList bindings = m_settings->shortcuts().value(name).toStringList();
    const Qt::KeyboardModifiers modifiers = event->modifiers()
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    const QKeySequence pressed(QKeyCombination(modifiers, static_cast<Qt::Key>(event->key())));
    for (const QString &binding : bindings)
        if (QKeySequence::fromString(binding, QKeySequence::PortableText) == pressed) return true;
    return false;
}

void RegionSelector::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutControls();
}

QRect RegionSelector::pixelRect(const QRectF &selection, const QSizeF &viewSize, const QSize &imageSize) {
    if (viewSize.isEmpty() || imageSize.isEmpty()) return {};
    const QRectF bounded = selection.normalized().intersected(QRectF(QPointF(0, 0), viewSize));
    const qreal sx = imageSize.width() / viewSize.width();
    const qreal sy = imageSize.height() / viewSize.height();
    const int x = qRound(bounded.left() * sx), y = qRound(bounded.top() * sy);
    return QRect(x, y, qRound(bounded.right() * sx) - x, qRound(bounded.bottom() * sy) - y)
        .intersected(QRect(QPoint(0, 0), imageSize));
}

QRectF RegionSelector::selection() const {
    return QRectF(m_start, m_end).normalized().intersected(rect());
}

QImage RegionSelector::crop(const QRectF &area) const {
    return m_image.copy(pixelRect(area, size(), m_image.size()));
}

void RegionSelector::setSelections(bool multiple, const QList<QPair<int, QRectF>> &areas, int total) {
    m_multiple = multiple;
    m_areas = areas;
    m_total = total;
    m_notice.clear();
    // Reuse controls: a removal can synchronously update selections from its own clicked signal.
    while (m_removeButtons.size() > areas.size()) {
        auto *remove = m_removeButtons.takeLast();
        remove->hide();
        remove->deleteLater();
    }
    while (m_removeButtons.size() < areas.size()) {
        auto *remove = new QToolButton(this);
        remove->setIcon(toolbarIcon("trash"));
        remove->setIconSize(QSize(20, 20));
        remove->setFocusPolicy(Qt::NoFocus);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setStyleSheet("QToolButton { color: white; background: #1b1e23; border: 1px solid #91bff0; border-radius: 5px; font-size: 20px; } QToolButton:hover { background: #394149; }");
        connect(remove, &QToolButton::clicked, this, [this, remove] { emit regionRemoved(remove->property("regionIndex").toInt()); });
        m_removeButtons.append(remove);
    }
    for (int i = 0; i < areas.size(); ++i) {
        auto *remove = m_removeButtons[i];
        remove->setProperty("regionIndex", areas[i].first);
        remove->setAccessibleName(QStringLiteral("Remove region %1").arg(areas[i].first + 1));
        remove->setToolTip(remove->accessibleName());
        remove->setVisible(m_multiple);
    }
    updateToolbar();
    update();
}

void RegionSelector::setNotice(const QString &notice) { m_notice = notice; updateToolbar(); }

void RegionSelector::setVideo(bool video) {
    m_video = video;
    setWindowTitle(video ? "xshot — Select a recording region" : "xshot — Select a region");
    updateToolbar();
    update();
}

void RegionSelector::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.drawImage(rect(), m_image);
    p.fillRect(rect(), QColor(0, 0, 0, 115));
    for (const auto &entry : m_areas) {
        p.save();
        p.setClipRect(entry.second);
        p.drawImage(rect(), m_image);
        p.restore();
        p.setPen(QPen(QColor("#91bff0"), 2));
        p.drawRect(entry.second);
        const QRectF badge(entry.second.topLeft(), QSizeF(28, 28));
        p.fillRect(badge, QColor("#91bff0"));
        p.setPen(QColor("#15283d"));
        QFont numberFont = p.font(); numberFont.setPixelSize(16); numberFont.setBold(true); p.setFont(numberFont);
        p.drawText(badge, Qt::AlignCenter, QString::number(entry.first + 1));
    }
    const QRectF area = selection();
    if (m_dragging && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.drawImage(rect(), m_image);
        p.restore();
        p.setPen(QPen(QColor("#91bff0"), 2));
        p.drawRect(area);
    }
    if (m_dragging) {
        const QRect pixels = pixelRect(area, size(), m_image.size());
        const QString dimensions = QStringLiteral("%1 × %2 px").arg(pixels.width()).arg(pixels.height());
        QFont font = p.font(); font.setPixelSize(14); font.setBold(false); p.setFont(font);
        const QSize labelSize(p.fontMetrics().horizontalAdvance(dimensions) + 20, 30);
        const int x = qBound(0, qRound(area.right()) - labelSize.width(), qMax(0, width() - labelSize.width()));
        int y = qRound(area.bottom()) + 8;
        if (y + labelSize.height() > height()) y = qMax(0, qRound(area.bottom()) - labelSize.height() - 8);
        const QRect label(QPoint(x, y), labelSize);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1b1e23"));
        p.drawRoundedRect(label, 5, 5);
        p.setPen(Qt::white);
        p.drawText(label, Qt::AlignCenter, dimensions);
    }
}

void RegionSelector::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) { emit canceled(); return; }
    if (event->button() != Qt::LeftButton) return;
    m_start = m_end = event->position();
    m_dragging = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) return;
    m_end = event->position();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    m_end = event->position();
    m_dragging = false;
    if (m_multiple && (m_end - m_start).manhattanLength() < 4) {
        for (auto it = m_areas.crbegin(); it != m_areas.crend(); ++it) {
            if (it->second.contains(m_end)) { emit regionRemoved(it->first); return; }
        }
    }
    const QRect region = pixelRect(selection(), size(), m_image.size());
    if (region.width() >= 2 && region.height() >= 2) {
        if (m_video) emit videoSelected(selection());
        else if (m_multiple) emit regionAdded(selection());
        else emit selected(m_image.copy(region));
    }
    else update();
}

void RegionSelector::keyPressEvent(QKeyEvent *event) {
    if (matchesShortcut(event, QStringLiteral("regionCancel"))) emit canceled();
    else if (matchesShortcut(event, QStringLiteral("regionSingle"))) { m_singleButton->click(); update(); }
    else if (matchesShortcut(event, QStringLiteral("regionVideo"))) { m_videoButton->click(); update(); }
    else if (matchesShortcut(event, QStringLiteral("regionMultiple"))) { m_multipleButton->click(); update(); }
    else if (m_multiple && m_total > 0 && !m_dragging && matchesShortcut(event, QStringLiteral("regionArrange"))) emit accepted();
    else if (m_multiple && matchesShortcut(event, QStringLiteral("regionRemoveLast"))) emit removeLastRequested();
    else QWidget::keyPressEvent(event);
}
