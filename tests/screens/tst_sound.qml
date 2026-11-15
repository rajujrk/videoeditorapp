import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Sound: the Audio tab, audio effects and loudness, the mixer, plug-ins (a clip's and a
// track's), speech enhancement, pauses and ducking, a voice-over take, captions and the
// transcript.
Item {
    id: root
    width: 1470
    height: 790
    Component {
        id: editorComponent
        Item {
            property alias project: controller
            property alias editor: screen
            anchors.fill: parent
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    SignalSpy { id: finished; signalName: "analysisFinished" }
    SignalSpy { id: downloaded; signalName: "modelDownloadFinished" }

    TestCase {
        name: "ScreensSound"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string samples: "file://" + samplesDir + "/"
        readonly property string analysis: "file://" + analysisMediaDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            finished.clear(); finished.target = f.project
            downloaded.clear(); downloaded.target = f.project
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function audioTrack(p) { return p.tracks.find((t) => t.kind === "audio") }
        function shot(name, dark, settle) {
            mouseMove(root, root.width - 4, root.height - 4)
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function scrollTo(f, name) {
            const item = findChild(f.editor, name)
            verify(item, name)
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                a.contentY = Math.max(0, Math.min(item.mapToItem(a.contentItem, 0, 0).y - 12, a.contentHeight - a.height))
                return
            }
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 120000, "the analysis never finished")
            verify(finished.signalArguments[count - 1][0], finished.signalArguments[count - 1][1])
        }
        function speechModel(p) {
            if (!p.speechModels[0].downloaded) {
                verify(p.downloadSpeechModel("base.en-q5_1"))
                tryVerify(() => downloaded.count >= 1, 60000)
            }
        }
        function clipOf(p, mediaId) {
            for (const t of p.tracks)
                for (const c of t.clips)
                    if (c.mediaId === mediaId)
                        return Number(c.id)
            return 0
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // The Audio tab previewing music, with music and a whoosh at the playhead.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.importAndAppend(media + "portrait-wide.mp4"))
                const sounds = p.soundLibrary.sounds
                const music = sounds.find((s) => s.kind === "music")
                const whoosh = sounds.find((s) => s.title.indexOf("Whoosh") >= 0) || sounds.find((s) => s.kind !== "music")
                verify(music && whoosh)
                p.seek(0.5)
                verify(p.addSound(music.id), "" + p.lastError)
                p.seek(2.6)
                verify(p.addSound(whoosh.id), "" + p.lastError)
                findChild(f.editor, "libraryTab:Audio").clicked()
                verify(p.soundLibrary.preview(music.id))
                p.seek(1.2)
                shot("audio", dark)
                p.soundLibrary.stopPreview()

                // Voice Clarity and Concert Hall on a clip's sound, normalized to −14 LUFS.
                f.destroy(); wait(200)
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"))
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"))
                let clip = Number(main(p).clips[0].id)
                verify(p.addAudioEffect(clip, "tve.audio.voice_clarity"), "" + p.lastError)
                verify(p.addAudioEffect(clip, "tve.audio.hall"), "" + p.lastError)
                verify(p.normalizeLoudness(clip, -14), "" + p.lastError)
                tryVerify(() => p.undoText === "Normalize Loudness", 30000)
                p.selectedClip = clip
                p.seek(1)
                findChild(f.editor, "libraryTab:Audio").clicked()
                findChild(f.editor, "inspectorTab:Audio").clicked()
                shot("audio-effects", dark)

                // The mixer during playback.
                findChild(f.editor, "mixerButton").clicked()
                const mixer = findChild(f.editor, "mixerPanel")
                tryVerify(() => mixer.opened)
                p.seek(0.2)
                p.play()
                wait(1200)
                shot("mixer", dark, 300)
                p.pause()
                mixer.close()
                f.destroy(); wait(200)

                // Plug-ins: AUDelay and the test VST3 on a clip; a track's own effects.
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"))
                clip = Number(main(p).clips[0].id)
                p.audioPlugins
                tryVerify(() => !p.scanningPlugins && p.audioPlugins.length > 0, 60000, "no plug-ins")
                verify(p.addAudioEffect(clip, "au:aufx/dely/appl"), "" + p.lastError)
                verify(p.addAudioEffect(clip, "vst3:5A3C1E427B9D4F608E21C3A4D05B6F17"), "" + p.lastError)
                p.selectedClip = clip
                p.seek(1)
                findChild(f.editor, "inspectorTab:Audio").clicked()
                wait(300)
                scrollTo(f, "audioEffectsSection")
                shot("plugins", dark)
                const lane = main(p).id
                verify(p.addTrackAudioEffect(lane, "au:aufx/dely/appl"), "" + p.lastError)
                findChild(f.editor, "mixerButton").clicked()
                const panel = findChild(f.editor, "mixerPanel")
                tryVerify(() => panel.opened)
                tryVerify(() => findChild(panel.contentItem, "trackEffects:" + lane) !== null)
                mouseClick(findChild(panel.contentItem, "trackEffects:" + lane))
                const effects = findChild(f.editor, "trackEffectsPanel")
                tryVerify(() => effects.opened)
                shot("track-effects", dark)
                effects.close(); panel.close()
                f.destroy(); wait(200)

                // Speech enhanced on a talking clip (Voice Cleanup on its sound too).
                f = editor()
                p = f.project
                speechModel(p)
                compare(p.importMedia([samples + "speech_en.wav"]), 1)
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.addClip(p.media[0].id))
                clip = clipOf(p, p.media[0].id)
                p.selectedClip = clip
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                if (!p.enhanceModel.downloaded) {
                    wait(300)
                    scrollTo(f, "enhanceSection")
                    findChild(f.editor, "downloadEnhanceModel").clicked()
                    tryVerify(() => p.enhanceModel.downloaded, 120000, "the enhancement model never came")
                }
                verify(p.addAudioEffect(clip, "tve.audio.voice_cleanup"), "" + p.lastError)
                const before = finished.count
                verify(p.enhanceSpeech(clip), "" + p.lastError)
                tryVerify(() => finished.count > before, 120000)
                p.selectedClip = clip
                p.seek(1)
                wait(300)
                scrollTo(f, "enhanceSection")
                shot("enhance", dark)
                f.destroy(); wait(200)

                // Two pauses found in a talk, the music below lowered under the speech.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                compare(p.importMedia([analysis + "talk.mov", analysis + "hum.wav"]), 2)
                const talkMedia = p.media.find((m) => m.name === "talk.mov").id
                const humMedia = p.media.find((m) => m.name === "hum.wav").id
                verify(p.addClip(talkMedia))
                verify(p.addClip(humMedia))
                const hum = clipOf(p, humMedia)
                verify(p.duckUnderSpeech(hum), "" + p.lastError)
                waitFinished(1)
                const talk = clipOf(p, talkMedia)
                p.selectedClip = talk
                verify(p.findSilences(talk), "" + p.lastError)
                waitFinished(2)
                p.seek(1.4)
                verify(p.canvasWidth === 1920)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                findChild(f.editor, "timelinePanel").pixelsPerSecond = 120
                shot("analyze", dark)
                f.destroy(); wait(200)

                // A voice-over take, mid-recording.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.importAndAppend(media + "portrait-wide.mp4"))
                p.seek(1)
                findChild(f.editor, "voiceOverButton").clicked()
                const vo = findChild(f.editor, "voiceOverPanel")
                tryVerify(() => vo.opened)
                tryVerify(() => p.voiceOverActive)
                const countIn = findChild(vo.contentItem, "countIn")
                if (vo.countIn) countIn.clicked()
                findChild(vo.contentItem, "recordButton").clicked()
                tryVerify(() => p.recording)
                tryVerify(() => p.recordingTime > 1.5, 6000)
                shot("voiceover", dark, 200)
                findChild(vo.contentItem, "recordButton").clicked()
                tryVerify(() => !p.recording)
                vo.close()
                f.destroy(); wait(200)

                // Captions made from speech on this Mac.
                f = editor()
                p = f.project
                speechModel(p)
                verify(p.importAndAppend(media + "night-street.mp4"))
                compare(p.importMedia([samples + "speech_en.wav"]), 1)
                verify(p.addClip(p.media[1].id))
                p.selectedClip = 0
                let n = finished.count
                verify(p.generateCaptions({ model: "base.en-q5_1" }), "" + p.lastError)
                tryVerify(() => finished.count > n, 120000)
                p.seek(0.6)
                findChild(f.editor, "libraryTab:Captions").clicked()
                shot("captions", dark)

                // The transcript: three words chosen to delete; karaoke captions.
                verify(p.setCaptionStyle("karaoke"), "" + p.lastError)
                findChild(f.editor, "libraryTab:Transcript").clicked()
                tryVerify(() => findChild(f.editor, "word:3") !== null)
                mouseClick(findChild(f.editor, "word:1"))
                mouseClick(findChild(f.editor, "word:3"), 2, 2, Qt.LeftButton, Qt.ShiftModifier)
                p.seek(p.transcript[2].start + 0.05)
                shot("transcript", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
