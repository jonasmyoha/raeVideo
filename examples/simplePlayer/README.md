# simplePlayer — a simple video player

One video file in a window: the picture fitted to the window (letterboxed,
aspect kept), an Open... button (another file, picked in the OS's panel, shown
paused on its first frame), a play/pause button (the space key too) and a seek
bar under it.
A click on the bar moves there; a drag moves the picture with the pointer, with
the sound stopped, and the sound starts again from the new place on release.
The line at the bottom shows the work per refresh and the frames dropped.

    make run-player FILE=clip.mp4                        # or: make run FILE=clip.mp4
    make run-player                                      # asks for a file
    ./rae run examples/simplePlayer/Main.rae -- clip.mp4   # the same, without make

What it uses: `packages/video`'s `VideoPlayer` (MP4 / MOV, VideoToolbox), with
Rae's audio engine as the clock; Rae's `lib/ui` for the window
(`assets/Ui.raescene`). The frame is drawn from the player's texture: the
decoded planes go to the GPU and a compute pass converts them into an RGBA
texture made in the canvas's own GPU resources, which is the canvas image the
scene's Picture node (a Sprite, scaled to fit) shows. Each new frame changes
that texture's contents, so `lib/ui` needed nothing new; the CPU does not touch
the pixels.

Files: `Main.rae` (the window and its loop), `PlayerControls.rae` (what the
button, the space key and the seek bar do, and the frame on the GPU),
`PlayerTest.rae` (the headless test), `assets/` (the scene, the theme, the
Roboto font atlas: `assets/ATTRIBUTION.md`).

## The test

`tools/test.sh` runs it with `config.env`: `RAE_SIMPLE_PLAYER_TEST=1` and a
hidden window. The app runs as it does for a person on
`tests/media/counter709.mp4`, but the audio engine is offline (800 samples per
refresh, 60 Hz) and a script presses the simulated pointer: play, pause, a
click at the bar's middle, a drag along the bar while playing, play to the
end, then Open... (the panel answered by `RAE_SDL_FILE_DIALOG_RESULT` with
`tests/media/counterHevc.mp4`, so none is shown). After every refresh the texture the window shows is read back and the
frame counter burned into it compared with the frame the audio clock is at;
the sound is checked silent during pause and drag, and to be the file's tone at
the new place after the release (`expected.txt`).

## 4K, measured (2026-11-14)

`sh tests/media/make.sh 4k` makes the two 4K files (3840x2160, 300 frames at
29.97 fps). Run by hand, in a visible window on an Apple M1 Max (macOS 27.0),
with the offline audio engine following the wall clock so the run is silent:

    RAE_AUDIO_DEVICE=offline RAE_SIMPLE_PLAYER_MEASURE=1 \
      ./rae run examples/simplePlayer/Main.rae -- build/media/counter4k.mp4

(`RAE_SIMPLE_PLAYER_MEASURE=1` shows the first frame paused, starts play ten
refreshes later and closes the window at the end, logging the debug line.)

| file | frame work per refresh (refresh, upload, conversion) | frames shown | dropped |
|---|---|---|---|
| H.264, run 1 | mean 2.5 ms, p95 9.5 ms | 282 | 1 |
| H.264, run 2 | mean 2.3 ms, p95 9.6 ms | 300 | 0 |
| H.264, run 3 | mean 2.3 ms, p95 9.6 ms | 299 | 1 |
| H.264, run 4 | mean 2.3 ms, p95 9.2 ms | 299 | 1 |
| HEVC, run 1 | mean 2.2 ms, p95 9.3 ms | 300 | 0 |
| HEVC, run 2 | mean 2.3 ms, p95 9.6 ms | 300 | 0 |

The player's own work stays well inside a 60 Hz refresh (16.7 ms). The few
dropped frames are not it: in those refreshes the window's frame
(`Gpu2d.beginFrame` to `endFrame`) waited 21 to 36 ms, about two display
refreshes, to present, and a 29.97 fps frame was skipped. Run 1 of H.264 was
the first after a rebuild, and showed fewer frames for a reason not yet found.
The first frame's setup (the 4K textures, the conversion pipeline) takes about
60 ms, which is why play starts after the paused first frame, as it does for a
person. Next steps for smoother playback are the frame pipeline's decode-ahead
and recording the conversion into the canvas's own frame (docs/design.md §5
V3), not a zero-copy upload (docs/measurements.md).

## Closing

Closing the window ends the loop; the program waits for the conversions still
on the GPU, stops the player's sound and the audio engine (its threads end
there), releases the canvas and closes the window before it returns. Every run
above, and the test, ended by itself with nothing of it left running.
