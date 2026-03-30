#include "AnnotationWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QLabel>
#include <QColorDialog>
#include <QInputDialog>
#include <QScrollArea>
#include <QFileDialog>
#include <QDir>
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QPainterPath>
#include <QtMath>
#include <QCursor>
#include <QMessageBox>
#include <QDesktopServices>
#include <QTemporaryFile>
#include <QUrl>
#include <QStyle>
#include <QShortcut>

// ─── AnnotCanvas ─────────────────────────────────────────────────────────────

AnnotCanvas::AnnotCanvas(const QPixmap &px, QWidget *parent)
    : QWidget(parent), m_orig(px)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRect AnnotCanvas::imageRect() const {
    if (m_orig.isNull()) return rect();
    QSize base = m_orig.size().scaled(size(), Qt::KeepAspectRatio);
    int sw = qMax(50, (int)(base.width()  * m_zoomScale));
    int sh = qMax(50, (int)(base.height() * m_zoomScale));
    int x  = (width()  - sw) / 2 + m_viewOffset.x();
    int y  = (height() - sh) / 2 + m_viewOffset.y();
    return QRect(x, y, sw, sh);
}

QPoint AnnotCanvas::toImg(QPoint wp) const {
    QRect ir = imageRect();
    if (ir.isEmpty()) return wp;
    double sx = (double)m_orig.width()  / ir.width();
    double sy = (double)m_orig.height() / ir.height();
    return QPoint((wp.x()-ir.left())*sx, (wp.y()-ir.top())*sy);
}

QPoint AnnotCanvas::toWid(QPoint ip) const {
    QRect ir = imageRect();
    if (ir.isEmpty()) return ip;
    double sx = (double)ir.width()  / m_orig.width();
    double sy = (double)ir.height() / m_orig.height();
    return QPoint(ir.left()+ip.x()*sx, ir.top()+ip.y()*sy);
}

void AnnotCanvas::resizeEvent(QResizeEvent *e) { QWidget::resizeEvent(e); update(); }

