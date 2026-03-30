#pragma once
#include <QObject>
#include <QMap>
#include <QKeySequence>
#include <QSettings>
#include <QString>

// Maps action names to key sequences, persisted in QSettings
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    static HotkeyManager &instance() { static HotkeyManager h; return h; }

    // Default hotkeys
    static QMap<QString,QString> defaults() {
        return {
            {"capture_fullscreen",   "Print Screen"},
            {"capture_region",       "Ctrl+Shift+S"},
            {"capture_window",       "Alt+Print Screen"},
            {"capture_scroll",       "Ctrl+Shift+W"},
            {"capture_manual_scroll","Ctrl+Shift+M"},
            {"capture_timed",        "Ctrl+Shift+T"},
            {"capture_color",        "Ctrl+Shift+C"},
            {"record_start_stop",    "Ctrl+Shift+R"},
            {"copy_last",            "Ctrl+C"},
            {"save_as",              "Ctrl+S"},
            {"annotate",             "Ctrl+E"},
            {"ocr",                  "Ctrl+Shift+O"},
        };
    }

    QKeySequence get(const QString &action) const {
        QSettings s;
        auto defs = defaults();
        QString key = s.value("hotkey/" + action, defs.value(action, "")).toString();
        return QKeySequence(key);
    }

    void set(const QString &action, const QKeySequence &seq) {
        QSettings s;
        s.setValue("hotkey/" + action, seq.toString());
        emit hotkeyChanged(action, seq);
    }

    void reset(const QString &action) {
        QSettings s;
        s.remove("hotkey/" + action);
        emit hotkeyChanged(action, get(action));
    }

    void resetAll() {
        QSettings s;
        for (auto &k : defaults().keys()) s.remove("hotkey/" + k);
        emit allHotkeysReset();
    }

signals:
    void hotkeyChanged(const QString &action, const QKeySequence &seq);
    void allHotkeysReset();

private:
    HotkeyManager() {}
};

#define HK HotkeyManager::instance()
