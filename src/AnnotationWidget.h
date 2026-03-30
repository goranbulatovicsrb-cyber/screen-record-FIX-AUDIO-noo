#pragma once
#include <QWidget>
#include <QPixmap>
#include <QColor>
#include <QFont>
#include <QStack>
#include <QList>
#include <QPainter>
#include <QPushButton>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QTimer>

enum class AnnotTool {
    Arrow, Rect, Ellipse, Line, Freehand, Text, Highlight, Blur, StepNum,
    Zoom, SelectBlur, Crop
};

struct Annot {
    AnnotTool     tool   = AnnotTool::Arrow;
    QPoint        p1, p2;
    QList<QPoint> pts;
    QColor        color  = Qt::red;
    int           width  = 3;
    QString       text;
    bool          filled = false;
    int           step   = 1;
    QFont         font;
};

class AnnotCanvas : public QWidget {
    Q_OBJECT
public:
    explicit AnnotCanvas(const QPixmap &px, QWidget *parent = nullptr);

    void    setTool(AnnotTool t)       { m_tool   = t; updateCursorForTool(); }
    void    setColor(const QColor &c)  { m_color  = c; }
    void    setWidth(int w)            { m_width  = w; }
    void    setFilled(bool f)          { m_filled = f; }
    AnnotTool currentTool() const      { return m_tool; }
    bool    isDrawing()    const       { return m_drawing; }
    void    undo();
    void    redo();
    void    clearAll();
    void    resetZoom()                { m_zoomScale = 1.0; m_viewOffset = QPoint(); update(); }
    int     stepCounter() const        { return m_step; }
    QPixmap flatten() const;

    // Exposed for dash animation
    int     m_dashOffset = 0;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *)       override;
    void mousePressEvent(QMouseEvent *)  override;
    void mouseMoveEvent(QMouseEvent *)   override;
    void mouseReleaseEvent(QMouseEvent *)override;
    void wheelEvent(QWheelEvent *)       override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void resizeEvent(QResizeEvent *)     override;

private:
    void drawAnnot(QPainter &p, const Annot &a, bool toImage = false) const;
    void drawArrow(QPainter &p, QPoint f, QPoint t, int w, QColor c) const;
    void updateCursorForTool();
    QRect  imageRect() const;
    QPoint toImg(QPoint wp) const;
    QPoint toWid(QPoint ip) const;

    QPixmap              m_orig;
    QList<Annot>         m_annots;
    QStack<QList<Annot>> m_undo, m_redo;
    Annot                m_cur;
    bool                 m_drawing      = false;
    double               m_zoomScale    = 1.0;
    QPoint               m_viewOffset;
    QPoint               m_panStart;
    int                  m_dragTextIdx  = -1;
    AnnotTool            m_tool         = AnnotTool::Arrow;
    QColor               m_color        = QColor(255,60,60);
    int                  m_width        = 3;
    bool                 m_filled       = false;
    int                  m_step         = 1;
};

class AnnotationWidget : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationWidget(const QPixmap &pixmap, QWidget *parent = nullptr);

signals:
    void annotationComplete(const QPixmap &result);

private slots:
    void onPrint();
    void onExportPdf();

private:
    void buildUI();
    void buildToolbar(QWidget *tb);
    void buildPalette(QWidget *pb);
    void setActiveTool(AnnotTool t, QPushButton *btn);

    AnnotCanvas        *m_canvas;
    QTimer             *m_dashTimer;
    QColor              m_activeColor = QColor(255,60,60);
    QList<QPushButton*> m_toolBtns;
};
