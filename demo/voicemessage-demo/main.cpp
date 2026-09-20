/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/demo/voicemessage-demo/main.cpp
  *
  *  A message composer with the microphone button, recording for real.
  *
  *  It exists to try what has never run: VoiceRecorderEngine and VoicePlaybackEngine with a real
  *  microphone, a real loudspeaker and the real recorder popup of the composer. The controls are
  *  the paths that are in doubt (devices by id, the permission, a short length limit, an encrypted
  *  file), and every signal of the engines and of the popup goes into the log.
  *
  *  Nothing in it is meant to be reused: an application wires the two engines the same way, but
  *  keeps its own files, keys and threads.
  *
  */

/****************************************************************************/

#include <QApplication>
#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QMediaDevices>
#include <QMetaEnum>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QUrl>

#include <uise/desktop/style.hpp>
#include <uise/desktop/utils/layout.hpp>
#include <uise/desktop/messageeditor.hpp>
#include <uise/desktop/abstractvoicerecorderdialog.hpp>
#include <uise/desktop/audioplayer.hpp>
#include <uise/desktop/waveformbar.hpp>

#ifdef HATN_UISE_VOICE

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>

#include <hatn/common/bytearray.h>
#include <hatn/common/error.h>
#include <hatn/common/file.h>
#include <hatn/common/format.h>
#include <hatn/common/logger.h>
#include <hatn/common/plainfile.h>
#include <hatn/common/plugin.h>
#include <hatn/common/thread.h>

#include <hatn/crypt/cryptplugin.h>
#include <hatn/crypt/ciphersuite.h>
#include <hatn/crypt/cryptfile.h>

#include <hatn/media/voicerecorder.h>

#include <hatnuise/voiceplaybackengine.h>
#include <hatnuise/voicerecorderengine.h>
#include <hatnuise/voicerecordingresult.h>

#endif

UISE_DESKTOP_USING

#ifdef HATN_UISE_VOICE

HATN_UISE_USING

namespace common=hatn::common;
namespace hcrypt=hatn::crypt;

namespace
{

constexpr int ShortLimitMs=10000;

//---------------------------------------------------------------

//! What a file needs to be encrypted. It is the setup of mediatests/test/testvoicecryptfile.cpp, without a test around it.
struct CryptContext
{
    std::shared_ptr<void> pluginInfos;
    std::shared_ptr<hcrypt::CryptPlugin> plugin;
    std::shared_ptr<hcrypt::CipherSuite> suite;
    common::SharedPtr<hcrypt::SymmetricKey> masterKey;
    bool startedLogger=false;

    //! Set once the setup was tried, whatever came of it.
    bool tried=false;

    //! Why the setup failed; empty when it did not.
    std::string failure;

    bool ready() const noexcept
    {
        return tried && failure.empty();
    }

    void setup(const std::string& pluginDir)
    {
        tried=true;

        // The test starts the logger and so does this: it is the setup that is known to work.
        if (!common::Logger::isRunning())
        {
            auto handler=[](const common::FmtAllocatedBufferChar& s)
            {
                std::cout<<common::lib::toStringView(s)<<std::endl;
            };
            common::Logger::setDefaultVerbosity(common::LoggerVerbosity::INFO);
            common::Logger::setFatalTracing(false);
            common::Logger::setOutputHandler(handler);
            common::Logger::setFatalLogHandler(handler);
            common::Logger::start(false);
            startedLogger=true;
        }

        // The plugins found stay loaded while the list lives.
        auto found=common::PluginLoader::instance().listDynamicPlugins(pluginDir);
        pluginInfos=std::make_shared<decltype(found)>(std::move(found));

        const common::PluginInfo* openssl=nullptr;
        const auto infos=common::PluginLoader::instance().listPlugins(hcrypt::CryptPlugin::Type);
        for (auto&& info : infos)
        {
            if (info->name.find("openssl")!=std::string::npos)
            {
                openssl=info;
                break;
            }
        }
        if (openssl==nullptr)
        {
            failure="the openssl crypt plugin was not found in "+pluginDir;
            return;
        }

        auto loaded=common::PluginLoader::instance().loadPlugin<hcrypt::CryptPlugin>(openssl);
        if (loaded)
        {
            failure="failed to load the openssl crypt plugin: "+loaded.error().message();
            return;
        }
        plugin=loaded.takeValue();

        auto ec=plugin->init();
        if (ec)
        {
            failure="failed to init the openssl crypt plugin: "+ec.message();
            return;
        }

        // HKDF from a raw key: no passphrase, so opening a file costs no key stretching.
        const std::string suiteJson=
            "{\"id\":\"voicemessage-demo-suite\",\"aead\":\"chacha20-poly1305\",\"pbkdf\":\"pbkdf2/sha256\","
            "\"digest\":\"sha512\",\"mac\":\"poly1305\",\"hkdf\":\"sha256\"}";
        suite=std::make_shared<hcrypt::CipherSuite>();
        ec=suite->loadFromJSON(common::ByteArray(suiteJson.data(),suiteJson.size()));
        if (ec)
        {
            failure="bad cipher suite: "+ec.message();
            return;
        }
        hcrypt::CipherSuitesGlobal::instance().addSuite(suite);
        hcrypt::CipherSuitesGlobal::instance().setDefaultEngine(std::make_shared<hcrypt::CryptEngine>(plugin.get()));

        const hcrypt::CryptAlgorithm* aead=nullptr;
        ec=suite->aeadAlgorithm(aead);
        if (ec || aead==nullptr)
        {
            failure="the suite has no AEAD algorithm";
            return;
        }
        masterKey=aead->createSymmetricKey();
        if (!masterKey)
        {
            failure="failed to create the master key";
            return;
        }
        common::ByteArray keyData;
        keyData.resize(32);
        for (size_t i=0;i<keyData.size();i++)
        {
            keyData.data()[i]=static_cast<char>(i*7+3);
        }
        ec=masterKey->importFromBuf(keyData,hcrypt::ContainerFormat::RAW_PLAIN);
        if (ec)
        {
            failure="failed to import the master key: "+ec.message();
        }
    }

    //! Only after every file made with the key is gone.
    void teardown()
    {
        if (!tried)
        {
            return;
        }
        hcrypt::CipherSuitesGlobal::instance().reset();
        masterKey.reset();
        if (plugin)
        {
            std::ignore=plugin->cleanup();
        }
        plugin.reset();
        suite.reset();
        pluginInfos.reset();
        if (startedLogger)
        {
            common::Logger::stop();
            startedLogger=false;
        }
    }
};

//---------------------------------------------------------------

template <typename EnumT>
QString enumName(EnumT value)
{
    const auto* key=QMetaEnum::fromType<EnumT>().valueToKey(static_cast<int>(value));
    return key!=nullptr ? QString::fromLatin1(key) : QString::number(static_cast<int>(value));
}

QString fraction(qreal value)
{
    return QString::number(value,'f',4);
}

//---------------------------------------------------------------

/**
 * Everything the demo holds, in one place that outlives the event loop.
 *
 * There is no QObject in here on purpose: the engines are NOT children of a widget, because
 * their destructors take their timers off the worker thread and that thread has to be alive
 * for it, so shutdown() takes them down in an order that Qt would not.
 */
struct Demo
{
    std::string cryptPluginDir;

    std::shared_ptr<common::Thread> thread;
    std::unique_ptr<VoiceRecorderEngine> recorder;
    std::unique_ptr<VoicePlaybackEngine> engineFinal;
    std::unique_ptr<VoicePlaybackEngine> engineOriginal;
    CryptContext crypt;

    QPointer<QPlainTextEdit> logEdit;
    QPointer<QLabel> capsLabel;
    QPointer<QLabel> detailFinal;
    QPointer<QLabel> detailOriginal;
    QPointer<QWidget> originalBox;
    QPointer<QCheckBox> cryptBox;
    QPointer<QCheckBox> shortLimitBox;
    QPointer<QCheckBox> allTicksBox;
    QPointer<MessageEditor> editor;
    AudioPlayer* playerFinal=nullptr;
    AudioPlayer* playerOriginal=nullptr;

    QString dir;

    //! The recording that is going, and how it was made.
    QString currentPath;
    bool currentEncrypted=false;

    qint64 lastElapsedSecond=-1;
    qint64 lastPositionSecond=-1;

    //---------------------------------------------------------------

    void log(const QString& text) const
    {
        const auto line=QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"))+QStringLiteral("  ")+text;
        if (!logEdit.isNull())
        {
            logEdit->appendPlainText(line);
        }
        qInfo().noquote()<<line;
    }

    //---------------------------------------------------------------

    void refreshCapabilities() const
    {
        if (capsLabel.isNull())
        {
            return;
        }
        const auto yesNo=[](bool value)
        {
            return value ? QStringLiteral("yes") : QStringLiteral("NO");
        };
        capsLabel->setText(
            QStringLiteral("recorder codec: %1   microphone present: %2   microphone allowed: %3   player codec: %4")
                .arg(yesNo(VoiceRecorderEngine::isAvailable()))
                .arg(yesNo(VoiceRecorderEngine::hasInputDevice()))
                .arg(yesNo(VoiceRecorderEngine::isPermissionGranted()))
                .arg(yesNo(VoicePlaybackEngine::isAvailable()))
        );
    }

    //---------------------------------------------------------------

    //! The crypt setup is made when it is first wanted. A failure switches the option off, nothing else.
    bool ensureCrypt()
    {
        if (!crypt.tried)
        {
            crypt.setup(cryptPluginDir);
            if (crypt.ready())
            {
                log(QStringLiteral("crypt: the openssl plugin loaded from %1").arg(QString::fromStdString(cryptPluginDir)));
            }
            else
            {
                log(QStringLiteral("crypt: UNAVAILABLE, %1").arg(QString::fromStdString(crypt.failure)));
            }
        }
        return crypt.ready();
    }

    //---------------------------------------------------------------

    //! A NEW handle, not opened: a plain file, or a CryptFile that the key opens.
    std::shared_ptr<common::File> makeFile(bool encrypted, bool forWriting)
    {
        if (!encrypted)
        {
            return std::make_shared<common::PlainFile>();
        }
        if (!ensureCrypt())
        {
            return std::shared_ptr<common::File>{};
        }
        auto file=std::make_shared<hcrypt::CryptFile>(crypt.masterKey.get(),crypt.suite.get());
        if (forWriting)
        {
            auto& container=file->processor();
            container.setKdfType(hcrypt::container_descriptor::KdfType::HKDF);
            container.setSalt(std::string("voicemessage-demo"));
        }
        return file;
    }

    //---------------------------------------------------------------

    //! An encrypted file is not an Ogg stream on disk, so it does not carry the extension of one.
    QString newPath(bool encrypted)
    {
        QDir().mkpath(dir);
        return dir+QStringLiteral("/voice-")+QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmsszzz"))
               +(encrypted ? QStringLiteral(".oggc") : QStringLiteral(".ogg"));
    }

    //---------------------------------------------------------------

