/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/voicerecorderengine.cpp
  *
  */

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCoreApplication>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QPermissions>
#include <QPointer>
#include <QTimer>

#include <hatn/common/error.h>
#include <hatn/common/file.h>
#include <hatn/common/thread.h>

#include <hatn/media/media.h>
#include <hatn/media/mediaerror.h>
#include <hatn/media/audioformat.h>
#include <hatn/media/pcmring.h>
#include <hatn/media/voicecrop.h>
#include <hatn/media/voicerecorder.h>
#include <hatn/media/waveformextractor.h>

#include <hatnuise/voiceplaybackengine.h>
#include <hatnuise/voicerecorderengine.h>

HATN_UISE_NAMESPACE_BEGIN

namespace common=hatn::common;
namespace media=hatn::media;

//---------------------------------------------------------------
// VoiceRecordingResult
//---------------------------------------------------------------

namespace {

//! The fraction of a message as milliseconds of it, rounded.
qint64 fractionToMs(qreal fraction, qint64 durationMs) noexcept
{
    return static_cast<qint64>(std::llround(std::clamp<qreal>(fraction,0.0,1.0)*static_cast<qreal>(durationMs)));
}

}

//---------------------------------------------------------------

bool isCropped(const VoiceRecordingResult& result) noexcept
{
    // in milliseconds, so that a range that rounds to the whole message is not a crop
    const auto durationMs=static_cast<qint64>(result.recording.durationMs);
    return fractionToMs(result.cropStart,durationMs)>0 || fractionToMs(result.cropEnd,durationMs)<durationMs;
}

//---------------------------------------------------------------

hatn::common::Error cropRecording(
        const VoiceRecordingResult& result,
        hatn::common::File& in,
        hatn::common::File& out,
        hatn::media::VoiceRecording& cropped
    )
{
    const auto durationMs=static_cast<qint64>(result.recording.durationMs);
    const auto startMs=fractionToMs(result.cropStart,durationMs);

    // The end of the message is not the duration in whole milliseconds, it can be a fraction of one
    // longer; cropVoice() clamps to the real end, so the untrimmed end is passed as "far".
    const auto endFrame=result.cropEnd>=1.0
                            ? std::numeric_limits<uint64_t>::max()
                            : media::voiceMsToFrames(static_cast<uint64_t>(fractionToMs(result.cropEnd,durationMs)));

    return media::cropVoice(in,out,media::voiceMsToFrames(static_cast<uint64_t>(startMs)),endFrame,cropped);
}

//---------------------------------------------------------------
// The engine
//---------------------------------------------------------------

namespace {

using State=VoiceRecorderEngine::State;
using Dialog=UISE_DESKTOP_NAMESPACE::AbstractVoiceRecorderDialog;

//! How often the worker calls VoiceRecorder::process().
constexpr const int WorkerTickMs=20;

//! How often the GUI thread reads the length and looks for the length limit.
constexpr const int GuiTickMs=100;

//! Room for the waveform: it is fed from the worker, which is at most a few ticks late.
constexpr const size_t WaveformRingFrames=static_cast<size_t>(media::VoiceSampleRate)*2;

constexpr const size_t ScratchFrames=2048;

QString errorText(const common::Error& ec)
{
    return QString::fromStdString(ec.message());
}

/**
 * The QIODevice a QAudioSource pushes the microphone into. It does no more than
 * VoiceRecorder::pushPcm(), which is wait-free, and a wait-free write into the ring that feeds the
 * live waveform, so it does not matter which thread the Qt audio backend writes from; there must only
 * be one at a time. It is the mirror image of the PullDevice of VoicePlaybackEngine.
 *
 * Only what the recorder accepted goes into the waveform: while paused, or past the length limit,
 * the recorder takes nothing and the waveform stays what the message is.
 */
class CaptureDevice : public QIODevice
{
    public:

        CaptureDevice(media::VoiceRecorder& recorder, media::PcmRing& waveformRing)
            : m_recorder(recorder),
              m_waveformRing(waveformRing)
        {
        }

        bool isSequential() const override
        {
            return true;
        }

    protected:

        qint64 readData(char*, qint64) override
        {
            return -1;
        }