// ── Update cursor based on tool + hover ──────────────────────────────────────
void AnnotCanvas::updateCursorForTool() {
    if (m_tool == AnnotTool::Zoom)
        setCursor(m_zoomScale >= 4.0 ? Qt::ArrowCursor : Qt::SizeAllCursor);
    else if (m_tool == AnnotTool::Text && m_dragTextIdx >= 0)
        setCursor(Qt::SizeAllCursor);
    else if (m_tool == AnnotTool::Crop || m_tool == AnnotTool::SelectBlur)
        setCursor(Qt::CrossCursor);
    else if (m_tool == AnnotTool::Text)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(Qt::CrossCursor);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void AnnotCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    p.fillRect(rect(), QColor(0x0d, 0x0d, 0x1e));

    QRect ir = imageRect();
    if (!m_orig.isNull()) {
        p.drawPixmap(ir, m_orig);
        p.setPen(QPen(QColor(0,174,255,60),1));
        p.drawRect(ir);
    }

    // Draw committed annotations
    for (const auto &a : m_annots) drawAnnot(p, a);

    // Draw in-progress annotation
    if (m_drawing) {
        if (m_tool == AnnotTool::Crop) {
            // Crop: dimmed overlay + bright selection
            QRect sel = QRect(toWid(m_cur.p1), toWid(m_cur.p2)).normalized();
            // Dim everything outside selection
            p.setBrush(QColor(0,0,0,120));
            p.setPen(Qt::NoPen);
            p.drawRect(ir.left(), ir.top(), ir.width(), sel.top()-ir.top());
            p.drawRect(ir.left(), sel.bottom(), ir.width(), ir.bottom()-sel.bottom());
            p.drawRect(ir.left(), sel.top(), sel.left()-ir.left(), sel.height());
            p.drawRect(sel.right(), sel.top(), ir.right()-sel.right(), sel.height());
            // Bright border
            p.setPen(QPen(QColor(255,200,0), 2, Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);
            // Corner handles
            p.setBrush(QColor(255,200,0));
            int hs = 8;
            for (auto &c : {sel.topLeft(), sel.topRight(), sel.bottomLeft(), sel.bottomRight()})
                p.drawRect(c.x()-hs/2, c.y()-hs/2, hs, hs);
            // Size label
            QString sizeStr = QString("  %1 × %2 px  ")
                .arg((int)(sel.width()*(double)m_orig.width()/ir.width()))
                .arg((int)(sel.height()*(double)m_orig.height()/ir.height()));
            QFont sf; sf.setPixelSize(12); sf.setBold(true); p.setFont(sf);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255,200,0,220));
            QFontMetrics fm(sf);
            QRect tr = fm.boundingRect(sizeStr);
            tr.moveCenter(QPoint(sel.center().x(), sel.top()-12));
            p.drawRoundedRect(tr.adjusted(-4,-2,4,2), 3, 3);
            p.setPen(Qt::black);
            p.drawText(tr, Qt::AlignCenter, sizeStr);
        }
        else if (m_tool == AnnotTool::SelectBlur) {
            // SelectBlur: dashed border + frosted look
            QRect sel = QRect(toWid(m_cur.p1), toWid(m_cur.p2)).normalized();
            // Frost effect inside
            p.setBrush(QColor(180,220,255,60));
            p.setPen(Qt::NoPen);
            p.drawRect(sel);
            // Animated dashed border
            QPen bp(QColor(0,174,255), 2, Qt::DashLine);
            bp.setDashOffset(m_dashOffset);
            p.setPen(bp);
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);
            // BLUR label
            p.setBrush(QColor(0,174,255,200)); p.setPen(Qt::NoPen);
            QFont lf; lf.setPixelSize(11); lf.setBold(true); p.setFont(lf);
            QFontMetrics fm(lf);
            QString lbl = "  BLUR  ";
            QRect lr = fm.boundingRect(lbl);
            lr.moveCenter(QPoint(sel.center().x(), sel.center().y()));
            p.drawRoundedRect(lr.adjusted(-4,-2,4,2), 3, 3);
            p.setPen(Qt::white);
            p.drawText(lr, Qt::AlignCenter, lbl);
        }
        else {
            drawAnnot(p, m_cur);
        }
    }

    // Text drag indicator — move icon on hover
    if (m_tool == AnnotTool::Text) {
        for (int i = 0; i < m_annots.size(); ++i) {
            if (m_annots[i].tool != AnnotTool::Text) continue;
            QPoint wp = toWid(m_annots[i].p1);
            // Draw move handle
            p.setPen(QPen(QColor(0,174,255,160), 1, Qt::DashLine));
            p.setBrush(QColor(0,174,255,30));
            QFont af = m_annots[i].font;
            af.setPixelSize(qMax(12,(int)(18*(double)width()/qMax(1,m_orig.width()))));
            QFontMetrics fm2(af);
            QRect textBounds = fm2.boundingRect(m_annots[i].text);
            textBounds.moveTopLeft(wp);
            p.drawRect(textBounds.adjusted(-4,-4,4,4));
            // Move icon in top-right of text box
            p.setBrush(QColor(0,174,255,200)); p.setPen(Qt::NoPen);
            QRect iconR(textBounds.right()+2, textBounds.top()-2, 16, 16);
            p.drawRoundedRect(iconR, 3, 3);
            p.setPen(Qt::white);
            QFont ic; ic.setPixelSize(10); p.setFont(ic);
            p.drawText(iconR, Qt::AlignCenter, "✥");
        }
    }

    // Zoom indicator
    if (m_zoomScale != 1.0) {
        p.setBrush(QColor(0,0,0,160)); p.setPen(Qt::NoPen);
        p.drawRoundedRect(8,8,90,24,5,5);
        p.setPen(QColor(0,200,255));
        QFont zf; zf.setPixelSize(12); zf.setBold(true); p.setFont(zf);
        p.drawText(QRect(8,8,90,24), Qt::AlignCenter,
                   QString("🔍 %1%").arg((int)(m_zoomScale*100)));
    }

    // Bottom hint
    QString hint;
    if (m_tool == AnnotTool::Crop)
        hint = "Drag to select crop area  |  Release to apply  |  ESC = cancel";
    else if (m_tool == AnnotTool::SelectBlur)
        hint = "Drag to select blur area  |  Release to apply";
    else if (m_tool == AnnotTool::Text)
        hint = "Click text box to drag  |  Click empty area for new text  |  Right-click text to delete";
    else if (m_tool == AnnotTool::Zoom)
        hint = "Scroll or click to zoom  |  Right-click to zoom out  |  1:1 to reset";

    if (!hint.isEmpty()) {
        QFont hf; hf.setPixelSize(11); p.setFont(hf);
        p.setBrush(QColor(0,0,0,160)); p.setPen(Qt::NoPen);
        QFontMetrics hfm(hf);
        QRect hr = hfm.boundingRect(hint);
        hr.moveCenter(QPoint(width()/2, height()-16));
        hr.adjust(-10,0,10,0);
        p.drawRoundedRect(hr, 4,4);
        p.setPen(QColor(180,220,255));
        p.drawText(hr, Qt::AlignCenter, hint);
    }
}