    void deleteFile(const QString& path, const QString& why) const
    {
        if (path.isEmpty())
        {
            return;
        }
        const auto removed=QFile::remove(path);
        log(QStringLiteral("file %1: %2 (%3)").arg(removed ? QStringLiteral("deleted") : QStringLiteral("NOT deleted")).arg(path).arg(why));
    }

    //---------------------------------------------------------------

    static QString describe(const QString& path, const hatn::media::VoiceRecording& recording, bool encrypted)
    {
        return QStringLiteral("%1\n%2 ms, %3 bytes, %4 Hz, %5 ch, waveform %6 bytes, %7")
            .arg(path)
            .arg(recording.durationMs)
            .arg(recording.fileBytes)
            .arg(recording.sampleRate)
            .arg(recording.channels)
            .arg(recording.waveform.size())
            .arg(encrypted ? QStringLiteral("ENCRYPTED") : QStringLiteral("plain"));
    }

    //---------------------------------------------------------------

    //! Load a message into one of the two players and leave it stopped: the message itself, or the original of a cropped one.
    void loadPlayer(
            bool original,
            const QString& path,
            bool encrypted,
            const hatn::media::VoiceRecording& recording
        )
    {
        const auto name=original ? QStringLiteral("original") : QStringLiteral("message");
        auto* player=original ? playerOriginal : playerFinal;
        auto* engine=original ? engineOriginal.get() : engineFinal.get();
        auto& detail=original ? detailOriginal : detailFinal;

        engine->stop();

        const QByteArray waveform(
            reinterpret_cast<const char*>(recording.waveform.data()),
            static_cast<qsizetype>(recording.waveform.size())
        );
        player->setTitle(QFileInfo(path).fileName());
        player->setWaveform(waveform);
        player->setDuration(static_cast<qint64>(recording.durationMs));
        player->setPosition(0);

        if (encrypted)
        {
            auto file=makeFile(true,false);
            if (!file)
            {
                log(QStringLiteral("%1 player: cannot make a read handle").arg(name));
                return;
            }
            const auto utf8=path.toUtf8().toStdString();
            const auto ec=file->open(utf8,common::File::Mode::read);
            if (ec)
            {
                log(QStringLiteral("%1 player: cannot open %2 for reading: %3")
                        .arg(name)
                        .arg(path)
                        .arg(QString::fromStdString(ec.message())));
                return;
            }
            engine->openFile(file);
        }
        else
        {
            engine->open(path);
        }

        if (!detail.isNull())
        {
            detail->setText(describe(path,recording,encrypted));
        }
        if (original && !originalBox.isNull())
        {
            originalBox->setVisible(true);
        }
        log(QStringLiteral("%1 player loaded: %2").arg(name).arg(path));
    }

    //---------------------------------------------------------------

    void onFinished(const VoiceRecordingResult& result)
    {
        const auto encrypted=currentEncrypted;
        log(QStringLiteral("FINISHED: %1 ms, %2 bytes, %3 Hz, %4 ch, waveform %5 bytes, crop %6 .. %7, comment \"%8\", overruns %9")
                .arg(result.recording.durationMs)
                .arg(result.recording.fileBytes)
                .arg(result.recording.sampleRate)
                .arg(result.recording.channels)
                .arg(result.waveformBytes().size())
                .arg(fraction(result.cropStart))
                .arg(fraction(result.cropEnd))
                .arg(result.comment)
                .arg(recorder->overruns()));
        if (recorder->overruns()!=0)
        {
            log(QStringLiteral("  overruns is not 0: the worker fell behind and the file has a gap"));
        }

        if (!originalBox.isNull())
        {
            originalBox->setVisible(false);
        }

        // What Send makes is what the player shows. Without a trim that is the recording itself.
        if (!isCropped(result))
        {
            loadPlayer(false,result.path,encrypted,result.recording);
            return;
        }

        // The trim is applied here, by the caller, into a second file with the extension of the first.
        // The message is that file, and the recording stays in the second player to be compared with.
        const QFileInfo recorded(result.path);
        const auto croppedPath=recorded.path()+QStringLiteral("/")+recorded.completeBaseName()+QStringLiteral("-cropped.")+recorded.suffix();
        auto in=makeFile(encrypted,false);
        auto out=makeFile(encrypted,true);
        if (!in || !out)
        {
            log(QStringLiteral("crop: cannot make the files, the message is shown uncropped"));
            loadPlayer(false,result.path,encrypted,result.recording);
            return;
        }
        auto ec=in->open(result.path.toUtf8().toStdString(),common::File::Mode::read);
        if (ec)
        {
            log(QStringLiteral("crop: cannot open the recording: %1, the message is shown uncropped").arg(QString::fromStdString(ec.message())));
            loadPlayer(false,result.path,encrypted,result.recording);
            return;
        }
        ec=out->open(croppedPath.toUtf8().toStdString(),common::File::Mode::write_new);
        if (ec)
        {
            common::Error closeError;
            in->close(closeError);
            log(QStringLiteral("crop: cannot create %1: %2, the message is shown uncropped").arg(croppedPath).arg(QString::fromStdString(ec.message())));
            loadPlayer(false,result.path,encrypted,result.recording);
            return;
        }

        hatn::media::VoiceRecording cropped;
        ec=cropRecording(result,*in,*out,cropped);
        common::Error closeIn;
        common::Error closeOut;
        in->close(closeIn);
        out->close(closeOut);
        if (ec || closeOut)
        {
            log(QStringLiteral("crop FAILED: %1, the message is shown uncropped").arg(QString::fromStdString(ec ? ec.message() : closeOut.message())));
            deleteFile(croppedPath,QStringLiteral("incomplete"));
            loadPlayer(false,result.path,encrypted,result.recording);
            return;
        }

        log(QStringLiteral("CROPPED %1 .. %2: %3 ms -> %4 ms, %5 bytes -> %6 bytes")
                .arg(fraction(result.cropStart))
                .arg(fraction(result.cropEnd))
                .arg(result.recording.durationMs)
                .arg(cropped.durationMs)
                .arg(result.recording.fileBytes)
                .arg(cropped.fileBytes));
        loadPlayer(false,croppedPath,encrypted,cropped);
        loadPlayer(true,result.path,encrypted,result.recording);
    }

