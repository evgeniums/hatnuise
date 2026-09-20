/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/voicerecordingresult.h
  *
  *  What a finished voice message recording hands to its host, and cropping it.
  *
  */

/****************************************************************************/

#ifndef HATNUISEVOICERECORDINGRESULT_H
#define HATNUISEVOICERECORDINGRESULT_H

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <hatn/common/error.h>
#include <hatn/common/file.h>

#include <hatn/media/voicerecorder.h>

#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

/**
 * @brief A recorded voice message, as VoiceRecorderEngine hands it to the host.
 *
 * It holds nothing that belongs to an application: no message, chat or storage type. The host
 * (a demo, an application client) turns it into a file message.
 *
 * The file at `path` is complete and closed, and the host owns it: it uploads it, or deletes it.
 * The crop range is what the user asked for in the recorder dialog and has NOT been applied to the
 * file: `recording` describes the file as recorded. Where the range is not the whole message, the host
 * calls cropRecording() and then uses the `cropped` recording it gives back instead.
 */
struct VoiceRecordingResult
{
    //! Where the finished message is.
    QString path;

    //! Length, the 100-byte waveform, size of the file, rate and channels of the recorded file.
    hatn::media::VoiceRecording recording;

    //! Where the message is to start and end, 0..1 of what was recorded. 0 and 1 mean untrimmed.
    qreal cropStart=0.0;
    qreal cropEnd=1.0;

    //! The comment typed into the recorder dialog, possibly empty.
    QString comment;

    //! recording.waveform as the bytes that the widgets and the message metadata take.
    QByteArray waveformBytes() const
    {
        return QByteArray(
            reinterpret_cast<const char*>(recording.waveform.data()),
            static_cast<qsizetype>(recording.waveform.size())
        );
    }
};

//! Whether the user trimmed the message, so that cropRecording() has something to do.
HATN_UISE_EXPORT bool isCropped(const VoiceRecordingResult& result) noexcept;

/**
 * @brief Write the trimmed part of a recorded message to a new file.
 *
 * @param result What the recorder gave.
 * @param in Open for reading, on result.path.
 * @param out A different file, open for writing (write_new) and positioned at 0. Open a
 *        crypt::CryptFile to have it encrypted. Only the caller can, since only it holds the keys.
 * @param cropped Receives the length, waveform and size of the new message.
 *
 * Neither file is closed. On an error `out` holds an incomplete stream that the caller discards.
 * A range that is shorter than a frame or that lies outside the message is INVALID_ARGUMENT.
 * The cropped file is one more lossy generation, see hatn::media::cropVoice().
 */
HATN_UISE_EXPORT hatn::common::Error cropRecording(
        const VoiceRecordingResult& result,
        hatn::common::File& in,
        hatn::common::File& out,
        hatn::media::VoiceRecording& cropped
    );

HATN_UISE_NAMESPACE_END

Q_DECLARE_METATYPE(hatnuise::VoiceRecordingResult)

#endif // HATNUISEVOICERECORDINGRESULT_H