        qint64 writeData(const char* data, qint64 size) override
        {
            // Everything is taken, whatever the recorder makes of it: a backend that is told that
            // fewer bytes were written may stop the input.
            qint64 pos=0;

            // a sample that a previous buffer ended in the middle of
            if (m_carried && size>0)
            {
                const char pair[2]={m_carry,data[0]};
                int16_t sample=0;
                std::memcpy(&sample,pair,sizeof(sample));
                push(&sample,1);
                m_carried=false;
                pos=1;
            }

            // The buffer of the sink is only bytes: go through an aligned scratch.
            while (size-pos>=static_cast<qint64>(sizeof(int16_t)))
            {
                const auto frames=std::min<size_t>(ScratchFrames,static_cast<size_t>(size-pos)/sizeof(int16_t));
                std::memcpy(m_scratch.data(),data+pos,frames*sizeof(int16_t));
                push(m_scratch.data(),frames);
                pos+=static_cast<qint64>(frames*sizeof(int16_t));
            }

            if (pos<size)
            {
                m_carry=data[pos];
                m_carried=true;
            }
            return size;
        }

    private:

        void push(const int16_t* samples, size_t frames) noexcept
        {
            const auto accepted=m_recorder.pushPcm(samples,frames);
            if (accepted!=0)
            {
                m_waveformRing.write(samples,accepted);
            }
        }

        media::VoiceRecorder& m_recorder;
        media::PcmRing& m_waveformRing;
        std::array<int16_t,ScratchFrames> m_scratch;
        char m_carry=0;
        bool m_carried=false;
};

}

//---------------------------------------------------------------

class VoiceRecorderEngine_p
{
    public:

        VoiceRecorderEngine_p(VoiceRecorderEngine* engine, std::shared_ptr<common::Thread> workerThread);

        VoiceRecorderEngine* self;

        std::shared_ptr<common::Thread> thread;
        bool ownsThread=false;
        uint32_t timerId=0;
        bool timerInstalled=false;
        std::atomic<bool> workerFailed{false};

        //! Bumped whenever the recording changes hands, so that a failure that was queued for the GUI
        //! thread by the worker or by the microphone is not blamed on what came after it.
        int generation=0;

        media::VoiceRecorderConfig config;
        QByteArray inputId;
        QByteArray outputId;

        // Members that reach one another are declared so that they are destroyed in the reverse
        // order: the pre-listen, the microphone, the device, the ring, the recorder and last the file.
        std::shared_ptr<common::File> file;
        QString path;
        std::string pathUtf8;
        VoiceFileFactory readFactory;
        std::unique_ptr<media::VoiceRecorder> recorder;
        std::unique_ptr<media::PcmRing> waveformRing;
        media::WaveformExtractor waveform;
        std::array<int16_t,ScratchFrames> scratch;
        std::unique_ptr<CaptureDevice> device;
        std::unique_ptr<QAudioSource> source;
        int captureEpoch=0;

        std::unique_ptr<VoicePlaybackEngine> preListen;
        bool preListenLoaded=false;

        QTimer guiTimer;
        qint64 lastElapsed=-1;

        State state=State::Idle;

        QPointer<Dialog> dialog;
        std::vector<QMetaObject::Connection> dialogConnections;

        //! What the dialog was last told the length of the message is: the recorder's length, or in
        //! Listening the length of what can be played (a little shorter, the writer holds the last packet
        //! back). The fraction of a seek is a fraction of THIS.
        qint64 dialogTotalMs=0;

        void setState(State newState);

        //! Tell the client of a failure that is not a change of state.
        void report(const QString& message);

        //! The recording cannot go on: everything is stopped, the file closed, the state Failed.
        void enterFailed(const QString& message);

        //! Move the dialog to `newState` unless it is there already.
        void syncDialogState(Dialog::State newState);

        common::Error closeFile();

        //! For the ways out that are already failing or ending: nothing more can be done about a close error.
        void closeFileQuietly();

        void startWorker();
        void stopWorker();
        void onWorkerTick();
        void onWorkerFailed(int messageGeneration, const QString& message);
        void drainWaveformRing();

        QAudioDevice findInputDevice() const;
        bool startCapture(QString& error);
        void stopCapture();
        void onSourceState(QtAudio::State newState);
        void onCaptureFailed(int epoch, int error);

        //! Recording -> Paused, all of it, see the plan of it in the body. False if it failed.
        bool doPause();
        bool doResume(QString& error);
        void pushPausedToDialog();

        void onGuiTick();

        //! Make the pre-listen engine and load the file into it if that has not been done. On false
        //! `error` is the reason, or empty when the pre-listen engine has reported it already.
        bool ensurePreListen(QString& error);
        void stopPreListen();
        void onPreListenState(VoicePlaybackEngine::State newState);
        void seekFraction(qreal fraction);

        void shutdown();
};

//---------------------------------------------------------------

VoiceRecorderEngine_p::VoiceRecorderEngine_p(VoiceRecorderEngine* engine, std::shared_ptr<common::Thread> workerThread)
    : self(engine),
      thread(std::move(workerThread))
{
    if (!thread)
    {
        thread=std::make_shared<common::Thread>("voicerecorder");
        thread->start();
        ownsThread=true;
    }

    guiTimer.setInterval(GuiTickMs);
    QObject::connect(&guiTimer,&QTimer::timeout,self,[this]()
    {
        onGuiTick();
    });
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::setState(State newState)
{
    if (state==newState)
    {
        return;
    }
    state=newState;
    emit self->stateChanged(newState);
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::report(const QString& message)
{
    emit self->errorOccurred(message);
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::syncDialogState(Dialog::State newState)
{
    if (dialog && dialog->state()!=newState)
    {
        dialog->setState(newState);
    }
}

//---------------------------------------------------------------

common::Error VoiceRecorderEngine_p::closeFile()
{
    common::Error ec;
    if (file && file->isOpen())
    {
        file->close(ec);
    }
    return ec;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::closeFileQuietly()
{
    static_cast<void>(closeFile());
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::enterFailed(const QString& message)
{
    guiTimer.stop();
    stopPreListen();
    stopCapture();
    stopWorker();
    if (recorder)
    {
        // a no-op if the recorder has failed or finished by itself, and it touches no file
        recorder->cancel();
    }
    closeFileQuietly();

    ++generation;
    setState(State::Failed);
    report(message);
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::startWorker()
{
    if (timerInstalled)
    {
        return;
    }

    workerFailed.store(false,std::memory_order_release);
    timerId=thread->installTimer(
        static_cast<uint64_t>(WorkerTickMs)*1000,
        [this]()
        {
            onWorkerTick();
            return true;
        }
    );
    timerInstalled=true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::stopWorker()
{
    if (!timerInstalled)
    {
        return;
    }

    // Waits for a running handler to return, so never from the handler itself. Afterwards nothing
    // of the worker touches the recorder, the ring, the extractor or the file.
    thread->uninstallTimer(timerId,true);
    timerInstalled=false;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::drainWaveformRing()
{
    // the consumer of the ring: the worker while its timer runs, the GUI thread once it is removed
    size_t frames=0;
    while ((frames=waveformRing->read(scratch.data(),scratch.size()))!=0)
    {
        waveform.add(scratch.data(),frames);
    }
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onWorkerTick()
{
    // worker thread
    if (workerFailed.load(std::memory_order_acquire))
    {
        return;
    }

    drainWaveformRing();

    auto ec=recorder->process();
    if (!ec)
    {
        return;
    }

    // The timer keeps ticking until the GUI thread removes it, and does nothing.
    workerFailed.store(true,std::memory_order_release);

    const auto messageGeneration=generation;
    const auto message=errorText(ec);
    QMetaObject::invokeMethod(self,[this,messageGeneration,message]()
    {
        onWorkerFailed(messageGeneration,message);
    },Qt::QueuedConnection);
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onWorkerFailed(int messageGeneration, const QString& message)
{
    if (messageGeneration!=generation || state!=State::Recording)
    {
        return;
    }
    enterFailed(message);
}

//---------------------------------------------------------------

QAudioDevice VoiceRecorderEngine_p::findInputDevice() const
{
    if (!inputId.isEmpty())
    {
        const auto inputs=QMediaDevices::audioInputs();
        for (const auto& input : inputs)
        {
            if (input.id()==inputId)
            {
                return input;
            }
        }
    }
    return QMediaDevices::defaultAudioInput();
}

//---------------------------------------------------------------

bool VoiceRecorderEngine_p::startCapture(QString& error)
{
    const auto input=findInputDevice();
    if (input.isNull())
    {
        error=QStringLiteral("there is no microphone");
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(media::VoiceSampleRate));
    format.setChannelCount(static_cast<int>(media::VoiceChannels));
    format.setSampleFormat(QAudioFormat::Int16);
    if (!input.isFormatSupported(format))
    {
        error=QStringLiteral("the microphone does not support mono 48 kHz 16-bit sound");
        return false;
    }

    device=std::make_unique<CaptureDevice>(*recorder,*waveformRing);
    device->open(QIODevice::WriteOnly|QIODevice::Unbuffered);

    // A new source at every start: a pause closes the microphone, so that the system's recording
    // indicator goes out, and a failure of one run does not stay with the next.
    source=std::make_unique<QAudioSource>(input,format);
    QObject::connect(source.get(),&QAudioSource::stateChanged,self,[this](QtAudio::State newState)
    {
        onSourceState(newState);
    });
    source->start(device.get());

    if (source->state()==QtAudio::StoppedState && source->error()!=QtAudio::NoError)
    {
        error=QStringLiteral("the microphone would not start (error %1)").arg(static_cast<int>(source->error()));
        stopCapture();
        return false;
    }
    return true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::stopCapture()
{
    ++captureEpoch;
    if (source)
    {
        // what the source says while it is being stopped is not a failure
        QObject::disconnect(source.get(),nullptr,self,nullptr);

        // If the backend has a thread of its own it is joined here, and only after that may
        // the device go.
        source->stop();
        source.reset();
    }
    if (device)
    {
        device->close();
        device.reset();
    }
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onSourceState(QtAudio::State newState)
{
    // Stopped by us leaves no error. Stopped with one is the microphone going away under us.
    if (newState!=QtAudio::StoppedState || state!=State::Recording || !source || source->error()==QtAudio::NoError)
    {
        return;
    }

    // not from inside the signal of the source, which is destroyed in reacting to it
    const auto epoch=captureEpoch;
    const auto error=static_cast<int>(source->error());
    QMetaObject::invokeMethod(self,[this,epoch,error]()
    {
        onCaptureFailed(epoch,error);
    },Qt::QueuedConnection);
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onCaptureFailed(int epoch, int error)
{
    if (epoch!=captureEpoch || state!=State::Recording)
    {
        return;
    }

    // What was recorded is fine: the recording goes to Paused and can be finished, or resumed
    // when the microphone is back.
    if (doPause())
    {
        syncDialogState(Dialog::State::Paused);
        report(QStringLiteral("the microphone stopped (error %1)").arg(error));
    }
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::pushPausedToDialog()
{
    const auto elapsed=static_cast<qint64>(recorder->elapsedMs());
    const auto buckets=waveform.buckets();
    const QByteArray bytes(reinterpret_cast<const char*>(buckets.data()),static_cast<qsizetype>(buckets.size()));

    lastElapsed=elapsed;
    dialogTotalMs=elapsed;
    emit self->elapsedMsChanged(elapsed);
    emit self->waveformChanged(bytes);

    if (dialog)
    {
        dialog->setElapsedMs(elapsed);
        dialog->setPlaybackMs(0);
        dialog->setWaveform(bytes);
        dialog->setCropRange(0.0,1.0);
    }
}

//---------------------------------------------------------------

bool VoiceRecorderEngine_p::doPause()
{
    if (state!=State::Recording)
    {
        return false;
    }

    // The microphone first, so that nothing more comes in. Then the recorder, which takes what was
    // already accepted and puts it on disk THROUGH THE OPEN FILE (VoiceRecorder::pause() flushes).
    stopCapture();
    auto ec=recorder->pause();

    // The worker must be gone before the file is closed: process() also runs on a Paused recorder.
    stopWorker();
    drainWaveformRing();
    if (ec)
    {
        enterFailed(errorText(ec));
        return false;
    }

    // Nothing writes to the file any more, and nothing reads it: the pre-listen opens a handle of its own.
    ec=closeFile();
    if (ec)
    {
        enterFailed(errorText(ec));
        return false;
    }

    guiTimer.stop();
    ++generation;
    setState(State::Paused);
    pushPausedToDialog();
    return true;
}

//---------------------------------------------------------------

bool VoiceRecorderEngine_p::doResume(QString& error)
{
    if (state!=State::Paused && state!=State::Listening)
    {
        error=QStringLiteral("only a paused recording can be resumed");
        return false;
    }

    // never a writer and a reader of one file at once
    stopPreListen();
    setState(State::Paused);

    if (recorder->limitReached())
    {
        error=QStringLiteral("the recording has reached its maximum length");
        return false;
    }

    // The same object, opened again to append. Where it lands is not read back from CryptFile::pos(),
    // which says 0 after an append open; the recording that comes out is the proof.
    auto ec=file->open(pathUtf8,common::File::Mode::append_existing);
    if (ec)
    {
        error=errorText(ec);
        return false;
    }

    ec=recorder->resume();
    if (ec)
    {
        closeFileQuietly();
        error=errorText(ec);
        return false;
    }

    ++generation;
    setState(State::Recording);
    startWorker();

    if (!startCapture(error))
    {
        // back to Paused, with the file closed again
        doPause();
        return false;
    }

    lastElapsed=-1;
    guiTimer.start();
    return true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onGuiTick()
{
    if (state!=State::Recording)
    {
        guiTimer.stop();
        return;
    }

    const auto elapsed=static_cast<qint64>(recorder->elapsedMs());
    if (elapsed!=lastElapsed)
    {
        lastElapsed=elapsed;
        dialogTotalMs=elapsed;
        emit self->elapsedMsChanged(elapsed);
        if (dialog)
        {
            dialog->setElapsedMs(elapsed);
        }
    }

    if (recorder->limitReached())
    {
        // out of room: what is recorded is what the message is
        if (doPause())
        {
            syncDialogState(Dialog::State::Paused);
            emit self->limitReached();
        }
    }
}

//---------------------------------------------------------------

bool VoiceRecorderEngine_p::ensurePreListen(QString& error)
{
    if (!preListen)
    {
        if (!readFactory)
        {
            error=QStringLiteral("this recording cannot be listened to: no read handle can be made");
            return false;
        }

        preListen=std::make_unique<VoicePlaybackEngine>(thread);
        preListen->setOutputDevice(outputId);
        preListenLoaded=false;

        QObject::connect(preListen.get(),&VoicePlaybackEngine::positionChanged,self,[this](qint64 ms)
        {
            emit self->preListenPositionChanged(ms);
            if (dialog)
            {
                dialog->setPlaybackMs(ms);
            }
        });
        QObject::connect(preListen.get(),&VoicePlaybackEngine::stateChanged,self,[this](VoicePlaybackEngine::State newState)
        {
            onPreListenState(newState);
        });
        QObject::connect(preListen.get(),&VoicePlaybackEngine::errorOccurred,self,[this](const QString& message)
        {
            report(message);
        });
    }

    if (!preListenLoaded)
    {
        auto handle=readFactory();
        if (!handle)
        {
            error=QStringLiteral("no read handle could be made for the recording");
            return false;
        }
        auto ec=handle->open(pathUtf8,common::File::Mode::read);
        if (ec)
        {
            error=errorText(ec);
            return false;
        }

        // takes the handle over and closes it; a failure comes out of its errorOccurred()
        preListen->openFile(std::move(handle));
        if (preListen->durationMs()<=0)
        {
            error.clear();
            return false;
        }
        preListenLoaded=true;
    }
    return true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::stopPreListen()
{
    // Destroying the engine closes its handle: the recording is about to be written again, or be gone.
    preListen.reset();
    preListenLoaded=false;
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::onPreListenState(VoicePlaybackEngine::State newState)
{
    // Playing and Paused are our own doing. Stopped is the end of the message, or a failure.
    if (newState!=VoicePlaybackEngine::State::Stopped || state!=State::Listening)
    {
        return;
    }

    setState(State::Paused);
    syncDialogState(Dialog::State::Paused);
    emit self->preListenEnded();
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::seekFraction(qreal fraction)
{
    if (state!=State::Paused && state!=State::Listening)
    {
        return;
    }

    QString error;
    if (!ensurePreListen(error))
    {
        if (!error.isEmpty())
        {
            report(error);
        }
        return;
    }
    preListen->seekMs(fractionToMs(fraction,dialogTotalMs));
}

//---------------------------------------------------------------

void VoiceRecorderEngine_p::shutdown()
{
    // no callback may run into a half-destroyed engine: the timer goes first, then the microphone
    dialog.clear();
    for (auto& connection : dialogConnections)
    {
        QObject::disconnect(connection);
    }
    dialogConnections.clear();

    guiTimer.stop();
    stopPreListen();
    stopCapture();
    stopWorker();

    if (recorder && (state==State::Recording || state==State::Paused || state==State::Listening))
    {
        recorder->cancel();
    }
    closeFileQuietly();

    if (ownsThread)
    {
        thread->stop();
    }
}

//---------------------------------------------------------------

VoiceRecorderEngine::VoiceRecorderEngine(
        std::shared_ptr<hatn::common::Thread> thread,
        QObject* parent
    ) : QObject(parent),
        pimpl(std::make_unique<VoiceRecorderEngine_p>(this,std::move(thread)))
{
    qRegisterMetaType<hatnuise::VoiceRecordingResult>();
}

//---------------------------------------------------------------

VoiceRecorderEngine::~VoiceRecorderEngine()
{
    pimpl->shutdown();
}

//---------------------------------------------------------------

bool VoiceRecorderEngine::isAvailable() noexcept
{
    return media::isOggOpusAvailable();
}

//---------------------------------------------------------------

bool VoiceRecorderEngine::hasInputDevice() noexcept
{
    return !QMediaDevices::defaultAudioInput().isNull();
}

//---------------------------------------------------------------

bool VoiceRecorderEngine::isPermissionGranted()
{
    auto* app=QCoreApplication::instance();
    if (app==nullptr)
    {
        return false;
    }
    return app->checkPermission(QMicrophonePermission{})==Qt::PermissionStatus::Granted;
}

//---------------------------------------------------------------

void VoiceRecorderEngine::requestPermission(std::function<void(bool)> done)
{
    auto* app=QCoreApplication::instance();
    if (app==nullptr)
    {
        if (done)
        {
            done(false);
        }
        return;
    }

    QMicrophonePermission permission;
    switch (app->checkPermission(permission))
    {
        case Qt::PermissionStatus::Granted:
            if (done)
            {
                done(true);
            }
            return;

        case Qt::PermissionStatus::Denied:
            // asking again does nothing: only the system settings can change it
            if (done)
            {
                done(false);
            }
            return;

        case Qt::PermissionStatus::Undetermined:
            break;
    }

    app->requestPermission(permission,this,[done](const QPermission& result)
    {
        if (done)
        {
            done(result.status()==Qt::PermissionStatus::Granted);
        }
    });
}

//---------------------------------------------------------------

void VoiceRecorderEngine::setInputDevice(const QByteArray& id)
{
    pimpl->inputId=id;
}

//---------------------------------------------------------------

QByteArray VoiceRecorderEngine::inputDevice() const
{
    return pimpl->inputId;
}

//---------------------------------------------------------------

void VoiceRecorderEngine::setOutputDevice(const QByteArray& id)
{
    pimpl->outputId=id;
    if (pimpl->preListen)
    {
        pimpl->preListen->setOutputDevice(id);
    }
}

//---------------------------------------------------------------

QByteArray VoiceRecorderEngine::outputDevice() const
{
    return pimpl->outputId;
}

//---------------------------------------------------------------

void VoiceRecorderEngine::setConfig(const hatn::media::VoiceRecorderConfig& config)
{
    pimpl->config=config;
}

//---------------------------------------------------------------

bool VoiceRecorderEngine::start(
        std::shared_ptr<hatn::common::File> file,
        const QString& path,
        VoiceFileFactory readFactory
    )
{
    auto& p=*pimpl;

    if (p.state==State::Recording || p.state==State::Paused || p.state==State::Listening)
    {
        p.report(QStringLiteral("a recording is already in progress"));
        return false;
    }

    // a new message: nothing of the last one may be running
    p.guiTimer.stop();
    p.stopPreListen();
    p.stopCapture();
    p.stopWorker();

    // From here on the file is the engine's, so that every way out closes it.
    p.file=std::move(file);
    p.path=path;
    p.pathUtf8=path.toUtf8().constData();
    p.readFactory=std::move(readFactory);
    p.recorder.reset();
    p.waveformRing.reset();
    p.waveform.reset();
    p.lastElapsed=-1;

    if (!isAvailable())
    {
        p.enterFailed(errorText(media::mediaError(media::MediaError::CODEC_UNAVAILABLE)));
        return false;
    }
    if (!p.file || !p.file->isOpen())
    {
        p.enterFailed(QStringLiteral("the file to record into is not open"));
        return false;
    }
    if (!isPermissionGranted())
    {
        p.enterFailed(QStringLiteral("the application is not allowed to use the microphone"));
        return false;
    }

    // One recorder records one message.
    p.recorder=std::make_unique<media::VoiceRecorder>(p.config);
    p.waveformRing=std::make_unique<media::PcmRing>(WaveformRingFrames);

    auto ec=p.recorder->start(*p.file);
    if (ec)
    {
        p.enterFailed(errorText(ec));
        return false;
    }

    ++p.generation;
    p.setState(State::Recording);
    p.startWorker();

    QString error;
    if (!p.startCapture(error))
    {
        p.enterFailed(error);
        return false;
    }

    p.guiTimer.start();
    return true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine::pause()
{
    if (pimpl->doPause())
    {
        pimpl->syncDialogState(Dialog::State::Paused);
    }
}

//---------------------------------------------------------------

void VoiceRecorderEngine::resume()
{
    auto& p=*pimpl;
    if (p.state!=State::Paused && p.state!=State::Listening)
    {
        return;
    }

    QString error;
    if (p.doResume(error))
    {
        p.syncDialogState(Dialog::State::Pinned);
        return;
    }

    // still Paused, or Failed by a failure of the file; the dialog must not sit in Pinned
    if (p.state==State::Paused)
    {
        p.syncDialogState(Dialog::State::Paused);
    }
    p.report(error);
}

//---------------------------------------------------------------

void VoiceRecorderEngine::startPreListen()
{
    auto& p=*pimpl;
    if (p.state==State::Listening)
    {
        return;
    }
    if (p.state!=State::Paused)
    {
        p.report(QStringLiteral("only a paused recording can be listened to"));
        return;
    }

    QString error;
    if (!p.ensurePreListen(error))
    {
        p.syncDialogState(Dialog::State::Paused);
        if (!error.isEmpty())
        {
            p.report(error);
        }
        return;
    }

    p.preListen->play();
    if (p.preListen->state()!=VoicePlaybackEngine::State::Playing)
    {
        // it has told why through errorOccurred()
        p.syncDialogState(Dialog::State::Paused);
        return;
    }

    p.setState(State::Listening);
    p.syncDialogState(Dialog::State::Listening);

    // The dialog draws the progress as position over length, and what can be played is a little
    // shorter than what was recorded: give it that length, or the bar never reaches the end.
    p.dialogTotalMs=p.preListen->durationMs();
    if (p.dialog)
    {
        p.dialog->setElapsedMs(p.dialogTotalMs);
    }
}

//---------------------------------------------------------------

void VoiceRecorderEngine::pausePreListen()
{
    auto& p=*pimpl;
    if (p.state!=State::Listening)
    {
        return;
    }

    p.preListen->pause();
    p.setState(State::Paused);
    p.syncDialogState(Dialog::State::Paused);
}

//---------------------------------------------------------------

void VoiceRecorderEngine::seekPreListenMs(qint64 ms)
{
    auto& p=*pimpl;
    if (p.state!=State::Paused && p.state!=State::Listening)
    {
        return;
    }

    QString error;
    if (!p.ensurePreListen(error))
    {
        if (!error.isEmpty())
        {
            p.report(error);
        }
        return;
    }
    p.preListen->seekMs(ms);
}

//---------------------------------------------------------------

bool VoiceRecorderEngine::finish(const QString& comment, qreal cropStart, qreal cropEnd)
{
    auto& p=*pimpl;
    if (p.state!=State::Recording && p.state!=State::Paused && p.state!=State::Listening)
    {
        p.report(QStringLiteral("there is no recording to finish"));
        return false;
    }

    // VoiceRecorder::finish() takes what is queued and closes the stream itself: nothing else may work on it.
    p.guiTimer.stop();
    p.stopPreListen();
    p.stopCapture();
    p.stopWorker();
    ++p.generation;

    // A paused recording has its file closed, and the end of the stream is written into it.
    if (!p.file->isOpen())
    {
        auto ec=p.file->open(p.pathUtf8,common::File::Mode::append_existing);
        if (ec)
        {
            p.enterFailed(errorText(ec));
            return false;
        }
    }

    media::VoiceRecording recording;
    auto ec=p.recorder->finish(recording);
    if (ec)
    {
        if (ec.is(media::MediaError::RECORDING_TOO_SHORT,media::MediaErrorCategory::getCategory()))
        {
            // the recorder is Cancelled, the file holds an incomplete stream
            p.closeFileQuietly();
            p.setState(State::Cancelled);
            emit recordingTooShort();
            return false;
        }
        p.enterFailed(errorText(ec));
        return false;
    }

    // closed before the caller gets it, so that it can open the file at once for a crop or an upload
    ec=p.closeFile();
    if (ec)
    {
        p.enterFailed(errorText(ec));
        return false;
    }

    VoiceRecordingResult result;
    result.path=p.path;
    result.recording=std::move(recording);
    result.cropStart=std::clamp<qreal>(cropStart,0.0,1.0);
    result.cropEnd=std::clamp<qreal>(cropEnd,0.0,1.0);
    result.comment=comment;

    p.setState(State::Finished);
    emit recordingFinished(result);
    return true;
}

//---------------------------------------------------------------

void VoiceRecorderEngine::cancel()
{
    auto& p=*pimpl;
    if (p.state!=State::Recording && p.state!=State::Paused && p.state!=State::Listening)
    {
        return;
    }

    p.guiTimer.stop();
    p.stopPreListen();
    p.stopCapture();
    p.stopWorker();

    // abandoning touches no file, so it works on a paused recording whose file is closed
    p.recorder->cancel();
    p.closeFileQuietly();

    ++p.generation;
    p.setState(State::Cancelled);
    emit recordingCancelled();
}

//---------------------------------------------------------------

VoiceRecorderEngine::State VoiceRecorderEngine::state() const
{
    return pimpl->state;
}

//---------------------------------------------------------------

qint64 VoiceRecorderEngine::elapsedMs() const
{
    if (!pimpl->recorder)
    {
        return 0;
    }
    return static_cast<qint64>(pimpl->recorder->elapsedMs());
}

//---------------------------------------------------------------

quint64 VoiceRecorderEngine::overruns() const
{
    if (!pimpl->recorder)
    {
        return 0;
    }
    return static_cast<quint64>(pimpl->recorder->overruns());
}

//---------------------------------------------------------------

void VoiceRecorderEngine::attachDialog(UISE_DESKTOP_NAMESPACE::AbstractVoiceRecorderDialog* dialog)
{
    detachDialog();
    if (dialog==nullptr)
    {
        return;
    }

    auto& p=*pimpl;
    p.dialog=dialog;

    // The dialog has moved itself to the next state before it emits, so these only follow. There is no
    // connection for pinned(), the recording runs since start(), and none for cropChanged(), the range
    // arrives with sendRequested().
    auto& connections=p.dialogConnections;
    connections.push_back(QObject::connect(dialog,&Dialog::pauseRequested,this,[this]()
    {
        pause();
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::resumeRequested,this,[this]()
    {
        resume();
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::cancelRequested,this,[this]()
    {
        cancel();
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::sendRequested,this,[this](const QString& comment, qreal cropStart, qreal cropEnd)
    {
        finish(comment,cropStart,cropEnd);
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::listenRequested,this,[this]()
    {
        startPreListen();
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::listenPauseRequested,this,[this]()
    {
        pausePreListen();
    }));
    connections.push_back(QObject::connect(dialog,&Dialog::seekRequested,this,[this](qreal fraction)
    {
        pimpl->seekFraction(fraction);
    }));
}

//---------------------------------------------------------------

void VoiceRecorderEngine::detachDialog()
{
    auto& p=*pimpl;
    for (auto& connection : p.dialogConnections)
    {
        QObject::disconnect(connection);
    }
    p.dialogConnections.clear();
    p.dialog.clear();
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END
