/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/voiceplaybackengine.сpp
  *
  */

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

#include <hatn/common/error.h>
#include <hatn/common/plainfile.h>
#include <hatn/common/thread.h>

#include <hatn/media/media.h>
#include <hatn/media/audioformat.h>
#include <hatn/media/voiceplayer.h>

#include <hatnuise/voiceplaybackengine.h>

HATN_UISE_NAMESPACE_BEGIN

namespace common=hatn::common;
namespace media=hatn::media;

namespace {

using State=UISE_DESKTOP_NAMESPACE::AudioPlaybackEngine::State;

//! How often the decode thread looks whether VoicePlayer::fill() has work to do.
constexpr const int DecodeTickMs=20;

//! How often the GUI thread reads the position and looks for the end of the message.
constexpr const int GuiTickMs=50;

//! Audio that the sink keeps ahead of the speaker. It is also how much of the tail is still to
//! be heard when VoicePlayer reports Ended, so it sets how long the end of a message is waited for.
constexpr const int SinkBufferMs=100;
constexpr const int SinkBufferBytes=static_cast<int>(media::VoiceSampleRate*sizeof(int16_t)*SinkBufferMs/1000);

//! The end of a message is reported this long after the buffered tail should have played out.
constexpr const int DrainExtraMs=30;

constexpr const size_t ScratchFrames=2048;

/**
 * The QIODevice a QAudioSink pulls from. It does no more than VoicePlayer::pull(), which is
 * wait-free, so it does not matter which thread the Qt audio backend reads from (on macOS it is
 * the GUI thread, from a timer inside the sink); there must only be one at a time.
 *
 * While `feeding` a read is always answered in full, the part that VoicePlayer could not give
 * being silence: a sink that gets fewer bytes than it asked for may go idle and, depending on the
 * backend, not come back by itself. Not feeding, a read gives nothing.
 */
class PullDevice : public QIODevice
{
    public:

        explicit PullDevice(media::VoicePlayer& player) : m_player(player)
        {
        }

        void setFeeding(bool enable) noexcept
        {
            m_feeding.store(enable,std::memory_order_release);
        }

        bool isSequential() const override
        {
            return true;
        }

        qint64 bytesAvailable() const override
        {
            return (m_feeding.load(std::memory_order_acquire) ? SinkBufferBytes : 0) + QIODevice::bytesAvailable();
        }

    protected:

        qint64 readData(char* data, qint64 maxSize) override
        {
            if (!m_feeding.load(std::memory_order_acquire))
            {
                return 0;
            }

            // whole frames only
            maxSize-=maxSize%static_cast<qint64>(sizeof(int16_t));

            qint64 done=0;
            while (done<maxSize)
            {
                const auto frames=std::min<size_t>(ScratchFrames,static_cast<size_t>(maxSize-done)/sizeof(int16_t));

                // VoicePlayer writes int16_t, and the sink's buffer is only bytes: go through an aligned scratch
                const auto got=m_player.pull(m_scratch.data(),frames);
                std::memcpy(data+done,m_scratch.data(),got*sizeof(int16_t));
                done+=static_cast<qint64>(got*sizeof(int16_t));

                if (got<frames)
                {
                    // starved or finished: the rest of this read is silence
                    std::memset(data+done,0,static_cast<size_t>(maxSize-done));
                    done=maxSize;
                }
            }
            return done;
        }

        qint64 writeData(const char*, qint64) override
        {
            return -1;
        }

    private:

        media::VoicePlayer& m_player;
        std::atomic<bool> m_feeding{false};
        std::array<int16_t,ScratchFrames> m_scratch;
};

void closeQuietly(common::File& file) noexcept
{
    if (file.isOpen())
    {
        common::Error ec;
        file.close(ec);
    }
}

}

//---------------------------------------------------------------

class VoicePlaybackEngine_p
{
    public:

        VoicePlaybackEngine_p(VoicePlaybackEngine* engine, std::shared_ptr<common::Thread> decodeThread);

        VoicePlaybackEngine* self;

        std::shared_ptr<common::Thread> thread;
        bool ownsThread=false;
        uint32_t decodeTimerId=0;
        bool decodeTimerInstalled=false;
        std::atomic<bool> decodeFailed{false};

        //! Bumped whenever a message is loaded or unloaded, so that a failure that was queued for
        //! the GUI thread by the previous message is not blamed on the next one.
        int generation=0;

        // Members that reach one another are declared so that they are destroyed in the reverse
        // order: the sink first, then the device, then the file and last the player.
        media::VoicePlayer player;
        std::shared_ptr<common::File> file;
        std::unique_ptr<PullDevice> device;
        std::unique_ptr<QAudioSink> sink;

        QByteArray outputId;

        QTimer guiTimer;
        QTimer drainTimer;
        bool draining=false;

        State state=State::Stopped;
        qint64 duration=0;
        qint64 lastPosition=-1;
        qreal volume=1.0;
        bool muted=false;

        void setState(State newState);

        //! Tell the client of a failure that is not a change of state.
        void report(const QString& message);

        //! Stop for a failure: back to the start, Stopped, and report.
        void fail(const QString& message);

        void load(std::shared_ptr<common::File> newFile);
        void unload();

        //! unload() for a client that has seen the message: it is told that nothing is loaded now.
        void unloadAndNotify();

        void startDecodeTimer();
        void stopDecodeTimer();
        void onDecodeTick();
        void onDecodeFailed(int messageGeneration, const QString& message);

        QAudioDevice findOutputDevice() const;

        //! Make the sink if there is none. False with the reason in `error`; nothing is reported.
        bool ensureSink(QString& error);
        void startSink();
        void suspendSink();
        void stopSink();
        void destroySink();
        void applyVolume();
        void onSinkState(QtAudio::State newState);

        void onGuiTick();
        void onDrained();
};

//---------------------------------------------------------------

VoicePlaybackEngine_p::VoicePlaybackEngine_p(VoicePlaybackEngine* engine, std::shared_ptr<common::Thread> decodeThread)
    : self(engine),
      thread(std::move(decodeThread))
{
    if (!thread)
    {
        thread=std::make_shared<common::Thread>("voiceplayer");
        thread->start();
        ownsThread=true;
    }

    guiTimer.setInterval(GuiTickMs);
    QObject::connect(&guiTimer,&QTimer::timeout,self,[this]()
    {
        onGuiTick();
    });

    drainTimer.setSingleShot(true);
    drainTimer.setInterval(SinkBufferMs+DrainExtraMs);
    QObject::connect(&drainTimer,&QTimer::timeout,self,[this]()
    {
        onDrained();
    });
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::setState(State newState)
{
    if (state==newState)
    {
        return;
    }
    state=newState;
    emit self->stateChanged(newState);
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::report(const QString& message)
{
    setState(State::Stopped);
    emit self->errorOccurred(message);
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::fail(const QString& message)
{
    // Silently, and before the sink is stopped, so that the stateChanged() of the sink finds nothing
    // to react to.
    const bool wasStopped=(state==State::Stopped);
    state=State::Stopped;

    guiTimer.stop();
    drainTimer.stop();
    draining=false;
    stopSink();
    if (file)
    {
        player.pause();
        player.seekMs(0);
    }

    lastPosition=0;
    emit self->positionChanged(0);
    if (!wasStopped)
    {
        emit self->stateChanged(State::Stopped);
    }
    emit self->errorOccurred(message);
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::unloadAndNotify()
{
    const auto previousState=state;
    const auto previousDuration=duration;
    unload();
    if (previousState!=State::Stopped)
    {
        emit self->stateChanged(State::Stopped);
    }
    if (previousDuration!=0)
    {
        emit self->durationChanged(0);
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::load(std::shared_ptr<common::File> newFile)
{
    unloadAndNotify();

    if (!newFile || !newFile->isOpen())
    {
        report(QStringLiteral("the voice message file is not open"));
        return;
    }

    auto ec=player.open(*newFile);
    if (ec)
    {
        closeQuietly(*newFile);
        report(QString::fromStdString(ec.message()));
        return;
    }

    file=std::move(newFile);
    duration=static_cast<qint64>(player.durationMs());
    lastPosition=0;
    startDecodeTimer();

    emit self->durationChanged(duration);
    emit self->positionChanged(0);
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::unload()
{
    guiTimer.stop();
    drainTimer.stop();
    draining=false;

    // the decode thread must not be in fill() when the player is closed
    stopDecodeTimer();
    destroySink();

    if (file)
    {
        player.close();
        closeQuietly(*file);
        file.reset();
    }

    ++generation;
    state=State::Stopped;
    duration=0;
    lastPosition=-1;
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::startDecodeTimer()
{
    if (decodeTimerInstalled)
    {
        return;
    }

    decodeFailed.store(false,std::memory_order_release);
    decodeTimerId=thread->installTimer(
        static_cast<uint64_t>(DecodeTickMs)*1000,
        [this]()
        {
            onDecodeTick();
            return true;
        }
    );
    decodeTimerInstalled=true;
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::stopDecodeTimer()
{
    if (!decodeTimerInstalled)
    {
        return;
    }

    // waits for a running handler to return, so never from the handler itself
    thread->uninstallTimer(decodeTimerId,true);
    decodeTimerInstalled=false;
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::onDecodeTick()
{
    // decode thread
    if (decodeFailed.load(std::memory_order_acquire) || !player.needsFill())
    {
        return;
    }

    auto ec=player.fill();
    if (!ec)
    {
        return;
    }

    // Decoding has stopped where it failed; what is buffered can still be pulled, after which the
    // player would be silent without ever reaching Ended. The timer is removed by the GUI thread,
    // it keeps ticking until then and does nothing.
    decodeFailed.store(true,std::memory_order_release);
    player.pause();

    const auto messageGeneration=generation;
    const auto message=QString::fromStdString(ec.message());
    QMetaObject::invokeMethod(self,[this,messageGeneration,message]()
    {
        onDecodeFailed(messageGeneration,message);
    },Qt::QueuedConnection);
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::onDecodeFailed(int messageGeneration, const QString& message)
{
    if (messageGeneration!=generation)
    {
        return;
    }

    stopDecodeTimer();
    fail(message);
}

//---------------------------------------------------------------

QAudioDevice VoicePlaybackEngine_p::findOutputDevice() const
{
    if (!outputId.isEmpty())
    {
        const auto outputs=QMediaDevices::audioOutputs();
        for (const auto& output : outputs)
        {
            if (output.id()==outputId)
            {
                return output;
            }
        }
    }
    return QMediaDevices::defaultAudioOutput();
}

//---------------------------------------------------------------

bool VoicePlaybackEngine_p::ensureSink(QString& error)
{
    if (sink)
    {
        return true;
    }

    const auto output=findOutputDevice();
    if (output.isNull())
    {
        error=QStringLiteral("there is no audio output device");
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(media::VoiceSampleRate));
    format.setChannelCount(static_cast<int>(media::VoiceChannels));
    format.setSampleFormat(QAudioFormat::Int16);
    if (!output.isFormatSupported(format))
    {
        error=QStringLiteral("the audio output device does not support mono 48 kHz 16-bit sound");
        return false;
    }

    if (!device)
    {
        device=std::make_unique<PullDevice>(player);
        device->open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    }

    sink=std::make_unique<QAudioSink>(output,format);
    sink->setBufferSize(SinkBufferBytes);
    applyVolume();
    QObject::connect(sink.get(),&QAudioSink::stateChanged,self,[this](QtAudio::State newState)
    {
        onSinkState(newState);
    });
    return true;
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::startSink()
{
    device->setFeeding(true);
    switch (sink->state())
    {
        case QtAudio::StoppedState:
            sink->start(device.get());
            break;

        case QtAudio::SuspendedState:
            sink->resume();
            break;

        default:
            break;
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::suspendSink()
{
    if (device)
    {
        device->setFeeding(false);
    }
    if (sink && (sink->state()==QtAudio::ActiveState || sink->state()==QtAudio::IdleState))
    {
        sink->suspend();
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::stopSink()
{
    if (device)
    {
        device->setFeeding(false);
    }
    if (sink)
    {
        sink->stop();
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::destroySink()
{
    if (device)
    {
        device->setFeeding(false);
    }
    if (sink)
    {
        // The sink's own thread, if the backend has one, is joined here; only after that may
        // the player be closed.
        sink->stop();
        sink.reset();
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::applyVolume()
{
    if (sink)
    {
        sink->setVolume(muted ? 0.0f : static_cast<float>(volume));
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::onSinkState(QtAudio::State newState)
{
    // Stopped by us leaves no error. Stopped with one is the device going away or failing under us.
    if (newState!=QtAudio::StoppedState || state!=State::Playing || !sink || sink->error()==QtAudio::NoError)
    {
        return;
    }

    fail(QStringLiteral("the audio output failed (error %1)").arg(static_cast<int>(sink->error())));
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::onGuiTick()
{
    if (state!=State::Playing)
    {
        guiTimer.stop();
        return;
    }

    const auto position=static_cast<qint64>(player.positionMs());
    if (position!=lastPosition)
    {
        lastPosition=position;
        emit self->positionChanged(position);
    }

    if (!draining && player.state()==media::PlayerState::Ended)
    {
        // everything was handed to the sink; give it time to play out
        draining=true;
        drainTimer.start();
    }
}

//---------------------------------------------------------------

void VoicePlaybackEngine_p::onDrained()
{
    draining=false;
    if (state!=State::Playing)
    {
        return;
    }

    guiTimer.stop();
    stopSink();
    player.seekMs(0);

    lastPosition=0;
    emit self->positionChanged(0);
    setState(State::Stopped);
}

//---------------------------------------------------------------

VoicePlaybackEngine::VoicePlaybackEngine(
        std::shared_ptr<hatn::common::Thread> decodeThread,
        QObject* parent
    ) : AudioPlaybackEngine(parent),
        pimpl(std::make_unique<VoicePlaybackEngine_p>(this,std::move(decodeThread)))
{
}

//---------------------------------------------------------------

VoicePlaybackEngine::~VoicePlaybackEngine()
{
    // no callback may run into a half-destroyed engine: the timer goes first, then the sink
    pimpl->unload();
    if (pimpl->ownsThread)
    {
        pimpl->thread->stop();
    }
}

//---------------------------------------------------------------

bool VoicePlaybackEngine::isAvailable() noexcept
{
    return media::isOggOpusAvailable();
}

//---------------------------------------------------------------

void VoicePlaybackEngine::open(const QString& source)
{
    QString path=source;
    const QUrl url(source);
    if (url.isLocalFile())
    {
        path=url.toLocalFile();
    }
    else if (url.scheme().size()>1)
    {
        // a one-letter scheme is a Windows drive letter
        pimpl->unloadAndNotify();
        pimpl->report(QStringLiteral("only local voice message files can be played"));
        return;
    }

    auto file=std::make_shared<common::PlainFile>();
    auto ec=file->open(path.toUtf8().constData(),common::File::Mode::read);
    if (ec)
    {
        pimpl->unloadAndNotify();
        pimpl->report(QString::fromStdString(ec.message()));
        return;
    }
    pimpl->load(std::move(file));
}

//---------------------------------------------------------------

void VoicePlaybackEngine::openFile(std::shared_ptr<hatn::common::File> file)
{
    pimpl->load(std::move(file));
}

//---------------------------------------------------------------

void VoicePlaybackEngine::play()
{
    if (!pimpl->file || pimpl->state==State::Playing)
    {
        return;
    }
    QString error;
    if (!pimpl->ensureSink(error))
    {
        pimpl->report(error);
        return;
    }

    pimpl->draining=false;
    pimpl->drainTimer.stop();

    // a no-op unless the timer was removed after a failure
    pimpl->startDecodeTimer();

    // on a message that has ended this starts it over
    pimpl->player.play();
    pimpl->startSink();
    pimpl->guiTimer.start();
    pimpl->setState(State::Playing);
}

//---------------------------------------------------------------

void VoicePlaybackEngine::pause()
{
    if (!pimpl->file || pimpl->state!=State::Playing)
    {
        return;
    }

    pimpl->draining=false;
    pimpl->drainTimer.stop();
    pimpl->guiTimer.stop();
    pimpl->player.pause();
    pimpl->suspendSink();

    pimpl->lastPosition=static_cast<qint64>(pimpl->player.positionMs());
    emit positionChanged(pimpl->lastPosition);
    pimpl->setState(State::Paused);
}

//---------------------------------------------------------------

void VoicePlaybackEngine::stop()
{
    if (!pimpl->file)
    {
        return;
    }

    pimpl->guiTimer.stop();
    pimpl->drainTimer.stop();
    pimpl->draining=false;
    pimpl->player.pause();
    pimpl->player.seekMs(0);
    pimpl->stopSink();

    pimpl->lastPosition=0;
    emit positionChanged(0);
    pimpl->setState(State::Stopped);
}

//---------------------------------------------------------------

void VoicePlaybackEngine::seekMs(qint64 ms)
{
    if (!pimpl->file)
    {
        return;
    }

    ms=std::clamp<qint64>(ms,0,pimpl->duration);

    if (pimpl->state==State::Playing)
    {
        pimpl->draining=false;
        pimpl->drainTimer.stop();
        if (pimpl->player.state()==media::PlayerState::Ended)
        {
            // an ended player stays ended through a seek; play() starts it over and the seek below moves it
            pimpl->player.play();
        }
    }
    pimpl->player.seekMs(static_cast<uint64_t>(ms));

    pimpl->lastPosition=ms;
    emit positionChanged(ms);
}

//---------------------------------------------------------------

void VoicePlaybackEngine::setVolume(qreal volume)
{
    pimpl->volume=std::clamp<qreal>(volume,0.0,1.0);
    pimpl->applyVolume();
}

//---------------------------------------------------------------

void VoicePlaybackEngine::setMuted(bool muted)
{
    pimpl->muted=muted;
    pimpl->applyVolume();
}

//---------------------------------------------------------------

void VoicePlaybackEngine::setPlaybackRate(qreal rate)
{
    // Legal while nothing is loaded, and clamped there: AbstractAudioPlayer::setEngine() pushes the
    // rate before any message is opened.
    pimpl->player.setSpeed(static_cast<float>(rate));
}

//---------------------------------------------------------------

VoicePlaybackEngine::State VoicePlaybackEngine::state() const
{
    return pimpl->state;
}

//---------------------------------------------------------------

qint64 VoicePlaybackEngine::positionMs() const
{
    if (!pimpl->file)
    {
        return 0;
    }
    return static_cast<qint64>(pimpl->player.positionMs());
}

//---------------------------------------------------------------

qint64 VoicePlaybackEngine::durationMs() const
{
    return pimpl->duration;
}

//---------------------------------------------------------------

void VoicePlaybackEngine::setOutputDevice(const QByteArray& id)
{
    if (pimpl->outputId==id)
    {
        return;
    }
    pimpl->outputId=id;

    if (!pimpl->sink)
    {
        return;
    }

    // What the old sink still holds is not heard: at most SinkBufferMs of sound.
    const bool playing=(pimpl->state==State::Playing);
    pimpl->destroySink();
    if (playing)
    {
        QString error;
        if (pimpl->ensureSink(error))
        {
            pimpl->startSink();
        }
        else
        {
            pimpl->fail(error);
        }
    }
}

//---------------------------------------------------------------

QByteArray VoicePlaybackEngine::outputDevice() const
{
    return pimpl->outputId;
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END
