#include "ScreenRecorder.h"
#include "RecordingCompleteDialog.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QScreenCapture>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QAudioInput>

ScreenRecorder::ScreenRecorder(QObject *parent) : QObject(parent) {
    m_outputPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                   + "/ScreenMasterPro";
    QDir().mkpath(m_outputPath);
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &ScreenRecorder::onTick);
    setupSession();
}

ScreenRecorder::~ScreenRecorder() { if (m_recording) stopRecording(); }

QString ScreenRecorder::ffmpegPath() {
    QString appDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(appDir + "/ffmpeg.exe")) return appDir + "/ffmpeg.exe";
    if (QFile::exists("ffmpeg.exe")) return QDir::current().absoluteFilePath("ffmpeg.exe");
    return {};
}

void ScreenRecorder::setupSession() {
    m_screenCapture = new QScreenCapture(this);
    m_screenCapture->setScreen(QGuiApplication::primaryScreen());
    m_session = new QMediaCaptureSession(this);
    m_session->setScreenCapture(m_screenCapture);
    m_recorder = new QMediaRecorder(this);
    m_session->setRecorder(m_recorder);
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, &ScreenRecorder::onRecorderError);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged,
            this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && m_recording && !m_usingFfmpeg) {
            m_recording = false; m_paused = false;
            m_tickTimer->stop();
            emit recordingStopped(m_outputFile);
            if (QFile::exists(m_outputFile)) {
                auto *dlg = new RecordingCompleteDialog(m_outputFile);
                dlg->setAttribute(Qt::WA_DeleteOnClose); dlg->show();
            }
        }
    });
}

void ScreenRecorder::startRecording() {
    if (m_recording) return;
    m_outputFile = buildOutputPath();
    m_elapsed = 0; m_recording = true; m_paused = false;
    QString ff = ffmpegPath();
    if (m_useRegion && !ff.isEmpty()) {
        m_usingFfmpeg = true;
        startFfmpegRecording();
    } else {
        if (m_useRegion && ff.isEmpty())
            emit recordingError("FFmpeg not found — recording full screen.\nPlace ffmpeg.exe next to ScreenMasterPro.exe");
        m_usingFfmpeg = false;
        startQtRecording();
    }
    m_tickTimer->start();
    emit recordingStarted();
}

void ScreenRecorder::startQtRecording() {
    QMediaFormat fmt;
    fmt.setFileFormat(QMediaFormat::MPEG4);
    auto codecs = QMediaFormat(QMediaFormat::MPEG4).supportedVideoCodecs(QMediaFormat::Encode);
    if (codecs.contains(QMediaFormat::VideoCodec::H264))
        fmt.setVideoCodec(QMediaFormat::VideoCodec::H264);
    else if (!codecs.isEmpty())
        fmt.setVideoCodec(codecs.first());

    // Audio support
    if (m_recordAudio) {
        auto aCodecs = QMediaFormat(QMediaFormat::MPEG4).supportedAudioCodecs(QMediaFormat::Encode);
        if (aCodecs.contains(QMediaFormat::AudioCodec::AAC))
            fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        else if (!aCodecs.isEmpty())
            fmt.setAudioCodec(aCodecs.first());

        // For Qt recording: use default audio input (mic)
        // System audio via Qt is not supported — use FFmpeg region recording for system audio
        m_audioInput = new QAudioInput(this);
        m_session->setAudioInput(m_audioInput);
    } else {
        m_session->setAudioInput(nullptr);
    }

    m_recorder->setMediaFormat(fmt);
    if (m_quality == "Low")         m_recorder->setQuality(QMediaRecorder::LowQuality);
    else if (m_quality == "Medium") m_recorder->setQuality(QMediaRecorder::NormalQuality);
    else if (m_quality == "Ultra")  m_recorder->setQuality(QMediaRecorder::VeryHighQuality);
    else                            m_recorder->setQuality(QMediaRecorder::HighQuality);
    m_recorder->setVideoFrameRate(m_fps);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_outputFile));
    m_screenCapture->start();
    m_recorder->record();
}

void ScreenRecorder::startFfmpegRecording() {
    QString ff = ffmpegPath();
    if (ff.isEmpty()) {
        emit recordingError("ffmpeg.exe not found next to ScreenMasterPro.exe");
        m_recording = false; return;
    }

    QRect r = m_recordRegion;
    if (!r.isValid() || r.width() < 10 || r.height() < 10) {
        emit recordingError("Invalid recording region"); m_recording = false; return;
    }

    // H264 requires width and height to be divisible by 2
    int w = r.width()  & ~1; // round down to even
    int h = r.height() & ~1; // round down to even
    r.setWidth(w);
    r.setHeight(h);

    QString sz = QString("%1x%2").arg(w).arg(h);
    QString fps    = QString::number(m_fps);
    QString preset = (m_quality=="Low")?"veryfast":(m_quality=="Medium")?"fast":(m_quality=="Ultra")?"slow":"medium";
    QString crf    = (m_quality=="Low")?"28":(m_quality=="Medium")?"23":(m_quality=="Ultra")?"18":"20";

    // Build args WITHOUT audio first (most reliable)
    QStringList args;
    args << "-y"
         << "-f"          << "gdigrab"
         << "-framerate"  << fps
         << "-offset_x"   << QString::number(r.x())
         << "-offset_y"   << QString::number(r.y())
         << "-video_size" << sz
         << "-draw_mouse" << (m_recordCursor ? "1" : "0")
         << "-i"          << "desktop"
         << "-c:v"        << "libx264"
         << "-preset"     << preset
         << "-crf"        << crf
         << "-pix_fmt"    << "yuv420p"
         << m_outputFile;

    // Try with audio if requested
    if (m_recordAudio) {
        // Build complete args with audio from scratch
        QStringList argsWithAudio;
        argsWithAudio << "-y"
                      << "-f"          << "gdigrab"
                      << "-framerate"  << fps
                      << "-offset_x"   << QString::number(r.x())
                      << "-offset_y"   << QString::number(r.y())
                      << "-video_size" << sz
                      << "-draw_mouse" << (m_recordCursor ? "1" : "0")
                      << "-i"         << "desktop";

        if (m_audioSource == "mic") {
            QString micName = getDshowMicDevice();
            argsWithAudio << "-f" << "dshow"
                          << "-i" << (micName.isEmpty() ? "audio=" : QString("audio=%1").arg(micName));
        } else {
            // WASAPI loopback — captures system audio (YouTube, browser, games)
            // Must use exact device name for USB/HDMI/Bluetooth speakers
            QString renderDevice = getWasapiLoopbackDevice();
            qDebug() << "WASAPI render device:" << renderDevice;

            argsWithAudio << "-f" << "wasapi" << "-loopback";
            if (!renderDevice.isEmpty()) {
                argsWithAudio << "-i" << renderDevice;
            } else {
                // Last resort: try empty string (some systems work with this)
                argsWithAudio << "-i" << "";
            }
        }

        argsWithAudio << "-c:v"     << "libx264"
                      << "-preset"  << preset
                      << "-crf"     << crf
                      << "-pix_fmt" << "yuv420p"
                      << "-c:a"     << "aac"
                      << "-b:a"     << "192k"
                      << m_outputFile;

        // Try with audio first, silent fallback to no audio if fails
        m_ffmpegArgs = argsWithAudio;
        m_ffmpegArgsNoAudio = args;
        m_audioFallback = true;
    } else {
        m_ffmpegArgs = args;
        m_audioFallback = false;
    }

    launchFfmpeg(m_ffmpegArgs);
}

// Gets the exact friendly name of the default Windows audio output device.
// This is needed for FFmpeg WASAPI loopback — works for USB, HDMI, 3.5mm, etc.
QString ScreenRecorder::getWasapiLoopbackDevice() {
#ifdef Q_OS_WIN
    // PowerShell: get default render (playback) device name
    QString ps = R"(
Add-Type -AssemblyName System.Windows.Forms | Out-Null
try {
    $policyConfig = [Activator]::CreateInstance([Type]::GetTypeFromCLSID([Guid]"294935CE-F637-4E7C-A41B-AB255460B862"))
} catch {}
# Use WMM to get default render device
$wmi = Get-WmiObject -Query "SELECT * FROM Win32_SoundDevice WHERE StatusInfo=3" 2>$null
if ($wmi) {
    $wmi | ForEach-Object { Write-Output $_.Name } | Select-Object -First 1
}
)";
    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-NonInteractive", "-Command", ps});
    proc.waitForFinished(4000);
    QString name = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (!name.isEmpty()) return name;

    // Fallback: use ffmpeg to list WASAPI render devices and find default
    QString ff = ffmpegPath();
    if (!ff.isEmpty()) {
        QProcess ffProc;
        ffProc.setProcessChannelMode(QProcess::MergedChannels);
        ffProc.start(ff, {"-list_devices", "true", "-f", "wasapi", "-i", "", "-t", "0"});
        ffProc.waitForFinished(5000);
        QString out = QString::fromUtf8(ffProc.readAllStandardOutput());

        // Parse ffmpeg output: find render device section, get default device name
        QStringList lines = out.split('\n');
        bool inRender = false;
        for (const QString &line : lines) {
            if (line.contains("render", Qt::CaseInsensitive)) {
                inRender = true;
            }
            if (line.contains("capture", Qt::CaseInsensitive)) {
                inRender = false;
            }
            if (inRender) {
                // FFmpeg format: [wasapi] "Device Name" (default)
                QRegularExpression re("\"([^\"]+)\"");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    return match.captured(1);
                }
            }
        }
    }

    return QString(); // empty = let FFmpeg decide
#else
    return QString();
#endif
}

// Returns the first available DirectShow audio capture device (microphone).
QString ScreenRecorder::getDshowMicDevice() {
#ifdef Q_OS_WIN
    QProcess ps;
    ps.start("powershell", {
        "-NoProfile", "-NonInteractive", "-Command",
        // List dshow audio devices, grab first one
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-WmiObject Win32_SoundDevice | "
        "Where-Object {$_.StatusInfo -eq 3} | "
        "Select-Object -First 1 -ExpandProperty Name | "
        "Write-Output"
    });
    ps.waitForFinished(3000);
    return QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
#else
    return QString();
#endif
}

void ScreenRecorder::launchFfmpeg(const QStringList &args) {
    if (m_ffmpeg) {
        m_ffmpeg->disconnect();
        m_ffmpeg->kill();
        m_ffmpeg->deleteLater();
    }
    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_ffmpeg, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this](int exitCode, QProcess::ExitStatus) {
        qDebug() << "FFmpeg exit:" << exitCode;

        // If failed quickly with audio, retry without audio
        if (exitCode != 0 && exitCode != 255 && m_audioFallback && m_recording) {
            qDebug() << "Audio recording failed, retrying without audio...";
            m_audioFallback = false;
            QTimer::singleShot(300, this, [this]() {
                if (!m_recording) return;
                launchFfmpeg(m_ffmpegArgsNoAudio);
            });
            return;
        }

        if (!m_recording && !m_usingFfmpeg) return;
        m_recording = false; m_paused = false;
        m_tickTimer->stop(); m_usingFfmpeg = false;
        emit recordingStopped(m_outputFile);

        bool success = (exitCode == 0 || exitCode == 255)
                       && QFile::exists(m_outputFile)
                       && QFileInfo(m_outputFile).size() > 10240;
        if (success) {
            auto *dlg = new RecordingCompleteDialog(m_outputFile);
            dlg->setAttribute(Qt::WA_DeleteOnClose); dlg->show();
        } else if (exitCode != 0 && exitCode != 255) {
            QString err = QString::fromUtf8(m_ffmpeg->readAllStandardOutput()).right(300);
            emit recordingError(QString("Recording failed (exit %1)\n%2").arg(exitCode).arg(err));
        }
    });

    QString ff = ffmpegPath();
    qDebug() << "=== FFmpeg command ===";
    qDebug() << ff;
    qDebug() << args.join(" ");
    qDebug() << "====================";
    m_ffmpeg->start(ff, args);
    if (!m_ffmpeg->waitForStarted(5000)) {
        m_recording = false; m_usingFfmpeg = false;
        emit recordingError("Failed to start FFmpeg: " + m_ffmpeg->errorString());
    }
}



void ScreenRecorder::stopRecording() {
    if (!m_recording) return;
    if (m_usingFfmpeg && m_ffmpeg && m_ffmpeg->state() == QProcess::Running) {
        m_ffmpeg->write("q\n");
        m_ffmpeg->closeWriteChannel();
        if (!m_ffmpeg->waitForFinished(8000)) {
            m_ffmpeg->terminate();
            m_ffmpeg->waitForFinished(2000);
        }
    } else {
        m_recorder->stop();
        m_screenCapture->stop();
        if (m_audioInput) { delete m_audioInput; m_audioInput = nullptr; }
    }
    m_tickTimer->stop();
    m_recording = false; m_paused = false; m_elapsed = 0;
    if (!m_usingFfmpeg) emit recordingStopped(m_outputFile);
}

void ScreenRecorder::pauseRecording() {
    if (!m_recording || m_paused) return;
    if (m_usingFfmpeg) { emit recordingError("Pause not supported in region mode"); return; }
    m_recorder->pause(); m_tickTimer->stop(); m_paused = true;
    emit recordingPaused();
}

void ScreenRecorder::resumeRecording() {
    if (!m_recording || !m_paused) return;
    m_recorder->record(); m_tickTimer->start(); m_paused = false;
    emit recordingResumed();
}

void ScreenRecorder::onTick() { if (!m_paused) { m_elapsed++; emit recordingTick(m_elapsed); } }
void ScreenRecorder::onRecorderError() { emit recordingError(m_recorder->errorString()); if (m_recording) stopRecording(); }
void ScreenRecorder::setFullScreen()  { clearRegion(); m_screenCapture->setScreen(QGuiApplication::primaryScreen()); }
void ScreenRecorder::selectRegion()   { }
void ScreenRecorder::selectWindow()   { clearRegion(); m_screenCapture->setScreen(QGuiApplication::primaryScreen()); }
void ScreenRecorder::setOutputPath(const QString &p) { m_outputPath = p; QDir().mkpath(p); }
QString ScreenRecorder::buildOutputPath() {
    return m_outputPath + "/Recording_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".mp4";
}
