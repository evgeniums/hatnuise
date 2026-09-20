/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/voicerecorderengine.h
  *
  *  Recorder of voice messages: hatn media VoiceRecorder behind the uise voice recorder dialog.
  *
  */

/****************************************************************************/

#ifndef HATNUISEVOICERECORDERENGINE_H
#define HATNUISEVOICERECORDERENGINE_H

#include <functional>
#include <memory>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <uise/desktop/abstractvoicerecorderdialog.hpp>

#include <hatn/media/voicerecorder.h>

#include <hatnuise/hatnuise.h>
#include <hatnuise/voicerecordingresult.h>

namespace hatn { namespace common {
class File;
class Thread;
}}

HATN_UISE_NAMESPACE_BEGIN

class VoiceRecorderEngine_p;

//! Makes a NEW, not yet opened handle on the file that is being recorded. See VoiceRecorderEngine::start().
using VoiceFileFactory=std::function<std::shared_ptr<hatn::common::File>()>;

/**
 * @brief Records a voice message from a microphone through Qt Multimedia into an Ogg Opus file.
 *
 * It is the recorder counterpart of VoicePlaybackEngine, and exists only where that one does: when
 * hatnuise was built with HATN_UISE_VOICE. It is also what turns the recorder dialog of uise-desktop,
 * which is presentation only, into a working recorder: attachDialog() connects the two.
 *
 * A host that opens the popup from the composer's microphone button does, in essence:
 *
 *   voiceRecorderOpened(dialog): create the file, engine->start(file,path,readFactory), engine->attachDialog(dialog)
 *   recordingFinished(result):   use the message, or cropRecording() it first when isCropped(result)
 *   voiceRecorderClosed():       engine->detachDialog(), engine->cancel() (a no-op after a send)
 *
 * THREADS. Every method is called on the GUI thread and every signal is emitted on it. Three other
 * threads take part and none shows:
 *  - CAPTURE, the thread of Qt's audio input, hands the microphone's PCM to VoiceRecorder::pushPcm(),
 *    which is wait-free, and nothing else.
 *  - WORKER, a hatn thread, runs VoiceRecorder::process() on a 20 ms timer: encoding, the file and
 *    its encryption happen there. Pass a running thread to share one, or none to have the engine
 *    start a thread of its own; the pre-listen decodes on the same thread.
 *  - The pre-listen has the decode thread and the audio thread of VoicePlaybackEngine.
 *
 * THE FILE. The engine records into a File that the caller has opened (write_new, at 0; a plain
 * file, or a crypt::CryptFile to record encrypted straight to disk). While the recording is paused
 * that file is CLOSED, and the engine reopens the same object in append mode to go on or to finish, so
 * that an encrypted file is never written and read at once. To go on from a cropped part it opens the
 * same object with Mode::write instead, which starts the file over, so a CryptFile has to keep the
 * settings of its container (the key derivation, the salt) from one open to the next; see resume().
 * The pre-listen reads through a second
 * handle, which the caller's VoiceFileFactory makes: only the caller knows the keys. Because of this
 * the caller must not touch the file while the engine has it, and it is closed whenever the engine
 * is Paused, Listening, Finished, Cancelled or Failed. It is never deleted by the engine.
 *
 * DIALOG. The dialog moves itself to the next state and then emits, and the engine follows; it
 * overrules the dialog with setState(Paused) where it cannot do what the user asked (a recording that
 * reached its length limit, a microphone that went away, a pre-listen that ended or failed).
 *
 * ERRORS come out of errorOccurred() and the text is for a log or a tooltip, not translated. A failure
 * that leaves the recording usable (no microphone on resume, the end of a pre-listen that would not
 * open) leaves the engine Paused. A failure of the encoder or of the file leaves it Failed, from where
 * only start() goes on, and the host should close the popup.
 */
class HATN_UISE_EXPORT VoiceRecorderEngine : public QObject
{
    Q_OBJECT