// ── Draw single annotation ────────────────────────────────────────────────────
void AnnotCanvas::drawAnnot(QPainter &p, const Annot &a, bool) const {
    QPoint w1=toWid(a.p1), w2=toWid(a.p2);
    QRect wr=QRect(w1,w2).normalized();
    switch(a.tool) {
    case AnnotTool::Arrow: drawArrow(p,w1,w2,a.width,a.color); break;
    case AnnotTool::Rect:
        p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.setBrush(a.filled?QColor(a.color.red(),a.color.green(),a.color.blue(),70):QColor(Qt::transparent));
        p.drawRect(wr); break;
    case AnnotTool::Ellipse:
        p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.setBrush(a.filled?QColor(a.color.red(),a.color.green(),a.color.blue(),70):QColor(Qt::transparent));
        p.drawEllipse(wr); break;
    case AnnotTool::Line:
        p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(w1,w2); break;
    case AnnotTool::Freehand: {
        if(a.pts.size()<2) break;
        QPainterPath path; path.moveTo(toWid(a.pts[0]));
        for(int i=1;i<a.pts.size();++i) path.lineTo(toWid(a.pts[i]));
        p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.setBrush(Qt::NoBrush); p.drawPath(path); break; }
    case AnnotTool::Text:
        if(!a.text.isEmpty()) {
            QFont f=a.font;
            f.setPixelSize(qMax(12,(int)(18*(double)width()/qMax(1,m_orig.width()))));
            p.setFont(f);
            p.setPen(QColor(0,0,0,160)); p.drawText(w1+QPoint(2,2),a.text);
            p.setPen(a.color); p.drawText(w1,a.text);
        } break;
    case AnnotTool::Highlight:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(a.color.red(),a.color.green(),a.color.blue(),100));
        p.drawRect(wr); break;
    case AnnotTool::Blur: {
        // Show as frosted rect (actual blur applied on flatten)
        p.setPen(QPen(QColor(0,150,255,100),2,Qt::DashLine));
        p.setBrush(QColor(100,150,255,40));
        p.drawRect(wr);
        QFont bf; bf.setPixelSize(10); p.setFont(bf);
        p.setPen(QColor(100,200,255,160));
        p.drawText(wr, Qt::AlignCenter, "BLUR"); break; }
    case AnnotTool::SelectBlur: {
        // Same as Blur (already applied)
        p.setPen(QPen(QColor(0,150,255,60),1));
        p.setBrush(QColor(100,150,255,20));
        p.drawRect(wr); break; }
    case AnnotTool::StepNum: {
        int r=qMax(12,a.width*5);
        p.setBrush(a.color); p.setPen(Qt::NoPen);
        p.drawEllipse(w1,r,r);
        QFont f; f.setPixelSize(r); f.setBold(true); p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRect(w1.x()-r,w1.y()-r,r*2,r*2),Qt::AlignCenter,QString::number(a.step));
        break; }
    default: break;
    }
}

void AnnotCanvas::drawArrow(QPainter &p,QPoint from,QPoint to,int w,QColor c) const {
    if(from==to) return;
    p.setPen(QPen(c,w,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.drawLine(from,to);
    double angle=std::atan2(-(to.y()-from.y()),to.x()-from.x());
    double len=14+w*2.5, ang=M_PI/7.0;
    QPointF p1(to.x()-len*std::cos(angle-ang),to.y()+len*std::sin(angle-ang));
    QPointF p2(to.x()-len*std::cos(angle+ang),to.y()+len*std::sin(angle+ang));
    p.setBrush(c); p.setPen(Qt::NoPen);
    QPolygonF head; head<<QPointF(to)<<p1<<p2;
    p.drawPolygon(head);
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void AnnotCanvas::mousePressEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton) return;

    if(m_tool==AnnotTool::Zoom) {
        m_zoomScale=qBound(0.2,m_zoomScale*1.25,8.0); update(); return;
    }
    if(!imageRect().contains(e->pos())) return;

    // Text: check if clicking existing text to drag
    if(m_tool==AnnotTool::Text) {
        QPoint imgPt=toImg(e->pos());
        for(int i=m_annots.size()-1;i>=0;--i) {
            if(m_annots[i].tool!=AnnotTool::Text) continue;
            QPoint wp=toWid(m_annots[i].p1);
            QFont af=m_annots[i].font;
            af.setPixelSize(qMax(12,(int)(18*(double)width()/qMax(1,m_orig.width()))));
            QFontMetrics fm(af);
            QRect tb=fm.boundingRect(m_annots[i].text);
            tb.moveTopLeft(wp); tb.adjust(-8,-8,8,8);
            if(tb.contains(e->pos())) {
                m_dragTextIdx=i; m_panStart=e->pos();
                setCursor(Qt::SizeAllCursor); return;
            }
        }
        // No existing text — create new
        bool ok;
        QString txt=QInputDialog::getText(this,"Add Text","Enter text:",QLineEdit::Normal,"",&ok);
        if(!ok||txt.isEmpty()) return;
        m_undo.push(m_annots); m_redo.clear();
        Annot a; a.tool=AnnotTool::Text; a.text=txt;
        a.p1=toImg(e->pos()); a.p2=a.p1;
        a.color=m_color; a.width=m_width;
        QFont f; f.setPixelSize(22); f.setBold(true); a.font=f;
        m_annots.append(a); update(); emit changed(); return;
    }

    m_drawing=true;
    m_undo.push(m_annots); m_redo.clear();
    m_cur=Annot(); m_cur.tool=m_tool;
    m_cur.p1=toImg(e->pos()); m_cur.p2=m_cur.p1;
    m_cur.color=m_color; m_cur.width=m_width;
    m_cur.filled=m_filled; m_cur.step=m_step;
    QFont f; f.setPixelSize(22); f.setBold(true); m_cur.font=f;
    if(m_tool==AnnotTool::Freehand||m_tool==AnnotTool::Highlight)
        m_cur.pts.append(m_cur.p1);
}

void AnnotCanvas::mouseMoveEvent(QMouseEvent *e) {
    // Drag text
    if(m_dragTextIdx>=0&&m_dragTextIdx<m_annots.size()) {
        QPoint delta=e->pos()-m_panStart; m_panStart=e->pos();
        double sx=(double)m_orig.width()/qMax(1,imageRect().width());
        double sy=(double)m_orig.height()/qMax(1,imageRect().height());
        m_annots[m_dragTextIdx].p1+=QPoint(delta.x()*sx,delta.y()*sy);
        m_annots[m_dragTextIdx].p2=m_annots[m_dragTextIdx].p1;
        update(); emit changed(); return;
    }
    // Update cursor on text hover
    if(m_tool==AnnotTool::Text && !m_drawing) {
        bool onText=false;
        for(int i=m_annots.size()-1;i>=0;--i) {
            if(m_annots[i].tool!=AnnotTool::Text) continue;
            QPoint wp=toWid(m_annots[i].p1);
            QFont af=m_annots[i].font;
            af.setPixelSize(qMax(12,(int)(18*(double)width()/qMax(1,m_orig.width()))));
            QFontMetrics fm(af);
            QRect tb=fm.boundingRect(m_annots[i].text);
            tb.moveTopLeft(wp); tb.adjust(-8,-8,8,8);
            if(tb.contains(e->pos())) { onText=true; break; }
        }
        setCursor(onText?Qt::SizeAllCursor:Qt::IBeamCursor);
    }
    if(!m_drawing) return;
    m_cur.p2=toImg(e->pos());
    if(m_tool==AnnotTool::Freehand||m_tool==AnnotTool::Highlight)
        m_cur.pts.append(m_cur.p2);
    update();
}

void AnnotCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton) return;
    if(m_dragTextIdx>=0) { m_dragTextIdx=-1; setCursor(Qt::IBeamCursor); emit changed(); return; }
    if(!m_drawing) return;
    m_drawing=false;
    m_cur.p2=toImg(e->pos());

    // Crop
    if(m_tool==AnnotTool::Crop) {
        QRect r=QRect(m_cur.p1,m_cur.p2).normalized()
                 .intersected(QRect(0,0,m_orig.width(),m_orig.height()));
        if(r.width()>5&&r.height()>5) {
            m_undo.push(m_annots);
            m_orig=m_orig.copy(r);
            m_annots.clear(); m_redo.clear();
            update(); emit changed();
        }
        return;
    }

    // SelectBlur
    if(m_tool==AnnotTool::SelectBlur) {
        QRect r=QRect(m_cur.p1,m_cur.p2).normalized()
                 .intersected(QRect(0,0,m_orig.width(),m_orig.height()));
        if(r.width()>5&&r.height()>5) {
            m_undo.push(m_annots);
            QImage img=m_orig.toImage().convertToFormat(QImage::Format_RGB32);
            QImage region=img.copy(r);
            for(int pass=0;pass<12;++pass) {
                QImage tmp=region.copy();
                for(int y=1;y<region.height()-1;++y) {
                    QRgb *row=(QRgb*)region.scanLine(y);
                    const QRgb *prev=(const QRgb*)tmp.constScanLine(y-1);
                    const QRgb *next=(const QRgb*)tmp.constScanLine(y+1);
                    for(int x=1;x<region.width()-1;++x) {
                        row[x]=qRgb(
                            (qRed(prev[x])+qRed(row[x-1])+qRed(row[x])+qRed(row[x+1])+qRed(next[x]))/5,
                            (qGreen(prev[x])+qGreen(row[x-1])+qGreen(row[x])+qGreen(row[x+1])+qGreen(next[x]))/5,
                            (qBlue(prev[x])+qBlue(row[x-1])+qBlue(row[x])+qBlue(row[x+1])+qBlue(next[x]))/5);
                    }
                }
            }
            QPainter pp(&m_orig); pp.drawImage(r,region); pp.end();
            update(); emit changed();
        }
        return;
    }

    if(m_tool==AnnotTool::StepNum) m_step++;
    m_annots.append(m_cur);
    update(); emit changed();
}

void AnnotCanvas::wheelEvent(QWheelEvent *e) {
    if(m_tool==AnnotTool::Zoom||e->modifiers()&Qt::ControlModifier) {
        double d=e->angleDelta().y()>0?1.15:0.87;
        m_zoomScale=qBound(0.2,m_zoomScale*d,8.0); update();
    }
    QWidget::wheelEvent(e);
}

void AnnotCanvas::contextMenuEvent(QContextMenuEvent *e) {
    if(m_tool==AnnotTool::Zoom) {
        m_zoomScale=qBound(0.2,m_zoomScale/1.25,8.0); update(); return;
    }
    if(m_tool==AnnotTool::Text) {
        for(int i=m_annots.size()-1;i>=0;--i) {
            if(m_annots[i].tool!=AnnotTool::Text) continue;
            QPoint wp=toWid(m_annots[i].p1);
            QFont af=m_annots[i].font;
            af.setPixelSize(qMax(12,(int)(18*(double)width()/qMax(1,m_orig.width()))));
            QFontMetrics fm(af);
            QRect tb=fm.boundingRect(m_annots[i].text);
            tb.moveTopLeft(wp); tb.adjust(-8,-8,8,8);
            if(tb.contains(e->pos())) {
                m_undo.push(m_annots);
                m_annots.removeAt(i); update(); emit changed(); return;
            }
        }
    }
    QWidget::contextMenuEvent(e);
}

void AnnotCanvas::undo() {
    if(!m_undo.isEmpty()) { m_redo.push(m_annots); m_annots=m_undo.pop(); update(); }
}
void AnnotCanvas::redo() {
    if(!m_redo.isEmpty()) { m_undo.push(m_annots); m_annots=m_redo.pop(); update(); }
}
void AnnotCanvas::clearAll() { m_undo.push(m_annots); m_annots.clear(); m_step=1; update(); }

// ── Flatten to original resolution ───────────────────────────────────────────
QPixmap AnnotCanvas::flatten() const {
    QPixmap result(m_orig.size());
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0,0,m_orig);
    for(const auto &a:m_annots) {
        QPoint w1=a.p1,w2=a.p2;
        QRect wr=QRect(w1,w2).normalized();
        switch(a.tool) {
        case AnnotTool::Arrow: drawArrow(p,w1,w2,a.width,a.color); break;
        case AnnotTool::Rect:
            p.setPen(QPen(a.color,a.width));
            p.setBrush(a.filled?QColor(a.color.red(),a.color.green(),a.color.blue(),70):QColor(Qt::transparent));
            p.drawRect(wr); break;
        case AnnotTool::Ellipse:
            p.setPen(QPen(a.color,a.width));
            p.setBrush(a.filled?QColor(a.color.red(),a.color.green(),a.color.blue(),70):QColor(Qt::transparent));
            p.drawEllipse(wr); break;
        case AnnotTool::Line:
            p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(w1,w2); break;
        case AnnotTool::Freehand: {
            if(a.pts.size()<2) break;
            QPainterPath path; path.moveTo(a.pts[0]);
            for(int i=1;i<a.pts.size();++i) path.lineTo(a.pts[i]);
            p.setPen(QPen(a.color,a.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            p.setBrush(Qt::NoBrush); p.drawPath(path); break; }
        case AnnotTool::Text:
            p.setFont(a.font);
            p.setPen(QColor(0,0,0,160)); p.drawText(w1+QPoint(1,1),a.text);
            p.setPen(a.color); p.drawText(w1,a.text); break;
        case AnnotTool::Highlight:
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(a.color.red(),a.color.green(),a.color.blue(),100));
            p.drawRect(wr); break;
        case AnnotTool::Blur: {
            QRect br=wr.intersected(result.rect());
            if(!br.isEmpty()) {
                QImage img=result.toImage().convertToFormat(QImage::Format_RGB32);
                QImage region=img.copy(br);
                for(int pass=0;pass<10;++pass) {
                    QImage tmp=region.copy();
                    for(int y=1;y<region.height()-1;++y) {
                        QRgb *row=(QRgb*)region.scanLine(y);
                        const QRgb *prev=(const QRgb*)tmp.constScanLine(y-1);
                        const QRgb *next=(const QRgb*)tmp.constScanLine(y+1);
                        for(int x=1;x<region.width()-1;++x)
                            row[x]=qRgb(
                                (qRed(prev[x])+qRed(row[x-1])+qRed(row[x])+qRed(row[x+1])+qRed(next[x]))/5,
                                (qGreen(prev[x])+qGreen(row[x-1])+qGreen(row[x])+qGreen(row[x+1])+qGreen(next[x]))/5,
                                (qBlue(prev[x])+qBlue(row[x-1])+qBlue(row[x])+qBlue(row[x+1])+qBlue(next[x]))/5);
                    }
                }
                p.drawImage(br,region);
            }
            break; }
        case AnnotTool::StepNum: {
            int r=18; p.setBrush(a.color); p.setPen(Qt::NoPen);
            p.drawEllipse(w1,r,r);
            QFont f; f.setPixelSize(14); f.setBold(true); p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRect(w1.x()-r,w1.y()-r,r*2,r*2),Qt::AlignCenter,QString::number(a.step));
            break; }
        default: break;
        }
    }
    return result;
}

// ─── AnnotationWidget ─────────────────────────────────────────────────────────