    //---------------------------------------------------------------

    //! With nothing to record, the popup has nothing to show. It is closed a turn later, not from inside its own signal.
    void closePopupLater()
    {
        QTimer::singleShot(0,logEdit.data(),
            [this]()
            {
                log(QStringLiteral("closing the popup, there is no recording"));
                if (!editor.isNull())
                {
                    editor->closeVoiceRecorder();
                }
            }
        );
    }

    //---------------------------------------------------------------

    void voiceRecorderOpened(AbstractVoiceRecorderDialog* dialog)
    {
        log(QStringLiteral("---- popup opened (dialog %1)").arg(reinterpret_cast<quintptr>(dialog),0,16));

        hatn::media::VoiceRecorderConfig config;
        if (!shortLimitBox.isNull() && shortLimitBox->isChecked())
        {
            config.maxDurationMs=ShortLimitMs;
        }
        recorder->setConfig(config);

        currentEncrypted=!cryptBox.isNull() && cryptBox->isChecked();
        currentPath=newPath(currentEncrypted);
        lastElapsedSecond=-1;

        auto file=makeFile(currentEncrypted,true);
        bool started=false;
        if (file)
        {
            const auto ec=file->open(currentPath.toUtf8().toStdString(),common::File::Mode::write_new);
            if (ec)
            {
                log(QStringLiteral("cannot create %1: %2").arg(currentPath).arg(QString::fromStdString(ec.message())));
            }
            else
            {
                const auto encrypted=currentEncrypted;
                started=recorder->start(
                    file,
                    currentPath,
                    [this,encrypted]()
                    {
                        return makeFile(encrypted,false);
                    }
                );
                log(QStringLiteral("start(%1 file, max %2 ms): %3")
                        .arg(currentEncrypted ? QStringLiteral("ENCRYPTED") : QStringLiteral("plain"))
                        .arg(config.maxDurationMs)
                        .arg(started ? QStringLiteral("ok") : QStringLiteral("REFUSED, see errorOccurred above")));
            }
        }
        else
        {
            log(QStringLiteral("no file to record into, the encrypted option is not available"));
        }
        refreshCapabilities();

        if (!started)
        {
            // A start() that got as far as the engine leaves it Failed, and its stateChanged() handler has
            // deleted the file and closed the popup. What is left is a file that was never handed over.
            if (recorder->state()!=VoiceRecorderEngine::State::Failed)
            {
                const auto path=currentPath;
                currentPath.clear();
                deleteFile(path,QStringLiteral("recording did not start"));
                closePopupLater();
            }
            return;
        }

        // The log's connections come BEFORE the engine's (attachDialog() below), so that every line shows
        // the state as it was when the request arrived, before the engine acted on it. The log widget is
        // their context object: voiceRecorderClosed() drops exactly these.
        auto* context=logEdit.data();
        const auto dialogState=[dialog]()
        {
            return enumName(dialog->state());
        };
        const auto engineState=[this]()
        {
            return enumName(recorder->state());
        };

        QObject::connect(dialog,&AbstractVoiceRecorderDialog::pinned,context,
            [this,dialogState,engineState]()
            {
                log(QStringLiteral("dialog: pinned (dialog %1, engine %2)").arg(dialogState()).arg(engineState()));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::pauseRequested,context,
            [this,dialogState,engineState]()
            {
                log(QStringLiteral("dialog: pauseRequested (dialog %1, engine %2)").arg(dialogState()).arg(engineState()));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::resumeRequested,context,
            [this,dialog,dialogState,engineState]()
            {
                // what the recording goes on from: the whole of it, or only what the crop handles keep
                log(QStringLiteral("dialog: resumeRequested (dialog %1, engine %2), crop %3 .. %4 of %5 ms")
                        .arg(dialogState())
                        .arg(engineState())
                        .arg(fraction(dialog->cropStart()))
                        .arg(fraction(dialog->cropEnd()))
                        .arg(recorder->elapsedMs()));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::listenRequested,context,
            [this,dialogState,engineState]()
            {
                log(QStringLiteral("dialog: listenRequested (dialog %1, engine %2)").arg(dialogState()).arg(engineState()));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::listenPauseRequested,context,
            [this,dialogState,engineState]()
            {
                log(QStringLiteral("dialog: listenPauseRequested (dialog %1, engine %2)").arg(dialogState()).arg(engineState()));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::seekRequested,context,
            [this](qreal value)
            {
                // compare with the preListenPositionChanged that follows: it is that fraction of the length the dialog was last told
                log(QStringLiteral("dialog: seekRequested fraction %1 of %2 ms = %3 ms expected")
                        .arg(fraction(value))
                        .arg(recorder->elapsedMs())
                        .arg(static_cast<qint64>(value*static_cast<qreal>(recorder->elapsedMs()))));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::cropChanged,context,
            [this](qreal start, qreal end)
            {
                log(QStringLiteral("dialog: cropChanged %1 .. %2").arg(fraction(start)).arg(fraction(end)));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::sendRequested,context,
            [this,dialogState,engineState](const QString& comment, qreal start, qreal end)
            {
                log(QStringLiteral("dialog: SEND requested (dialog %1, engine %2), crop %3 .. %4, comment \"%5\"")
                        .arg(dialogState())
                        .arg(engineState())
                        .arg(fraction(start))
                        .arg(fraction(end))
                        .arg(comment));
            }
        );
        QObject::connect(dialog,&AbstractVoiceRecorderDialog::cancelRequested,context,
            [this,dialogState,engineState]()
            {
                log(QStringLiteral("dialog: CANCEL requested (dialog %1, engine %2)").arg(dialogState()).arg(engineState()));
            }
        );

        recorder->attachDialog(dialog);
    }

    //---------------------------------------------------------------

    void voiceRecorderClosed(QPointer<AbstractVoiceRecorderDialog> dialog)
    {
        log(QStringLiteral("---- popup closed (engine %1)").arg(enumName(recorder->state())));

        // The popup is kept between recordings and handed out again at the next press, so
        // everything connected to it goes now: the engine's connections, then the log's.
        recorder->detachDialog();
        if (!dialog.isNull() && !logEdit.isNull())
        {
            QObject::disconnect(dialog.data(),nullptr,logEdit.data(),nullptr);
        }

        // a no-op after a send; after Escape or the close button it is the cancellation
        recorder->cancel();
        refreshCapabilities();
    }

    //---------------------------------------------------------------

    void connectRecorder()
    {
        QObject::connect(recorder.get(),&VoiceRecorderEngine::stateChanged,logEdit.data(),
            [this](VoiceRecorderEngine::State state)
            {
                log(QStringLiteral("engine: state -> %1").arg(enumName(state)));
                if (state==VoiceRecorderEngine::State::Failed)
                {
                    // the host should close the popup; the file is ours to delete
                    const auto path=currentPath;
                    currentPath.clear();
                    deleteFile(path,QStringLiteral("the recording failed"));
                    closePopupLater();
                }
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::elapsedMsChanged,logEdit.data(),
            [this](qint64 ms)
            {
                if (allTicksBox->isChecked() || ms/1000!=lastElapsedSecond)
                {
                    lastElapsedSecond=ms/1000;
                    log(QStringLiteral("engine: elapsed %1 ms").arg(ms));
                }
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::waveformChanged,logEdit.data(),
            [this](const QByteArray& waveform)
            {
                const auto peak=waveform.isEmpty() ? 0 : static_cast<int>(static_cast<quint8>(*std::max_element(waveform.begin(),waveform.end(),
                    [](char a, char b)
                    {
                        return static_cast<quint8>(a)<static_cast<quint8>(b);
                    })));
                log(QStringLiteral("engine: waveform %1 bytes, peak %2 of 255").arg(waveform.size()).arg(peak));
                if (!waveform.isEmpty() && peak==0)
                {
                    log(QStringLiteral("  the waveform is all zero: the microphone gave silence (a denied permission does that on macOS)"));
                }
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::preListenPositionChanged,logEdit.data(),
            [this](qint64 ms)
            {
                if (allTicksBox->isChecked() || ms/1000!=lastPositionSecond)
                {
                    lastPositionSecond=ms/1000;
                    log(QStringLiteral("engine: pre-listen position %1 ms").arg(ms));
                }
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::preListenEnded,logEdit.data(),
            [this]()
            {
                log(QStringLiteral("engine: pre-listen ended (or failed, see errorOccurred)"));
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::limitReached,logEdit.data(),
            [this]()
            {
                log(QStringLiteral("engine: LIMIT reached at %1 ms, it is paused; resume must be refused").arg(recorder->elapsedMs()));
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::recordingFinished,logEdit.data(),
            [this](const VoiceRecordingResult& result)
            {
                onFinished(result);
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::recordingTooShort,logEdit.data(),
            [this]()
            {
                // not an error: a tap on the button
                log(QStringLiteral("engine: too short, not a message"));
                const auto path=currentPath;
                currentPath.clear();
                deleteFile(path,QStringLiteral("too short"));
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::recordingCancelled,logEdit.data(),
            [this]()
            {
                log(QStringLiteral("engine: cancelled"));
                const auto path=currentPath;
                currentPath.clear();
                deleteFile(path,QStringLiteral("cancelled"));
            }
        );
        QObject::connect(recorder.get(),&VoiceRecorderEngine::errorOccurred,logEdit.data(),
            [this](const QString& message)
            {
                log(QStringLiteral("engine: ERROR: %1").arg(message));
                refreshCapabilities();
            }
        );
    }

    //---------------------------------------------------------------

    void connectPlayer(const QString& name, VoicePlaybackEngine* engine, AudioPlayer* player)
    {
        QObject::connect(engine,&VoicePlaybackEngine::stateChanged,logEdit.data(),
            [this,name](VoicePlaybackEngine::State state)
            {
                log(QStringLiteral("%1 player: state -> %2").arg(name).arg(enumName(state)));
            }
        );
        QObject::connect(engine,&VoicePlaybackEngine::durationChanged,logEdit.data(),
            [this,name](qint64 ms)
            {
                log(QStringLiteral("%1 player: duration %2 ms").arg(name).arg(ms));
            }
        );
        QObject::connect(engine,&VoicePlaybackEngine::errorOccurred,logEdit.data(),
            [this,name](const QString& message)
            {
                log(QStringLiteral("%1 player: ERROR: %2").arg(name).arg(message));
            }
        );
        // the position is not logged: the bar shows it
        QObject::connect(player,&AbstractAudioPlayer::seekRequested,logEdit.data(),
            [this,name,engine](qint64 ms)
            {
                log(QStringLiteral("%1 player: seek requested to %2 ms of %3 ms").arg(name).arg(ms).arg(engine->durationMs()));
            }
        );
        QObject::connect(player,&AbstractAudioPlayer::speedChanged,logEdit.data(),
            [this,name](qreal speed)
            {
                log(QStringLiteral("%1 player: speed %2").arg(name).arg(speed));
            }
        );
    }

    //---------------------------------------------------------------

    //! Before the widgets and the event loop are gone, and in this order.
    void shutdown()
    {
        if (playerFinal!=nullptr)
        {
            playerFinal->setEngine(nullptr);
        }
        if (playerOriginal!=nullptr)
        {
            playerOriginal->setEngine(nullptr);
        }

        // Each destructor takes its timers off the worker and closes its file.
        engineOriginal.reset();
        engineFinal.reset();
        recorder.reset();

        if (thread)
        {
            thread->stop();
            thread.reset();
        }

        // No file made with the key is left.
        crypt.teardown();
    }
};

//---------------------------------------------------------------

void fillDevices(QComboBox* combo, const QList<QAudioDevice>& devices, const QAudioDevice& systemDefault)
{
    const QSignalBlocker blocker(combo);
    const auto keep=combo->currentData().toByteArray();
    combo->clear();
    combo->addItem(QStringLiteral("(system default: %1)").arg(systemDefault.isNull() ? QStringLiteral("none") : systemDefault.description()),QByteArray());
    for (const auto& device : devices)
    {
        combo->addItem(device.description(),device.id());
    }
    const auto index=combo->findData(keep);
    combo->setCurrentIndex(index>=0 ? index : 0);
}

}

//---------------------------------------------------------------

int main(int argc, char *argv[])
{
    QApplication app(argc,argv);

    Demo demo;
    // next to the executable, where the build puts it, so the working directory does not matter
    demo.cryptPluginDir=(QCoreApplication::applicationDirPath()+QStringLiteral("/plugins/crypt")).toStdString();
    for (int i=1;i<argc;i++)
    {
        const auto arg=QString::fromLocal8Bit(argv[i]);
        const auto prefix=QStringLiteral("--crypt-plugin-dir=");
        if (arg.startsWith(prefix))
        {
            demo.cryptPluginDir=arg.mid(prefix.size()).toStdString();
        }
    }
    demo.dir=QStandardPaths::writableLocation(QStandardPaths::TempLocation)+QStringLiteral("/voicemessage-demo");

    Style::instance().applyStyleSheet();

    // The theme sheets colour the window, labels, check boxes, combo boxes, buttons and the recorder popup, and
    // not a QPlainTextEdit, which is the log: it gets its colours here, or a dark system with the light theme
    // would show a dark log on a light window. Reloading also redraws the icons that exist already.
    const auto applyTheme=[](Style::StyleSheetMode mode)
    {
        auto& style=Style::instance();
        style.setStyleSheetMode(mode);
        style.setBaseQss(style.isDarkTheme()
            ? QStringLiteral("QPlainTextEdit { background-color: #1E1E1E; color: #DDDDDD; }")
            : QStringLiteral("QPlainTextEdit { background-color: #FFFFFF; color: #222222; }"));
        style.applyStyleSheet(true);
    };
    applyTheme(Style::instance().isDarkTheme() ? Style::StyleSheetMode::Dark : Style::StyleSheetMode::Light);

    QMainWindow w;
    auto* mainFrame=new QFrame();
    auto* l=Layout::vertical(mainFrame);
    l->setContentsMargins(16,16,16,16);
    l->setSpacing(8);

    // ---- engines: one worker thread for all of them
    demo.thread=std::make_shared<common::Thread>("voicedemo");
    demo.thread->start();
    demo.recorder=std::make_unique<VoiceRecorderEngine>(demo.thread);
    demo.engineFinal=std::make_unique<VoicePlaybackEngine>(demo.thread);
    demo.engineOriginal=std::make_unique<VoicePlaybackEngine>(demo.thread);

    // ---- capabilities
    demo.capsLabel=new QLabel(mainFrame);
    demo.capsLabel->setWordWrap(true);
    l->addWidget(demo.capsLabel);

    // ---- devices
    auto* devices=new QMediaDevices(&w);
    auto* devicesRow=new QFrame(mainFrame);
    auto* devicesLayout=Layout::horizontal(devicesRow);
    devicesLayout->setContentsMargins(0,0,0,0);
    auto* inputCombo=new QComboBox(devicesRow);
    auto* outputCombo=new QComboBox(devicesRow);
    auto* rescan=new QPushButton("Rescan devices",devicesRow);
    devicesLayout->addWidget(new QLabel("Microphone:",devicesRow));
    devicesLayout->addWidget(inputCombo,1);
    devicesLayout->addWidget(new QLabel("Output:",devicesRow));
    devicesLayout->addWidget(outputCombo,1);
    devicesLayout->addWidget(rescan);
    l->addWidget(devicesRow);

    // ---- options
    auto* optionsRow=new QFrame(mainFrame);
    auto* optionsLayout=Layout::horizontal(optionsRow);
    optionsLayout->setContentsMargins(0,0,0,0);
    demo.cryptBox=new QCheckBox("Record encrypted (CryptFile)",optionsRow);
    demo.shortLimitBox=new QCheckBox(QString("Short limit (%1 s), at the next start").arg(ShortLimitMs/1000),optionsRow);
    auto* enabledBox=new QCheckBox("Voice messages enabled",optionsRow);
    enabledBox->setChecked(true);
    optionsLayout->addWidget(demo.cryptBox);
    optionsLayout->addWidget(demo.shortLimitBox);
    optionsLayout->addWidget(enabledBox);
    optionsLayout->addStretch(1);
    optionsLayout->addWidget(new QLabel("Colour theme:",optionsRow));
    auto* themeCombo=new QComboBox(optionsRow);
    themeCombo->addItem("Auto",static_cast<int>(Style::StyleSheetMode::Auto));
    themeCombo->addItem("Light",static_cast<int>(Style::StyleSheetMode::Light));
    themeCombo->addItem("Dark",static_cast<int>(Style::StyleSheetMode::Dark));
    themeCombo->setCurrentIndex(themeCombo->findData(static_cast<int>(Style::instance().styleSheetMode())));
    optionsLayout->addWidget(themeCombo);
    l->addWidget(optionsRow);

    auto* permissionRow=new QFrame(mainFrame);
    auto* permissionLayout=Layout::horizontal(permissionRow);
    permissionLayout->setContentsMargins(0,0,0,0);
    auto* askBox=new QCheckBox("Ask for the microphone permission at startup",permissionRow);
    askBox->setChecked(true);
    auto* askButton=new QPushButton("Request microphone permission",permissionRow);
    demo.allTicksBox=new QCheckBox("Log every tick",permissionRow);
    auto* folderButton=new QPushButton("Open recordings folder",permissionRow);
    permissionLayout->addWidget(askBox);
    permissionLayout->addWidget(askButton);
    permissionLayout->addWidget(demo.allTicksBox);
    permissionLayout->addStretch(1);
    permissionLayout->addWidget(folderButton);
    l->addWidget(permissionRow);

    // ---- the composer
    auto* editor=new MessageEditor(mainFrame);
    editor->setMicButtonVisible(true);
    editor->setEmojiButtonVisible(true);
    demo.editor=editor;
    l->addWidget(editor);

    // ---- the recorded message and its cropped copy
    l->addWidget(new QLabel("Message (what Send made: the cropped file if the recording was trimmed):",mainFrame));
    demo.playerFinal=new AudioPlayer(mainFrame);
    demo.playerFinal->initWidget(mainFrame);
    demo.playerFinal->setTitle("nothing sent yet");
    demo.playerFinal->setTitleClickable(false);
    demo.playerFinal->setProgressStyle(WaveformBar::Style::Bars);
    demo.playerFinal->setEngine(demo.engineFinal.get());
    l->addWidget(demo.playerFinal->qWidget());
    demo.detailFinal=new QLabel(mainFrame);
    demo.detailFinal->setWordWrap(true);
    demo.detailFinal->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(demo.detailFinal);

    demo.originalBox=new QFrame(mainFrame);
    auto* croppedLayout=Layout::vertical(demo.originalBox.data());
    croppedLayout->setContentsMargins(0,0,0,0);
    croppedLayout->addWidget(new QLabel("Original recording, before the crop (the message above is one more lossy generation):",demo.originalBox));
    demo.playerOriginal=new AudioPlayer(mainFrame);
    demo.playerOriginal->initWidget(demo.originalBox);
    demo.playerOriginal->setTitleClickable(false);
    demo.playerOriginal->setProgressStyle(WaveformBar::Style::Bars);
    demo.playerOriginal->setEngine(demo.engineOriginal.get());
    croppedLayout->addWidget(demo.playerOriginal->qWidget());
    demo.detailOriginal=new QLabel(demo.originalBox);
    demo.detailOriginal->setWordWrap(true);
    demo.detailOriginal->setTextInteractionFlags(Qt::TextSelectableByMouse);
    croppedLayout->addWidget(demo.detailOriginal);
    demo.originalBox->setVisible(false);
    l->addWidget(demo.originalBox);

    // ---- log
    auto* clearButton=new QPushButton("Clear log",mainFrame);
    l->addWidget(clearButton,0,Qt::AlignRight);
    demo.logEdit=new QPlainTextEdit(mainFrame);
    demo.logEdit->setReadOnly(true);
    demo.logEdit->setMaximumBlockCount(2000);
    demo.logEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    l->addWidget(demo.logEdit,1);

    // ---- wiring
    auto* d=&demo;

    demo.connectRecorder();
    demo.connectPlayer("message",demo.engineFinal.get(),demo.playerFinal);
    demo.connectPlayer("original",demo.engineOriginal.get(),demo.playerOriginal);

    const auto refillDevices=[d,inputCombo,outputCombo]()
    {
        const auto oldInput=inputCombo->currentData().toByteArray();
        const auto oldOutput=outputCombo->currentData().toByteArray();

        fillDevices(inputCombo,QMediaDevices::audioInputs(),QMediaDevices::defaultAudioInput());
        fillDevices(outputCombo,QMediaDevices::audioOutputs(),QMediaDevices::defaultAudioOutput());

        // a device that was selected and is gone: say so, and go back to the default
        if (!oldInput.isEmpty() && inputCombo->currentData().toByteArray()!=oldInput)
        {
            d->log(QStringLiteral("the selected microphone is gone, back to the system default"));
            d->recorder->setInputDevice(QByteArray());
        }
        if (!oldOutput.isEmpty() && outputCombo->currentData().toByteArray()!=oldOutput)
        {
            d->log(QStringLiteral("the selected output is gone, back to the system default"));
            d->recorder->setOutputDevice(QByteArray());
            d->engineFinal->setOutputDevice(QByteArray());
            d->engineOriginal->setOutputDevice(QByteArray());
        }
        d->refreshCapabilities();
    };
    refillDevices();
    d->log(QStringLiteral("recordings go to %1").arg(demo.dir));

    QObject::connect(rescan,&QPushButton::clicked,&w,refillDevices);
    QObject::connect(devices,&QMediaDevices::audioInputsChanged,&w,
        [d,refillDevices]()
        {
            d->log(QStringLiteral("system: the list of microphones changed (engine state %1)").arg(enumName(d->recorder->state())));
            refillDevices();
        }
    );
    QObject::connect(devices,&QMediaDevices::audioOutputsChanged,&w,
        [d,refillDevices]()
        {
            d->log(QStringLiteral("system: the list of outputs changed"));
            refillDevices();
        }
    );
    QObject::connect(inputCombo,&QComboBox::currentIndexChanged,&w,
        [d,inputCombo]()
        {
            const auto id=inputCombo->currentData().toByteArray();
            d->recorder->setInputDevice(id);
            d->log(QStringLiteral("microphone: %1 (takes effect at the next start or resume)").arg(inputCombo->currentText()));
        }
    );
    QObject::connect(outputCombo,&QComboBox::currentIndexChanged,&w,
        [d,outputCombo]()
        {
            const auto id=outputCombo->currentData().toByteArray();
            d->recorder->setOutputDevice(id);
            d->engineFinal->setOutputDevice(id);
            d->engineOriginal->setOutputDevice(id);
            d->log(QStringLiteral("output: %1").arg(outputCombo->currentText()));
        }
    );

    const auto askPermission=[d]()
    {
        d->log(QStringLiteral("permission: requesting, allowed now: %1")
                   .arg(VoiceRecorderEngine::isPermissionGranted() ? QStringLiteral("yes") : QStringLiteral("no")));
        d->recorder->requestPermission(
            [d](bool granted)
            {
                d->log(QStringLiteral("permission: %1").arg(granted ? QStringLiteral("GRANTED") : QStringLiteral("DENIED")));
                d->refreshCapabilities();
            }
        );
    };
    QObject::connect(askButton,&QPushButton::clicked,&w,askPermission);

    QObject::connect(demo.cryptBox.data(),&QCheckBox::toggled,&w,
        [d](bool on)
        {
            if (on && !d->ensureCrypt())
            {
                const QSignalBlocker blocker(d->cryptBox.data());
                d->cryptBox->setChecked(false);
                d->cryptBox->setEnabled(false);
                d->cryptBox->setToolTip(QString::fromStdString(d->crypt.failure));
            }
        }
    );
    QObject::connect(enabledBox,&QCheckBox::toggled,editor,&AbstractMessageEditor::setVoiceMessageEnabled);
    QObject::connect(themeCombo,&QComboBox::currentIndexChanged,&w,
        [d,themeCombo,applyTheme]()
        {
            const auto mode=static_cast<Style::StyleSheetMode>(themeCombo->currentData().toInt());
            applyTheme(mode);
            d->log(QStringLiteral("theme: %1 (dark: %2)").arg(themeCombo->currentText()).arg(Style::instance().isDarkTheme() ? QStringLiteral("yes") : QStringLiteral("no")));
        }
    );
    QObject::connect(clearButton,&QPushButton::clicked,demo.logEdit.data(),&QPlainTextEdit::clear);
    QObject::connect(folderButton,&QPushButton::clicked,&w,
        [d]()
        {
            QDir().mkpath(d->dir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(d->dir));
        }
    );

    QObject::connect(editor,&AbstractMessageEditor::voiceRecorderOpened,&w,
        [d](AbstractVoiceRecorderDialog* dialog)
        {
            d->voiceRecorderOpened(dialog);
        }
    );
    // The popup is reused, so the host has to know which one it was to disconnect from it.
    QObject::connect(editor,&AbstractMessageEditor::voiceRecorderClosed,&w,
        [d,editor]()
        {
            d->voiceRecorderClosed(QPointer<AbstractVoiceRecorderDialog>(editor->voiceRecorder()));
        }
    );

    // Where the Qt::Tool popup goes when the application is deactivated: it is only reported.
    QObject::connect(&app,&QGuiApplication::applicationStateChanged,&w,
        [d](Qt::ApplicationState state)
        {
            const auto recording=d->recorder->state();
            d->log(QStringLiteral("application state -> %1 (engine %2)").arg(enumName(state)).arg(enumName(recording)));
            if (state!=Qt::ApplicationActive
                && (recording==VoiceRecorderEngine::State::Recording || recording==VoiceRecorderEngine::State::Paused
                    || recording==VoiceRecorderEngine::State::Listening))
            {
                d->log(QStringLiteral("  the application is not active while a recording is open: the Qt::Tool popup may be hidden now, the recording goes on"));
            }
        }
    );

    w.setCentralWidget(mainFrame);
    w.resize(820,900);
    w.setWindowTitle("Voice Message Demo");
    w.show();

    QTimer::singleShot(0,&w,
        [d,askBox,askPermission]()
        {
            d->refreshCapabilities();
            if (askBox->isChecked() && !VoiceRecorderEngine::isPermissionGranted())
            {
                askPermission();
            }
        }
    );

    const auto rc=app.exec();

    demo.shutdown();
    return rc;
}

#else

int main(int argc, char *argv[])
{
    QApplication app(argc,argv);

    Style::instance().applyStyleSheet();

    QMainWindow w;
    auto* label=new QLabel("hatnuise was built without the voice glue (HATN_UISE_VOICE): it needs hatn media and Qt Multimedia.");
    label->setAlignment(Qt::AlignCenter);
    w.setCentralWidget(label);
    w.resize(620,120);
    w.setWindowTitle("Voice Message Demo");
    w.show();
    return app.exec();
}

#endif