    public:

        enum class State
        {
            Idle,       //!< nothing has been started
            Recording,  //!< the microphone is open
            Paused,     //!< the file is closed and can be listened to
            Listening,  //!< Paused, and the recorded part is playing
            Finished,   //!< recordingFinished() was emitted
            Cancelled,  //!< cancelled, or too short to be a message
            Failed      //!< the encoder, the file or the start failed
        };
        Q_ENUM(State)

        /**
         * @param thread A running hatn thread for the worker and the pre-listen, or null for a thread
         *        of the engine's own.
         */
        explicit VoiceRecorderEngine(
                std::shared_ptr<hatn::common::Thread> thread=std::shared_ptr<hatn::common::Thread>{},
                QObject* parent=nullptr
            );

        /**
         * A recording that is still going is thrown away: the file is closed and left where it is.
         */
        ~VoiceRecorderEngine() override;

        VoiceRecorderEngine(const VoiceRecorderEngine&)=delete;
        VoiceRecorderEngine(VoiceRecorderEngine&&)=delete;
        VoiceRecorderEngine& operator=(const VoiceRecorderEngine&)=delete;
        VoiceRecorderEngine& operator=(VoiceRecorderEngine&&)=delete;

        //! Whether this build can encode voice messages (Ogg/Opus codec compiled into hatn media).
        static bool isAvailable() noexcept;

        //! Whether the system has a microphone at all.
        static bool hasInputDevice() noexcept;

        /**
         * @brief Whether the user has allowed the application to use the microphone. Never asks.
         *
         * Where the platform has no such permission this is true. start() refuses without it.
         */
        static bool isPermissionGranted();

        /**
         * @brief Ask for the permission if it has not been given, and tell the outcome.
         *
         * On macOS the application needs NSMicrophoneUsageDescription in its Info.plist, or the
         * system ends the process at the first use of the microphone. `done` is called on the GUI
         * thread, possibly at once.
         */
        void requestPermission(std::function<void(bool)> done);

        /**
         * @brief Choose the microphone by the id that QAudioDevice::id() reports.
         *
         * An empty id, or one that is not among the devices any more, means the default one. Takes
         * effect at the next start of the microphone, that is at the next start() or resume().
         */
        void setInputDevice(const QByteArray& id);
        QByteArray inputDevice() const;

        //! The loudspeaker of the pre-listen, the same way. See VoicePlaybackEngine::setOutputDevice().
        void setOutputDevice(const QByteArray& id);
        QByteArray outputDevice() const;

        //! Shortest and longest message, bitrate. Takes effect at the next start().
        void setConfig(const hatn::media::VoiceRecorderConfig& config);

        /**
         * @brief Begin recording a new message. Only in Idle, Finished, Cancelled or Failed.
         *
         * @param file Open for writing at position 0, and the caller's: see "THE FILE" above.
         * @param path The file's path. The engine reopens the file by it, and it is the path in the result.
         * @param readFactory Makes a new handle on the same file, of the same kind (a CryptFile for a
         *        CryptFile), not opened. The engine opens it for reading and closes it. May be empty
         *        when the pre-listen is not wanted; listening then fails with an error.
         * @return False, with the reason in errorOccurred(), when there is no codec, no microphone or
         *         no permission, or when the microphone or the file would not start. The engine is then
         *         Failed and the file, into which the header may have been written, is the caller's to delete.
         */
        bool start(
                std::shared_ptr<hatn::common::File> file,
                const QString& path,
                VoiceFileFactory readFactory=VoiceFileFactory{}
            );

        //! Recording -> Paused: the microphone is closed, the file is closed, the waveform is ready.
        void pause();