AnnotationWidget::AnnotationWidget(const QPixmap &pixmap, QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("ScreenMaster Pro — Annotate");
    setMinimumSize(1000,700);
    setAttribute(Qt::WA_DeleteOnClose,false);
    setStyleSheet(R"(
QWidget{background:#0d0d1e;color:#e0e0f0;font-family:"Segoe UI",Arial,sans-serif;font-size:13px;}
QPushButton{background:#1e1e3a;border:1px solid #303055;border-radius:6px;padding:5px 12px;color:#ccccee;}
QPushButton:hover{background:#2a2a50;border-color:#00aeff;color:#fff;}
QPushButton:checked,QPushButton#active{background:#00aeff;color:#fff;font-weight:bold;border:none;}
QPushButton#done{background:#0099dd;color:white;font-weight:bold;border:none;border-radius:6px;padding:7px 18px;}
QPushButton#done:hover{background:#00aaee;}
QPushButton#danger{background:rgba(180,40,40,0.3);color:#ee6666;}
QSpinBox{background:#1e1e3a;border:1px solid #303055;border-radius:5px;color:#d0d0f0;padding:3px 6px;}
QLabel{color:#aaa;}
)");
    m_canvas=new AnnotCanvas(pixmap,this);
    // Dash animation timer for SelectBlur
    m_dashTimer=new QTimer(this);
    m_dashTimer->setInterval(80);
    connect(m_dashTimer,&QTimer::timeout,this,[this](){
        m_canvas->m_dashOffset=(m_canvas->m_dashOffset+1)%20;
        if(m_canvas->isDrawing()&&(m_canvas->currentTool()==AnnotTool::SelectBlur||
           m_canvas->currentTool()==AnnotTool::Crop)) m_canvas->update();
    });
    m_dashTimer->start();
    buildUI();
}

void AnnotationWidget::buildUI() {
    QVBoxLayout *main=new QVBoxLayout(this);
    main->setSpacing(0); main->setContentsMargins(0,0,0,0);
    QWidget *tb=new QWidget(); tb->setFixedHeight(52);
    tb->setStyleSheet("background:#0a0a18;border-bottom:2px solid #00aeff;");
    buildToolbar(tb); main->addWidget(tb);
    main->addWidget(m_canvas,1);
    QWidget *pb=new QWidget(); pb->setFixedHeight(54);
    pb->setStyleSheet("background:#0a0a18;border-top:1px solid #202040;");
    buildPalette(pb); main->addWidget(pb);

    auto addSc=[this](const QKeySequence &k,auto fn){
        auto *sc=new QShortcut(k,this); connect(sc,&QShortcut::activated,this,fn);
    };
    addSc(QKeySequence("Ctrl+Z"),[this](){m_canvas->undo();});
    addSc(QKeySequence("Ctrl+Y"),[this](){m_canvas->redo();});
    addSc(QKeySequence("Escape"),[this](){close();});
    addSc(QKeySequence("Ctrl+P"),[this](){onPrint();});
    addSc(QKeySequence("Ctrl+Shift+E"),[this](){onExportPdf();});
}

void AnnotationWidget::buildToolbar(QWidget *tb) {
    QHBoxLayout *lay=new QHBoxLayout(tb);
    lay->setContentsMargins(12,6,12,6); lay->setSpacing(5);

    QLabel *title=new QLabel("✏️  Annotation Mode");
    title->setStyleSheet("font-size:14px;font-weight:bold;color:#00aeff;background:transparent;");
    lay->addWidget(title); lay->addSpacing(12);

    struct TB{QString icon;QString tip;AnnotTool tool;};
    QList<TB> tools={
        {"↗","Arrow",AnnotTool::Arrow},
        {"▭","Rectangle",AnnotTool::Rect},
        {"◯","Ellipse",AnnotTool::Ellipse},
        {"╱","Line",AnnotTool::Line},
        {"✏","Freehand",AnnotTool::Freehand},
        {"T","Text (click to drag)",AnnotTool::Text},
        {"▌","Highlight",AnnotTool::Highlight},
        {"⬜","Blur",AnnotTool::Blur},
        {"⊡","Select & Blur",AnnotTool::SelectBlur},
        {"①","Step Number",AnnotTool::StepNum},
        {"🔍","Zoom",AnnotTool::Zoom},
        {"✂","Crop",AnnotTool::Crop},
    };

    QButtonGroup *grp=new QButtonGroup(this); grp->setExclusive(true);
    bool first=true;
    for(const auto &t:tools) {
        QPushButton *btn=new QPushButton(t.icon);
        btn->setToolTip(t.tip); btn->setFixedSize(36,36);
        btn->setCheckable(true); btn->setCursor(Qt::PointingHandCursor);
        if(first){btn->setChecked(true);btn->setObjectName("active");first=false;}
        grp->addButton(btn); m_toolBtns.append(btn); lay->addWidget(btn);
        AnnotTool tool=t.tool;
        connect(btn,&QPushButton::clicked,this,[this,tool,btn](){setActiveTool(tool,btn);});
    }
    lay->addSpacing(10);
    QLabel *wl=new QLabel("W:"); wl->setStyleSheet("color:#888;background:transparent;");
    lay->addWidget(wl);
    QSpinBox *ws=new QSpinBox(); ws->setRange(1,30); ws->setValue(3); ws->setFixedWidth(58);
    connect(ws,QOverload<int>::of(&QSpinBox::valueChanged),m_canvas,&AnnotCanvas::setWidth);
    lay->addWidget(ws);
    lay->addStretch();

    auto makeBtn=[&](const QString &lbl,const QString &tip,const QString &obj=""){
        QPushButton *b=new QPushButton(lbl); b->setToolTip(tip);
        b->setFixedHeight(34); b->setCursor(Qt::PointingHandCursor);
        if(!obj.isEmpty()) b->setObjectName(obj); return b;
    };
    QPushButton *undoBtn=makeBtn("↩","Undo Ctrl+Z");
    QPushButton *redoBtn=makeBtn("↪","Redo Ctrl+Y");
    QPushButton *clearBtn=makeBtn("🗑","Clear all");
    QPushButton *zoomRst=makeBtn("1:1","Reset zoom");
    QPushButton *printBtn=makeBtn("🖨","Print Ctrl+P");
    QPushButton *pdfBtn=makeBtn("📄","Export PDF Ctrl+Shift+E");
    QPushButton *copyBtn=makeBtn("📋","Copy");
    QPushButton *saveBtn=makeBtn("💾","Save");
    QPushButton *doneBtn=makeBtn("✔ Done","Apply","done");

    connect(undoBtn,&QPushButton::clicked,m_canvas,&AnnotCanvas::undo);
    connect(redoBtn,&QPushButton::clicked,m_canvas,&AnnotCanvas::redo);
    connect(clearBtn,&QPushButton::clicked,m_canvas,&AnnotCanvas::clearAll);
    connect(zoomRst,&QPushButton::clicked,this,[this](){m_canvas->resetZoom();});
    connect(printBtn,&QPushButton::clicked,this,&AnnotationWidget::onPrint);
    connect(pdfBtn,&QPushButton::clicked,this,&AnnotationWidget::onExportPdf);
    connect(copyBtn,&QPushButton::clicked,this,[this](){QApplication::clipboard()->setPixmap(m_canvas->flatten());});
    connect(saveBtn,&QPushButton::clicked,this,[this](){
        QString path=QFileDialog::getSaveFileName(this,"Save","annotated.png","PNG (*.png);;JPEG (*.jpg)");
        if(!path.isEmpty()) m_canvas->flatten().save(path);
    });
    connect(doneBtn,&QPushButton::clicked,this,[this](){emit annotationComplete(m_canvas->flatten());close();});

    for(auto *b:{undoBtn,redoBtn,clearBtn,zoomRst,printBtn,pdfBtn,copyBtn,saveBtn,doneBtn})
        lay->addWidget(b);
}

void AnnotationWidget::buildPalette(QWidget *pb) {
    QHBoxLayout *lay=new QHBoxLayout(pb);
    lay->setContentsMargins(16,8,16,8); lay->setSpacing(8);
    QLabel *cl=new QLabel("Color:");
    cl->setStyleSheet("color:#888;background:transparent;"); lay->addWidget(cl);
    QList<QColor> presets={
        QColor(255,60,60),QColor(255,140,0),QColor(255,230,0),
        QColor(60,220,80),QColor(0,180,255),QColor(160,80,255),
        QColor(255,100,180),QColor(255,255,255),QColor(20,20,20)
    };
    for(const auto &c:presets) {
        QPushButton *btn=new QPushButton();
        btn->setFixedSize(28,28); btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton{background:%1;border:2px solid #404060;border-radius:14px;}"
            "QPushButton:hover{border:2px solid #fff;}").arg(c.name()));
        connect(btn,&QPushButton::clicked,this,[this,c](){m_activeColor=c;m_canvas->setColor(c);});
        lay->addWidget(btn);
    }
    QPushButton *customBtn=new QPushButton("+ Custom");
    customBtn->setFixedHeight(28); customBtn->setCursor(Qt::PointingHandCursor);
    connect(customBtn,&QPushButton::clicked,this,[this](){
        QColor c=QColorDialog::getColor(m_activeColor,this,"Color",QColorDialog::ShowAlphaChannel);
        if(c.isValid()){m_activeColor=c;m_canvas->setColor(c);}
    });
    lay->addWidget(customBtn);
    lay->addSpacing(16);
    QPushButton *fillBtn=new QPushButton("■ Fill");
    fillBtn->setCheckable(true); fillBtn->setFixedHeight(28);
    connect(fillBtn,&QPushButton::toggled,m_canvas,&AnnotCanvas::setFilled);
    lay->addWidget(fillBtn);
    lay->addStretch();
    QLabel *hint=new QLabel("Ctrl+Z Undo  |  Ctrl+Y Redo  |  Ctrl+P Print  |  Ctrl+Shift+E PDF");
    hint->setStyleSheet("color:#444;font-size:10px;background:transparent;");
    lay->addWidget(hint);
}

void AnnotationWidget::onPrint() {
    // Save to temp file and open with system default (triggers print dialog)
    QPixmap px = m_canvas->flatten();
    QString path = QDir::temp().filePath("smp_print_tmp.png");
    if (px.save(path, "PNG")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else {
        QMessageBox::warning(this, "Print", "Could not prepare image for printing.");
    }
}

void AnnotationWidget::onExportPdf() {
    QString path = QFileDialog::getSaveFileName(this, "Export PDF",
        "screenshot.pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;
    QPixmap px = m_canvas->flatten();
    // Save as image first, then rename with .pdf hint
    // For a simple PDF-like export, save as high-quality PNG with PDF extension note
    QString pngPath = path + ".png";
    if (px.save(pngPath, "PNG")) {
        QMessageBox::information(this, "Export", "Saved as PNG: " + QFileInfo(pngPath).fileName());
        QDesktopServices::openUrl(QUrl::fromLocalFile(pngPath));
    }
}

void AnnotationWidget::setActiveTool(AnnotTool t,QPushButton *btn) {
    m_canvas->setTool(t);
    for(auto *b:m_toolBtns){
        b->setChecked(false); b->setObjectName("");
        b->setStyleSheet("");
    }
    if(btn){
        btn->setChecked(true);
        btn->setObjectName("active");
        btn->setStyleSheet("background:#00aeff;color:white;font-weight:bold;border:none;");
    }
}
