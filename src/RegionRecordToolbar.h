#pragma once
#include <QWidget>
#include <QRect>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>

// Small toolbar shown above the selected recording region
// Lets user Start/Cancel recording without going back to main window
class RegionRecordToolbar : public QWidget {
    Q_OBJECT
public:
    explicit RegionRecordToolbar(const QRect &region, QWidget *parent = nullptr)
        : QWidget(parent), m_region(region)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);

        QHBoxLayout *lay = new QHBoxLayout(this);
        lay->setContentsMargins(10,6,10,6);
        lay->setSpacing(8);
        setFixedHeight(48);

        QLabel *icon = new QLabel("🎬");
        icon->setStyleSheet("font-size:18px;background:transparent;");
        lay->addWidget(icon);

        m_regionLabel = new QLabel(
            QString("%1 × %2").arg(region.width()).arg(region.height()));
        m_regionLabel->setStyleSheet(
            "color:#00aeff;font-weight:bold;font-size:12px;background:transparent;");
        lay->addWidget(m_regionLabel);

        m_recBtn = new QPushButton("⏺  Start Recording");
        m_recBtn->setFixedHeight(34);
        m_recBtn->setCursor(Qt::PointingHandCursor);
        m_recBtn->setStyleSheet(R"(
            QPushButton{background:#cc2222;color:white;font-weight:bold;
                        border:none;border-radius:7px;padding:0 14px;}
            QPushButton:hover{background:#ee2222;}
        )");

        QPushButton *cancelBtn = new QPushButton("✕");
        cancelBtn->setFixedSize(34,34);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(R"(
            QPushButton{background:rgba(100,40,40,0.8);color:#ee8888;
                        border:none;border-radius:7px;font-weight:bold;}
            QPushButton:hover{background:rgba(180,40,40,0.9);color:white;}
        )");

        lay->addWidget(m_recBtn);
        lay->addWidget(cancelBtn);

        connect(m_recBtn,   &QPushButton::clicked, this, &RegionRecordToolbar::recordRequested);
        connect(cancelBtn,  &QPushButton::clicked, this, &RegionRecordToolbar::cancelled);
        connect(cancelBtn,  &QPushButton::clicked, this, &QWidget::close);

        // Position just above the region
        QScreen *screen = QGuiApplication::primaryScreen();
        int w = 320;
        int x = region.left() + (region.width() - w) / 2;
        int y = region.top() - 56;
        if (y < 4) y = region.bottom() + 4;
        if (screen) {
            QRect ag = screen->availableGeometry();
            x = qBound(ag.left(), x, ag.right() - w);
        }
        setFixedWidth(w);
        move(x, y);
    }

    void setRecording(bool rec) {
        if (rec) {
            m_recBtn->setText("⏹  Stop Recording");
            m_recBtn->setStyleSheet(R"(
                QPushButton{background:#228822;color:white;font-weight:bold;
                            border:none;border-radius:7px;padding:0 14px;}
                QPushButton:hover{background:#33aa33;}
            )");
        } else {
            m_recBtn->setText("⏺  Start Recording");
            m_recBtn->setStyleSheet(R"(
                QPushButton{background:#cc2222;color:white;font-weight:bold;
                            border:none;border-radius:7px;padding:0 14px;}
                QPushButton:hover{background:#ee2222;}
            )");
        }
    }

signals:
    void recordRequested();
    void cancelled();

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(15,15,30,235));
        p.setPen(QPen(QColor(0,174,255,180),1.5));
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1),10,10);
    }

private:
    QRect     m_region;
    QPushButton *m_recBtn;
    QLabel    *m_regionLabel;
};