        /**
         * @brief Paused -> Recording. Not possible once limitReached() was emitted.
         *
         * When the crop handles of the attached dialog keep only part of the recording, the recording
         * goes on from the end of that part and that part only: what the handles cut off is thrown
         * away first. For that the part is decoded, the same file is written anew and the part is
         * encoded into it again, one more lossy generation as with cropRecording(), and the dialog is
         * given the new length, the new waveform and whole-message handles, as after a pause. It
         * happens on the calling thread and takes time in proportion to the length that is kept.
         * A failure before the file is touched leaves the recording as it was, Paused, with the reason in
         * errorOccurred(). After that the recording is lost and the engine is Failed. Without a dialog
         * nothing is cut off.
         */
        void resume();

        /**
         * @brief Paused -> Listening: play what has been recorded so far, from where the last seek left it.
         *
         * Only the part that the crop handles of the attached dialog keep is played: from the start of it
         * unless the position is inside it, and at its end the pre-listen is paused again and
         * preListenEnded() is emitted, as at the end of the message. A seek outside the part lands on its
         * edge. The end is watched by the ticks of the position, so it can be overshot by a few tens of
         * milliseconds. Without a dialog the whole recording is played.
         */
        void startPreListen();

        //! Listening -> Paused.
        void pausePreListen();

        //! Move the pre-listen to a position of the recording, at rest or while it plays, within the part that is kept.
        void seekPreListenMs(qint64 ms);

        /**
         * @brief Finish the message. Recording or Paused -> Finished.
         *
         * On success recordingFinished() is emitted and true returned; the file has been closed.
         * A recording that is shorter than the configured minimum is not a message: the engine
         * becomes Cancelled, recordingTooShort() is emitted, and the caller deletes the file.
         *
         * @param comment Carried into the result, untouched.
         * @param cropStart Carried into the result, untouched: the file is not trimmed here, see cropRecording().
         * @param cropEnd The same.
         */
        bool finish(const QString& comment=QString(), qreal cropStart=0.0, qreal cropEnd=1.0);

        /**
         * @brief Throw the recording away. A no-op when there is none going.
         *
         * The file is closed and left, the caller deletes it. recordingCancelled() is emitted.
         */
        void cancel();

        State state() const;

        //! Length of the recording so far.
        qint64 elapsedMs() const;

        //! Frames of the recording that were lost because the worker did not keep up. Non-zero means a gap.
        quint64 overruns() const;

        /**
         * @brief Let the dialog drive this recorder, and show it what it records.
         *
         * Connects every request of the dialog to the matching method here, and pushes the length,
         * the waveform, the position of the pre-listen and where needed the state back. Replaces a
         * dialog attached before. The popup of the composer is reused for every recording, so this
         * goes with a detachDialog() from the host, in voiceRecorderClosed(); an engine that is
         * destroyed detaches itself.
         */
        void attachDialog(UISE_DESKTOP_NAMESPACE::AbstractVoiceRecorderDialog* dialog);

        //! Disconnect from the dialog. Only the connections made by attachDialog() go, no other one.
        void detachDialog();

    signals:

        void stateChanged(hatnuise::VoiceRecorderEngine::State state);

        //! About ten times a second while recording.
        void elapsedMsChanged(qint64 ms);

        //! The waveform of what is recorded: 100 bytes of 0..255. At every pause.
        void waveformChanged(const QByteArray& waveform);

        //! Position of the pre-listen, in time of the recording.
        void preListenPositionChanged(qint64 ms);

        //! The pre-listen reached the end, or failed: it is Paused again.
        void preListenEnded();

        //! The recording reached its maximum length and was paused. It can only be finished or cancelled.
        void limitReached();

        void recordingFinished(const hatnuise::VoiceRecordingResult& result);

        //! finish() found the recording too short; the engine is Cancelled and the file is the caller's to delete.
        void recordingTooShort();

        void recordingCancelled();

        //! Not translated.
        void errorOccurred(const QString& message);

    private:

        std::unique_ptr<VoiceRecorderEngine_p> pimpl;
};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEVOICERECORDERENGINE_H
