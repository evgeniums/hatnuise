/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/voiceplaybackengine.h
  *
  *  Playback engine of voice messages: hatn media VoicePlayer behind the uise AudioPlaybackEngine.
  *
  */

/****************************************************************************/

#ifndef HATNUISEVOICEPLAYBACKENGINE_H
#define HATNUISEVOICEPLAYBACKENGINE_H

#include <memory>

#include <QByteArray>
#include <QString>

#include <uise/desktop/audioplaybackengine.hpp>

#include <hatnuise/hatnuise.h>

namespace hatn { namespace common {
class File;
class Thread;
}}

HATN_UISE_NAMESPACE_BEGIN

class VoicePlaybackEngine_p;

/**
 * @brief Plays Ogg Opus voice messages through Qt Multimedia.
 *
 * It is the engine that AbstractAudioPlayer::setEngine() and the voice row talk to, in the place
 * of QtAudioPlaybackEngine, which does not play voice messages. It exists only when hatnuise was
 * built with HATN_UISE_VOICE, that is with hatn media and Qt Multimedia.
 *
 * THREADS. Every method is called on the GUI thread and every signal is emitted on it, as
 * AudioPlaybackEngine requires. Two other threads take part and neither one shows:
 *  - the DECODE thread, a hatn thread, runs VoicePlayer::fill() on a timer. Decoding, file reads and
 *    decryption happen there and only there. Pass a thread to share one (it must be running), or
 *    none to have the engine start a thread of its own.
 *  - the AUDIO side, QAudioSink, pulls PCM from VoicePlayer::pull(), which is wait-free. Which
 *    thread that is depends on the Qt audio backend; only one at a time ever calls it.
 * A failure of the decode thread is posted to the GUI thread and reported by errorOccurred().
 *
 * The engine asks for mono 48 kHz signed 16-bit output, the format of voice messages. It pads a
 * starved or finished stream with silence while playing, so that no backend goes idle in the
 * middle of a message, and lets the buffered tail play out before it reports Stopped at the end.
 *
 * Position and duration are in time of the message, also at another playback rate: at 2x ten
 * seconds of sound advance the position by twenty. The rate is clamped by hatn media to 0.5..2.
 *
 * ERRORS come out of errorOccurred() and leave the engine Stopped. The text is for a log or a
 * tooltip and is not translated. Without the Ogg/Opus codec in the build (isAvailable() is false)
 * opening a message fails that way and nothing else changes.
 */
class HATN_UISE_EXPORT VoicePlaybackEngine : public UISE_DESKTOP_NAMESPACE::AudioPlaybackEngine
{
    Q_OBJECT

    public:

        /**
         * @param decodeThread A running hatn thread to decode on, or null for a thread of the
         *        engine's own. Timers of the engine are removed from it before the engine is gone.
         */
        explicit VoicePlaybackEngine(
                std::shared_ptr<hatn::common::Thread> decodeThread=std::shared_ptr<hatn::common::Thread>{},
                QObject* parent=nullptr
            );
        ~VoicePlaybackEngine() override;

        VoicePlaybackEngine(const VoicePlaybackEngine&)=delete;
        VoicePlaybackEngine(VoicePlaybackEngine&&)=delete;
        VoicePlaybackEngine& operator=(const VoicePlaybackEngine&)=delete;
        VoicePlaybackEngine& operator=(VoicePlaybackEngine&&)=delete;

        //! Whether this build can decode voice messages (Ogg/Opus codec compiled into hatn media).
        static bool isAvailable() noexcept;

        /**
         * @brief Load a voice message from a plain local file. Does not start playing.
         * @param source A local path or a file: URL.
         */
        void open(const QString& source) override;

        /**
         * @brief Load a voice message from a file the caller has opened for reading.
         *
         * For a message that is stored encrypted, pass an open crypt::CryptFile. The engine takes
         * the file over: it reads from it on the decode thread and closes it when the engine loads
         * another message or is destroyed. The caller must not touch it meanwhile.
         */
        void openFile(std::shared_ptr<hatn::common::File> file);

        void play() override;
        void pause() override;

        //! Stop and go back to the start.
        void stop() override;

        void seekMs(qint64 ms) override;

        void setVolume(qreal volume) override;
        void setMuted(bool muted) override;
        void setPlaybackRate(qreal rate) override;

        State state() const override;
        qint64 positionMs() const override;
        qint64 durationMs() const override;

        /**
         * @brief Choose the output device by the id that QAudioDevice::id() reports.
         *
         * An empty id, or one that is not among the devices any more, means the default output. It
         * takes effect at once, and a message that is playing goes on from the sink's next buffer.
         */
        void setOutputDevice(const QByteArray& id) override;

        QByteArray outputDevice() const;

    private:

        std::unique_ptr<VoicePlaybackEngine_p> pimpl;
};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEVOICEPLAYBACKENGINE_H
