/* TechieVideoEditor feature guide: the features, where they live, and how to use them.
   Every fact here comes from the project's README; keep the two in step. Loaded as a plain
   script (not fetched) so the guide also works opened straight from the folder. */
window.TVE = {
  // focus: the part of a feature's screenshot that shows it, as [left, top, width, height] in
  // percent; an area's focus is the default for its features. tools/make_web_images.sh crops
  // images/focus/<feature id>-<theme>.webp from it. mark: where a feature's page outlines its
  // area in the feature screenshots, when that differs from the map's box.
  areas: [
    { id: "topbar", n: 1, name: "Top bar", blurb: "Undo, save, export, the render queue, AI settings, workspaces and shortcuts.", focus: [22, 8, 56, 64], box: [0, 0, 100, 5.6], pin: [74, 2.8] },
    { id: "library", n: 2, name: "Library", blurb: "The tab rail on the left: media, audio, titles, effects, templates, captions and more.", focus: [0, 2, 52, 60], box: [0, 5.6, 23.1, 56.5] },
    { id: "player", n: 3, name: "Player", blurb: "The picture, the transport bar and the buttons under it.", focus: [18, 0, 64, 74], box: [23.1, 5.6, 55.2, 56.5] },
    { id: "inspector", n: 4, name: "Inspector", blurb: "The selected clip's Video, Speed, Audio, Color, Effects and Analyze tabs.", focus: [76.5, 2, 23.5, 52], mark: [77, 5.6, 23, 70], box: [78.3, 5.6, 21.7, 56.5] },
    { id: "timeline", n: 5, name: "Timeline", blurb: "Tracks, the ruler and the toolbar of editing tools above them.", focus: [0, 45, 55, 55], box: [0, 62.1, 100, 37.9] },
    { id: "home", n: 6, name: "Home screen", blurb: "Where projects start, recover and change language.", focus: [0, 0, 62, 62] },
    { id: "menus", n: 7, name: "Menus and keys", blurb: "Right-click menus and keyboard shortcuts that work anywhere in the editor.", focus: [22, 15, 56, 64] }
  ],

  chapters: [
    { id: "edit", name: "Import and edit", kind: "video" },
    { id: "motion", name: "Placement and animation", kind: "video" },
    { id: "speed", name: "Speed", kind: "video" },
    { id: "colour", name: "Colour", kind: "video" },
    { id: "effects", name: "Effects, masks and transitions", kind: "effect" },
    { id: "titles", name: "Titles and graphics", kind: "title" },
    { id: "audio", name: "Audio", kind: "audio" },
    { id: "record", name: "Recording", kind: "audio" },
    { id: "local", name: "Analysis and AI on your Mac", kind: "effect" },
    { id: "cloud", name: "AI with your own keys", kind: "title" },
    { id: "auto", name: "Templates and automatic edits", kind: "title" },
    { id: "setup", name: "Projects and your setup", kind: "video" },
    { id: "export", name: "Export and share", kind: "audio" }
  ],

  features: [
    // ---------- Import and edit ----------
    {
      id: "import-media", title: "Import media", chapter: "edit", area: "library",
      where: ["Library", "Media", "Import"], image: "editor",
      summary: "Bring video, audio and image files into the project. Dropping files works too, onto the editor, the timeline or the player.",
      steps: ["Press ⌘I, or click the Import button at the top of the Media tab.", "Choose one or more files.", "Drag a file from the media bin onto a track, or onto the player, to put it on the timeline."],
      keys: ["⌘I"]
    },
    {
      id: "import-from", title: "Import from Photos, Music and devices", chapter: "edit", area: "library",
      where: ["Library", "Media", "⌄ Import from"], image: "imports",
      summary: "Pick photos and videos from your Photos albums, songs from your Music playlists, or files from a connected iPhone, camera or memory card.",
      steps: ["Click the ⌄ button in the Media tab (or a link under an empty bin).", "Choose Photos, Music or Devices, then an album, playlist or device.", "Select items and click Import."],
      note: "Photos, videos and device files are copied into the project's Imports folder, never overwriting. Songs are used where they are. macOS asks once for access to Photos and Music."
    },
    {
      id: "image-sequence", title: "Image sequences", chapter: "edit", area: "library",
      where: ["Library", "Media", "Import ▾", "An image sequence…"], image: "image-sequence",
      summary: "Play numbered pictures from a render or a time-lapse, such as shot_0001.png and shot_0002.png, as one video.",
      steps: ["Open Import ▾ and choose An image sequence….", "Pick any one picture; the dialog finds the rest in its folder.", "Check the frame count, size and any gaps, choose a frame rate (23.976 to 60), and import."],
      note: "The bin names it by its pattern (shot_####.png). It plays, gets proxies and exports like any video, and keeps transparency."
    },
    {
      id: "stills", title: "Photos, SVG, camera RAW and OpenEXR", chapter: "edit", area: "library",
      where: ["Library", "Media"], image: "stills",
      summary: "Photos and graphics import like video: PNG, JPEG, HEIC, WebP, TIFF, BMP, SVG, OpenEXR and camera RAW (CR2, CR3, NEF, ARW, RAF, DNG and others macOS reads). Animated GIFs play as video.",
      steps: ["Import the picture (⌘I) or drop it on the editor.", "On the timeline it becomes a 5-second clip; trim it to any length.", "Use placement, keyframes, colour, looks, effects and transitions as with any clip."],
      note: "SVG drawings are drawn at the size they are shown, so they stay sharp. Photos taken on their side are turned upright."
    },
    {
      id: "move-trim", title: "Move, trim and split clips", chapter: "edit", area: "timeline",
      where: ["Timeline"], image: "editor",
      summary: "The basics of cutting: drag clips along and between tracks, trim their edges, split them at the playhead, and delete them.",
      steps: ["Drag a clip to move it; it snaps to clip edges and the playhead and can change tracks.", "Drag a clip's left or right edge to trim it.", "Press S to split the selected clip at the playhead.", "Press ⌫ to delete it; on the main track the gap closes."],
      keys: ["S", "⌫", "⌘Z", "⇧⌘Z"]
    },
    {
      id: "in-out-blade", title: "In and Out, the blade and J K L", chapter: "edit", area: "timeline",
      where: ["Timeline"], image: "in-out-blade",
      summary: "Mark a range to play, delete or export; cut clips with the blade; shuttle through the timeline at speed.",
      steps: ["Press I and O to mark a range (⌥X clears it); it is shaded on the ruler.", "Right-click the timeline to play the range, delete it from every track, or export it.", "Press B for the blade and click a clip to cut it there; V goes back to selecting.", "L plays (press again for 2×, 4×, 8×), J plays backwards, K stops."],
      keys: ["I", "O", "⌥X", "B", "V", "J", "K", "L"]
    },
    {
      id: "markers", title: "Markers and chapters", chapter: "edit", area: "timeline",
      where: ["Timeline", "Toolbar", "Flag"], image: "structure",
      summary: "Drop coloured markers on the ruler; give them notes, lengths, and make them chapters of the exported MP4.",
      steps: ["Press M (or the flag button) to add a marker at the playhead.", "Click the flag to add a note, pick one of six colours, give it a length, or make it a chapter.", "Use ⌥← and ⌥→ to jump between markers; drag a flag to move it."],
      keys: ["M", "⌥←", "⌥→"]
    },
    {
      id: "groups-compound", title: "Groups and compound clips", chapter: "edit", area: "timeline",
      where: ["Timeline", "Toolbar", "Stack"], image: "compound-tab",
      summary: "Group clips so they move together, or fold them into one compound clip that you can grade, speed up and give transitions like any other.",
      steps: ["⇧-click or ⌘-click clips to select several.", "⌘G groups them; ⇧⌘G ungroups.", "⌥⌘G (or the stack button) makes a compound clip; double-click it to edit inside it in its own tab.", "⌥⌘G on a compound clip breaks it apart again."],
      keys: ["⌘G", "⇧⌘G", "⌥⌘G"]
    },
    {
      id: "source-monitor", title: "Source monitor", chapter: "edit", area: "player",
      where: ["Player", "Source tab"], image: "source-monitor",
      summary: "Play a media item on its own, mark the part you want, and insert, overwrite or append it.",
      steps: ["⌥-double-click a media item, or choose Open in the source monitor from its menu.", "Mark In and Out with I and O.", "Choose Insert (later clips move), Overwrite, or Append."],
      keys: ["I", "O"]
    },
    {
      id: "multicam", title: "Multicam", chapter: "edit", area: "inspector",
      where: ["Inspector", "Multicam angles"], image: "multicam",
      summary: "For a shoot with several cameras, cut between them live as the timeline plays.",
      steps: ["Line the cameras' clips up (Sync by sound in the Analyze tab).", "Select them and make a multicam clip.", "Press 1–9, or click an angle in the inspector, to cut to that camera at the playhead."],
      keys: ["1–9"]
    },
    {
      id: "clip-menu", title: "Clip and track menus", chapter: "edit", area: "menus", focus: [5, 15, 50, 70],
      where: ["Right-click a clip or a track header"], image: "clip-menu",
      summary: "Right-click for the edit you need: cut, copy, paste, duplicate, ripple delete, split or trim at the playhead, detach audio, speed, groups, markers and Show in Finder.",
      steps: ["Right-click a clip for its edits.", "Right-click a track header to add, rename, lock, hide, mute or delete tracks.", "Right-click empty timeline space to paste there, add a marker, close gaps or change track height."],
      keys: ["⌘X", "⌘C", "⌘V", "⌘D", "⌘A", "⇧⌫"]
    },
    {
      id: "paste-attributes", title: "Paste attributes", chapter: "edit", area: "menus", focus: [22, 15, 56, 64],
      where: ["Right-click a clip", "Copy, then Paste attributes"], image: "paste-attributes",
      summary: "Copy one clip's look and settings onto others, choosing exactly what moves across.",
      steps: ["Copy a clip (⌘C).", "Select the clips to change and press ⇧⌘V.", "Choose position and size, colour, effects, keying, sound or speed, and paste."],
      keys: ["⇧⌘V"]
    },
    {
      id: "replace-media", title: "Replace a clip's media", chapter: "edit", area: "menus", focus: [52, 20, 48, 50],
      where: ["Right-click a clip", "Replace with"], image: "timeline-handling",
      summary: "Swap in other media while the clip keeps its place, length, speed, look, effects, keyframes and transition.",
      steps: ["Right-click the clip and open Replace with.", "Pick a picture (or, on a sound track, a sound) from the project.", "If the new media is shorter, the clip shortens to fit; ⌘Z undoes it."]
    },
    {
      id: "clip-off", title: "Turn clips off, and placeholders", chapter: "edit", area: "timeline", focus: [0, 60, 60, 40],
      where: ["Right-click a clip", "Turn off"], image: "timeline-handling",
      summary: "Hide a clip without deleting it: it stays in place, faded and marked Off, but is neither seen, heard nor exported. Placeholders stand in for shots you have not got yet.",
      steps: ["Select a clip and press ⇧E, or right-click it and choose Turn off (Turn on brings it back).", "Right-click empty timeline space and choose Add a placeholder here for a five-second grey stand-in.", "Later, right-click the placeholder and use Replace with to put the real shot in its place."],
      keys: ["⇧E"]
    },
    {
      id: "edit-navigation", title: "Extend edits, jump between them, select from the playhead", chapter: "edit", area: "timeline", focus: [0, 60, 60, 40],
      where: ["Timeline", "Keys and right-click menus"], image: "timeline-handling",
      summary: "Move the nearer edge of a clip to the playhead, step from cut to cut, and select everything after or before the playhead.",
      steps: ["Select a clip, put the playhead where its edge should be and press ⇧X; when another clip touches that edge, the cut rolls.", "Press ↓ for the next edit and ↑ for the previous one.", "Press ⌥⇧→ to select every clip from the playhead on, ⌥⇧← for those before it."],
      keys: ["⇧X", "↓", "↑", "⌥⇧→", "⌥⇧←"]
    },
    {
      id: "match-frame", title: "Match frame", chapter: "edit", area: "menus", focus: [52, 20, 48, 50],
      where: ["Right-click a clip", "Match frame"], image: "timeline-handling",
      summary: "Open the media under the playhead in the source monitor at exactly the same frame, to take more of it.",
      steps: ["Put the playhead over a clip.", "Press ⇧F, or right-click the clip and choose Match frame.", "The source monitor opens at that frame; mark In and Out and place it as usual."],
      keys: ["⇧F"]
    },
    {
      id: "auto-ripple", title: "Auto ripple and track handling", chapter: "edit", area: "timeline", focus: [0, 60, 60, 40],
      where: ["Timeline", "Toolbar", "Magnet"], image: "timeline-handling",
      summary: "Make deleting close the gap on every track, move tracks up and down, lock them all at once, and clear every transition or marker.",
      steps: ["Click the magnet in the timeline toolbar to turn auto ripple on or off.", "Right-click a track header for Move up, Move down, Lock every track and Unlock every track.", "Right-click empty timeline space for Delete every transition… or Delete every marker… (it asks first)."]
    },
    {
      id: "rotate-flip", title: "Rotate and flip", chapter: "motion", area: "menus", focus: [52, 20, 48, 50],
      where: ["Right-click a clip", "Rotate and flip"], image: "timeline-handling",
      summary: "Turn a picture by right angles, or mirror it left to right or upside down.",
      steps: ["Right-click a video or picture clip and open Rotate and flip.", "Choose Rotate 90° clockwise, anticlockwise or 180°; its rotation keyframes turn with it.", "Choose Flip left to right or Flip upside down; choose it again to undo the flip."]
    },
    {
      id: "frame-border", title: "Frame border and snapping", chapter: "motion", area: "player",
      where: ["Player", "Guides", "Frame border"], image: "frame-border",
      summary: "A thin line round what is exported. Pictures you move or scale snap to it, and the edge they reach lights up with a little shake and a tap on a Force Touch trackpad.",
      steps: ["Select a clip and drag its picture, or a corner, in the player.", "Near an edge it snaps onto the border; that edge lights up.", "Turn the border or the snapping off in the Guides menu under the player."]
    },
    {
      id: "default-transition", title: "The default transition", chapter: "effects", area: "library",
      where: ["Library", "Transitions", "Right-click a transition"], image: "transitions",
      summary: "Choose the transition ⌘T adds, and how long new transitions last.",
      steps: ["Right-click a transition in the Transitions tab and choose Make it the default; it shows a Default badge.", "Set New ones last (0.1 to 10 seconds) at the top of the tab.", "Press ⌘T to put the default on the cut nearest the playhead."],
      keys: ["⌘T"]
    },

    // ---------- Placement and animation ----------
    {
      id: "transform", title: "Place clips in the player", chapter: "motion", area: "player",
      where: ["Player"], image: "transform",
      summary: "Select a clip and its frame shows in the player: move, resize and turn it there directly.",
      steps: ["Click a picture in the player, or the clip on the timeline, to select it.", "Drag inside the frame to move it (it snaps to the centre lines).", "Drag a corner to resize it, or the round knob to turn it (⇧ for 15° steps).", "Fine-tune position, scale, rotation, crop, opacity and blend mode in Inspector › Video."]
    },
    {
      id: "keyframes", title: "Keyframes", chapter: "motion", area: "inspector",
      where: ["Inspector", "Video", "Keyframes"], image: "keyframes",
      summary: "Animate position, scale, rotation, opacity, crop and volume over time.",
      steps: ["Move the playhead and click Keyframes › Add keyframe.", "Move the playhead again and change a value; the clip animates between the two.", "Click ◆ beside one value to animate just that one.", "Pick an easing per keyframe: linear, ease in, ease out, ease in and out, or hold."]
    },
    {
      id: "curves", title: "Custom easing curves", chapter: "motion", area: "inspector",
      where: ["Inspector", "Video", "Keyframes", "Custom curve"], image: "curves",
      summary: "Draw exactly how a value travels to its next keyframe, with overshoot if you like.",
      steps: ["Select a keyframe and choose Custom curve (or drag a handle in the curve editor).", "Shape the curve: time runs left to right, progress bottom to top.", "One click gives Overshoot, Anticipate or Snap."]
    },
    {
      id: "corner-pin", title: "Anchor point and corner pin", chapter: "motion", area: "inspector",
      where: ["Inspector", "Video", "Transform"], image: "corner-pin",
      summary: "Set what a clip turns about, and stretch its corners in perspective, onto a screen in the shot.",
      steps: ["Set the anchor point under Transform, or ⌥-drag the cross in the player.", "Under Corner pin, turn on Drag the corners in the player.", "Drag each corner to where it belongs."]
    },
    {
      id: "animation", title: "Animation, pan and zoom, motion blur", chapter: "motion", area: "inspector",
      where: ["Inspector", "Video", "Animation"], image: "motion-blur",
      summary: "Ready-made ways in, out and through for videos and photos, a slow pan and zoom, and motion blur where a clip moves.",
      steps: ["Under Animation, pick an in, out or through animation: fades, slides, spins, pops, pulse or swing.", "Use Pan and zoom to move from one framing to another over the whole clip.", "Turn on motion blur to smear the clip where its keyframes move it."],
      note: "Both are written as keyframes, so you can change them afterwards."
    },
    {
      id: "colour-clip", title: "Colour clips and frame grabs", chapter: "motion", area: "player",
      where: ["Library", "Colour-clip button"], image: "colour-clip",
      summary: "Add a clip of one colour or a gradient, or save the frame at the playhead as a picture.",
      steps: ["Click the colour-clip button in the library to add a colour or gradient clip.", "Click the camera button under the player to save the current frame to the library."]
    },
    {
      id: "guides", title: "Guides and player zoom", chapter: "motion", area: "player",
      where: ["Player", "Grid button"], image: "effect-mask",
      summary: "Thirds, centre lines, safe areas and rulers over the picture, and a zoom from Fit to 400%.",
      steps: ["Click the grid button under the player and choose the guides to show.", "Use the zoom menu beside it to go from Fit up to 400%."]
    },

    // ---------- Speed ----------
    {
      id: "speed", title: "Speed, reverse and freeze frames", chapter: "speed", area: "inspector",
      where: ["Inspector", "Speed"], image: "speed",
      summary: "Play a clip from 0.01× to 100×, backwards, or hold a frame.",
      steps: ["Select a clip and open the Speed tab.", "Drag the slider, or click 0.5×, 1×, 2× or 4×; later clips move with it.", "Turn on Reverse to play it backwards, sound too.", "Click Freeze frame at playhead to insert a 2-second still."],
      note: "Played faster or slower, sound keeps its pitch; switch Keep pitch off for a tape-style sound."
    },
    {
      id: "speed-ramp", title: "Speed ramps", chapter: "speed", area: "inspector",
      where: ["Inspector", "Speed", "Ramp"], image: "speed-ramp",
      summary: "Change speed through a clip: ramp up, flash in, bullet time, hero moment and more.",
      steps: ["Pick a ramp: Ramp up, Ramp down, Flash in, Flash out, Bullet time, Hero moment, Montage or Jumper.", "Drag points on the graph up or down (inner ones sideways too); double-click to add one.", "Click Remove ramp to go back to a constant speed."]
    },

    // ---------- Colour ----------
    {
      id: "grade", title: "Grade a clip", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color"], image: "color",
      summary: "Exposure, contrast, saturation, temperature and tint, then lift, gamma, gain and offset wheels, graded on the GPU before the clip's effects.",
      steps: ["Select a video clip and open the Color tab.", "Set the Basic sliders.", "Drag a wheel's puck towards a hue to push shadows, midtones or highlights that way (double-click centres it).", "Copy and Paste move a grade to other clips; Presets save it by name."]
    },
    {
      id: "curves-lut", title: "Curves and LUTs", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "Curves · LUT"], image: "color-curves",
      summary: "Master, red, green and blue tone curves, and your own .cube or .3dl LUTs with an intensity.",
      steps: ["Drag points on a curve; double-click to add or remove one.", "Under LUT, choose a .cube or .3dl file (or a built-in one) and set its intensity."]
    },
    {
      id: "hsl", title: "HSL and hue curves", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "HSL"], image: "hsl",
      summary: "Change one colour without touching the others: eight hue bands and three hue curves.",
      steps: ["Pick a band: red, orange, yellow, green, aqua, blue, purple or magenta.", "Turn its hue, saturation and luminance.", "Or use the hue curves: saturation by hue, hue by hue, and saturation by brightness."]
    },
    {
      id: "scopes", title: "Scopes", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "Scopes"], image: "scopes",
      summary: "Waveform, RGB parade, vectorscope with skin-tone line, and histogram, measured from the preview.",
      steps: ["Open the Color tab; the scopes sit at the top.", "Switch between waveform, parade, vectorscope and histogram."]
    },
    {
      id: "auto-colour", title: "Auto colour and Match", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "Auto colour"], image: "auto-colour",
      summary: "Take a cast off a clip in one click, or make its colours like another clip's.",
      steps: ["Click Auto colour to remove a cast and fill the levels.", "Click Match and choose another clip to match its colours.", "Both set the red, green and blue curves, so adjust them afterwards."]
    },
    {
      id: "hdr-wheels", title: "HDR wheels and lens correction", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "HDR wheels"], image: "hdr-wheels",
      summary: "Colour and brighten six tonal ranges separately, from the blacks to the specular highlights; straighten action-camera, phone, drone and fisheye lenses.",
      steps: ["Open HDR wheels on the Color tab and work range by range.", "For a curved lens, open Lens correction on the Video tab and pick the camera."]
    },
    {
      id: "looks", title: "Looks", chapter: "colour", area: "library",
      where: ["Library", "Filters"], image: "filters",
      summary: "Twelve original looks: Warm Film, Teal & Orange, Bleach Bypass, Golden Hour, Cool Night, Vintage Fade, Matte Pastel, Cross Process, Sepia, Noir, Vivid and Black & White.",
      steps: ["Select a clip and open the Filters tab.", "Click a look to apply it.", "Set its intensity, or remove it, in the Color tab's Look section."]
    },
    {
      id: "adjustment-layer", title: "Adjustment layers", chapter: "colour", area: "library",
      where: ["Library", "Filters", "Adjustment layer"], image: "adjustment",
      summary: "A clip with no picture of its own whose look, grade and effects change everything below it while it lasts.",
      steps: ["Click the first tile in the Filters tab to add one at the playhead.", "Give it a look, a grade or effects.", "Keyframe its opacity to fade it in or out."]
    },
    {
      id: "protect-skin", title: "Protect skin tones", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "Basic", "Protect skin tones"], image: "skin-protect",
      summary: "Keep faces natural under a strong look or grade: skin keeps the colour it had before them, by the amount you choose.",
      steps: ["Grade the clip, or give it a look.", "Open the Color tab and find Protect skin tones under Basic.", "Drag it up until faces look right; other colours keep the grade."],
      note: "On its own it changes nothing: it only holds skin back from the look and grade."
    },
    {
      id: "hdr", title: "HDR projects and the HDR viewer", chapter: "colour", area: "inspector",
      where: ["Inspector", "Color", "Project colour"], image: "colour-hdr",
      summary: "Work in SDR Rec.709, HDR PQ or HDR HLG; iPhone HLG and camera PQ footage is converted to match, and HEVC exports keep HDR.",
      steps: ["Choose HDR · PQ or HDR · HLG under Project colour at the top of the Color tab.", "Click HDR viewer in the transport bar to see the picture as HDR on an XDR display."]
    },
    {
      id: "sphere-360", title: "360° video", chapter: "colour", area: "inspector",
      where: ["Inspector", "Video", "360°"], image: "sphere-360",
      summary: "Reframe 360° footage into a flat video, or edit a whole 360° project and export it for 360° players.",
      steps: ["In a flat project, set Direction, Tilt, Roll and Field of view under 360° (each can be keyframed).", "Or drag in the player to look around and scroll to zoom.", "Put 360° footage in an empty project to make the project 360° itself."]
    },

    // ---------- Effects, masks and transitions ----------
    {
      id: "effects", title: "Effects library", chapter: "effects", area: "library",
      where: ["Library", "Effects"], image: "effects-library",
      summary: "61 original effects in blur, colour, stylize, distort, glitch, light and frame categories, previewed on the GPU.",
      steps: ["Select a clip and open the Effects tab in the library.", "Filter by category chips or search; previews animate on hover.", "Click an effect to add it; its settings appear in Inspector › Effects."]
    },
    {
      id: "frame-effects", title: "Shadows, outlines and fills", chapter: "effects", area: "library",
      where: ["Library", "Effects", "Frame"], image: "frame-effects",
      summary: "Drop Shadow, Outline, Outer Glow, Background Fill and Rounded Corners work on the placed clip, beyond its own edges.",
      steps: ["Place a clip smaller than the frame (picture in picture).", "Add a Frame effect; its shadow falls on what is below.", "Background Fill covers the frame around it with its own blurred picture or a colour."]
    },
    {
      id: "effect-keyframes", title: "Effect settings and keyframes", chapter: "effects", area: "inspector",
      where: ["Inspector", "Effects"], image: "effect-keyframes",
      summary: "Each clip's effect stack, with on/off, order and every numeric setting keyframable.",
      steps: ["Open the Effects tab in the inspector.", "Change a setting; click ◆ beside it to keyframe that parameter.", "Reorder or remove effects; their keyframes travel with them."]
    },
    {
      id: "masks", title: "Masks", chapter: "effects", area: "inspector",
      where: ["Inspector", "Video", "Masks"], image: "masks",
      summary: "Rectangle, ellipse, line, star, heart and pen masks, up to eight per clip, on the picture, its effects only, or its colour only.",
      steps: ["Add a mask shape in the Masks section.", "Drag its centre, square handle and knob in the player; a pen mask's points drag one by one.", "Set feather, roundness and Invert; choose whether it applies to the picture, the effects or the colour."]
    },
    {
      id: "chroma-key", title: "Chroma key and track mattes", chapter: "effects", area: "inspector",
      where: ["Inspector", "Video", "Chroma key"], image: "chroma-key",
      summary: "Remove a green or blue background, or shape a clip by the clip above it.",
      steps: ["Under Chroma key, choose Green, Blue, or Pick and click the player.", "Set tolerance, edge softness and spill removal.", "Or use Track matte to shape a clip by the one above it, by alpha or luma."]
    },
    {
      id: "split-screen", title: "Split screens and picture in picture", chapter: "effects", area: "inspector",
      where: ["Inspector", "Video", "Split screen"], image: "split-screen",
      summary: "Arrange the clips at the playhead in one step: picture in picture, side by side, top and bottom, three columns, one and two, or a grid of four.",
      steps: ["Stack the clips on tracks at the playhead.", "Pick a layout under Split screen; each clip is cropped to fill its part.", "One undo puts them all back."]
    },
    {
      id: "stickers", title: "Stickers", chapter: "effects", area: "library",
      where: ["Library", "Stickers"], image: "stickers",
      summary: "100 original animated stickers: 25 designs, each pulsing, spinning, bouncing or wobbling. Your own Lottie .json files import the same way.",
      steps: ["Open the Stickers tab; previews play on hover.", "Click one to put it at the playhead, small and in the middle, on a free track.", "Place, grade, mask and give it effects like any picture."]
    },
    {
      id: "alpha-video", title: "Video with transparency", chapter: "effects", area: "library",
      where: ["Library", "Media"], image: "alpha-video",
      summary: "ProRes 4444, WebM VP9 with alpha, HEVC with alpha, GIF and animated PNG or WebP keep their transparency and play over what is below.",
      steps: ["Import the file like any video.", "Put it on a track above another clip."]
    },
    {
      id: "transitions", title: "Transitions", chapter: "effects", area: "library",
      where: ["Library", "Transitions"], image: "transitions",
      summary: "60 transitions in basic, slide, wipe, shape, zoom, 3D, distort, luma, stylize, blur and light categories, with constant-power audio crossfades.",
      steps: ["Click a transition to put it on the cut nearest the playhead, or drag it onto a cut.", "Click its marker over the cut to set duration, position and settings in the inspector.", "⌫ removes the selected transition."]
    },
    {
      id: "transitions-all", title: "Transitions on every cut", chapter: "effects", area: "library",
      where: ["Library", "Transitions", "Random on every cut"], image: "transitions-3d",
      summary: "Put one transition on every cut, or a different random one on each.",
      steps: ["Select a transition marker and click Apply to all cuts in the inspector.", "Or click Random on every cut in the Transitions tab.", "Either is one undo step."]
    },
    {
      id: "overlays", title: "Overlay transitions", chapter: "effects", area: "library",
      where: ["Library", "Transitions", "Overlays"], image: "overlays",
      summary: "Warm and cool light leaks, a film burn, and white and black paint strokes that go over a cut on a track above.",
      steps: ["Move the playhead near a cut.", "Click an overlay under Overlays in the Transitions tab."]
    },
    {
      id: "video-plugins", title: "Video plug-ins (OpenFX)", chapter: "effects", area: "inspector",
      where: ["Inspector", "Effects", "Plug-in · name"], image: "video-plugins",
      summary: "OpenFX effects installed on your Mac appear in the effect picker, with their settings as the app's own controls.",
      steps: ["Install OpenFX plug-ins in /Library/OFX/Plugins.", "Pick Plug-in · name in the Effects tab's picker.", "Change and keyframe its settings like any effect."]
    },
    {
      id: "drawing", title: "Draw shapes and paths", chapter: "effects", area: "player",
      where: ["Player", "Draw (brush button)"], image: "drawing",
      summary: "Draw with a brush, lines, arrows, rectangles and ellipses straight onto the picture, animated on and off.",
      steps: ["Click the brush button under the player.", "Pick a tool, colour, width and fill, and draw; it becomes a drawing clip at the playhead.", "Set how it draws on and takes off in its inspector.", "Any clip can follow a drawn path (Analyze tab), and a title can run on one."]
    },

    // ---------- Titles and graphics ----------
    {
      id: "titles", title: "Title templates", chapter: "titles", area: "library",
      where: ["Library", "Titles"], image: "titles-library",
      summary: "51 original title templates in titles, lower thirds, callouts, credits and animated categories, plus six logo reveals and your own presets.",
      steps: ["Open the Titles tab; animated previews play on hover.", "Click a style to add a 5-second title at the playhead, or drag it onto a track.", "Logo reveals ask for your logo and animate it in with a tagline."]
    },
    {
      id: "title-text", title: "Edit a title", chapter: "titles", area: "inspector",
      where: ["Inspector", "Video", "Text"], image: "titles",
      summary: "Words, font, size, bold and italic, alignment, colour, spacing, outline and shadow, and a shape or speech bubble behind the text.",
      steps: ["Select the title clip.", "Type in the Text section; it updates when you pause.", "Add a shape behind it under Shape: box, pill, speech or thought bubble, banner and more."]
    },
    {
      id: "title-motion", title: "Animate titles", chapter: "titles", area: "inspector",
      where: ["Inspector", "Video", "Animation"], image: "title-motion",
      summary: "In, Out and Loop animations moving the whole text, its lines, words or letters, plus rolling credits and tickers.",
      steps: ["Pick an In and an Out animation: fade, rise, slide, zoom, pop, typewriter and more.", "Pick a Loop: wave, pulse, flicker, shake or swing.", "Choose the unit (text, lines, words, letters), duration and stagger.", "Use Scroll for rolling credits or a news ticker."]
    },
    {
      id: "text-styles", title: "Text styles, presets and music", chapter: "titles", area: "inspector",
      where: ["Inspector", "Video", "Fill · 3D · Title presets"], image: "text-styles",
      summary: "Underline, vertical text, curved lines, gradient or picture fills, 3D depth and tilt; save a look as a preset; make a title move with the music.",
      steps: ["Bend the text with Curve; set gradient or picture fills under Fill; add depth under 3D.", "Save the look by name under Title presets; it appears in the Titles library under My presets.", "Under Move with the music, make it pulse, bounce, flash or shake on the beat."]
    },
    {
      id: "charts", title: "Animated charts", chapter: "titles", area: "inspector",
      where: ["Inspector", "Video", "Chart"], image: "charts",
      summary: "Turn a title into bars or a line that grow in, from typed values or a CSV file.",
      steps: ["Select a title and choose Make it a chart.", "Type a label and a value on each line, or load a CSV or text file.", "Pick Bars or Line, a colour, and whether to show the values."]
    },
    {
      id: "visualiser", title: "Audio visualisers", chapter: "titles", area: "library",
      where: ["Library", "Titles", "Visualisers"], image: "visualiser",
      summary: "Spectrum Bars, Mirror Bars, Sound Wave and Ring that move with the music under them.",
      steps: ["Put the playhead on music.", "Click a visualiser under Visualisers in the Titles tab; it is added for the length of the music.", "Change style, colours and size in the Visualiser section of the Video tab."]
    },

    // ---------- Audio ----------
    {
      id: "audio-library", title: "Music and sound effects", chapter: "audio", area: "library",
      where: ["Library", "Audio"], image: "audio",
      summary: "12 original music pieces in six moods and 15 sound effects, all made by the app's own synthesiser and free to use, plus your own music folders and sound packs.",
      steps: ["Open the Audio tab and pick Music or Sound effects.", "Click ▶ to preview.", "Click + (or double-click) to put it at the playhead on an audio track.", "My music shows the audio files in folders you add."]
    },
    {
      id: "clip-audio", title: "Volume, fades and detach", chapter: "audio", area: "inspector",
      where: ["Inspector", "Audio"], image: "batch-gain",
      summary: "Set a clip's volume and fades, detach a video's sound, and change many clips at once.",
      steps: ["Drag the round knobs at a clip's top corners to set fades, or use the Audio tab.", "Click Detach audio to move a video's sound to an audio track.", "With several clips selected, turn them all up or down, set one volume, or Normalize each."]
    },
    {
      id: "fit-music", title: "Fit music to a length", chapter: "audio", area: "inspector", where: ["Inspector", "Audio", "Fit to length"], image: "fit-music", privacy: "mac",
      summary: "Make a song last exactly as long as your video: whole bars come out of the middle, or play again, where the music sounds alike.",
      steps: ["Select the music on an audio track and open the Audio tab.", "Type the length in seconds, or click To the video's end.", "Click Fit. The song becomes grouped pieces; one undo puts it back."],
      note: "A small difference is made up by playing it up to 4 % faster or slower with the pitch kept, or by fading the end."
    },
    {
      id: "audio-effects", title: "Audio effects and loudness", chapter: "audio", area: "inspector",
      where: ["Inspector", "Audio", "Audio effects"], image: "audio-effects",
      summary: "45 original audio effects: EQ, dynamics, rooms, echoes, modulation, pitch, voices and clean-up; Normalize to −14, −16 or −23 LUFS.",
      steps: ["Pick an effect in the Audio effects section; stack as many as you need.", "Change its settings on its card; any can be keyframed.", "Click Normalize in the Loudness section to set the clip to a loudness."]
    },
    {
      id: "mixer", title: "Mixer", chapter: "audio", area: "timeline", focus: [10, 22, 50, 62],
      where: ["Timeline", "Toolbar", "Mixer"], image: "mixer",
      summary: "A strip per track with volume, balance, mute, solo and a meter, and the master with loudness readings.",
      steps: ["Click the Mixer button in the timeline toolbar.", "Adjust each track's strip; FX adds effects to a whole track."]
    },
    {
      id: "audio-plugins", title: "Audio plug-ins (AU and VST3)", chapter: "audio", area: "inspector",
      where: ["Inspector", "Audio", "Plug-in · name"], image: "plugins",
      summary: "Audio Units and VST3 effects installed on your Mac work like any audio effect, on clips or whole tracks.",
      steps: ["Pick Plug-in · name (maker, AU/VST3) in the audio effect picker.", "Change its settings on its card, or click Window for its own window."],
      note: "Each VST3 is checked once in a separate process, so one that crashes can't take the app with it."
    },
    {
      id: "voiceover", title: "Voice-over", chapter: "audio", area: "timeline",
      where: ["Timeline", "Toolbar", "Microphone"], image: "voiceover",
      summary: "Record narration from your microphone at the playhead while the timeline plays.",
      steps: ["Click the microphone in the timeline toolbar.", "Pick the microphone and check its level.", "Press the red button; after an optional count-in, recording starts.", "Press it again, or Space, to stop; the take lands where it started."]
    },

    {
      id: "loop-skim", title: "Loop and skimming", chapter: "speed", area: "player",
      where: ["Player", "Loop", "and Timeline", "Skimming"], image: "timeline-handling", focus: [20, 0, 62, 70],
      summary: "Play round and round, or see the frame under the pointer as it moves over the timeline without moving the playhead.",
      steps: ["Click the loop button in the transport, or press ⌘L; with In and Out marked, it loops between them.", "Click the skimming button in the timeline toolbar, or press ⇧S.", "Move the pointer over the timeline: the player follows it, and goes back to the playhead's frame when the pointer leaves."],
      keys: ["⌘L", "⇧S"]
    },

    // ---------- Recording ----------
    {
      id: "recording", title: "Record the screen and camera", chapter: "record", area: "timeline",
      where: ["Timeline", "Toolbar", "Screen"], image: "recording",
      summary: "Record a display or a window with its sound, your camera and your microphone, straight onto new tracks.",
      steps: ["Click the screen button in the timeline toolbar.", "Choose a display or window, a camera, and the microphone; for the camera, its Background: as it is, blurred or removed.", "Click Record, then Stop; screen, camera (picture in picture) and microphone land on new tracks."],
      note: "The Analyze tab's Cursor section can add click rings, a spotlight, zoom on clicks and click sounds afterwards."
    },
    {
      id: "teleprompter", title: "Teleprompter", chapter: "record", area: "timeline",
      where: ["Recorder or voice-over panel", "Teleprompter…"], image: "teleprompter",
      summary: "Read your script in a window that stays on top and is left out of screen recordings.",
      steps: ["Click Teleprompter… in the recorder or the voice-over panel.", "Click Edit to type or paste the script; set speed and size, or mirror it.", "With recording on, it scrolls when a recording starts. Space plays, ↑ and ↓ change speed."]
    },
    {
      id: "live-drawing", title: "Draw on the screen while recording", chapter: "record", area: "timeline",
      where: ["Recorder", "Draw on the screen while recording"], image: "live-drawing",
      summary: "Draw with a pen, highlighter or arrow over the display you are recording; the strokes are recorded too.",
      steps: ["Turn on Draw on the screen while recording, or press Draw during a recording.", "Pick a tool, one of five inks and a width from the small bar.", "D turns drawing on and off, C clears, Esc stops drawing."]
    },
    {
      id: "record-area", title: "Record the entire screen, a window or an area", chapter: "record", area: "timeline", focus: [20, 0, 62, 66],
      where: ["Recorder", "Entire screen · A window · An area"], image: "record-area",
      summary: "Choose exactly what to record: a whole display, one window, or a rectangle you draw and size on the display. A red frame marks it while recording, left out of the recording.",
      steps: ["Open the recorder from the screen button in the timeline toolbar.", "Choose Entire screen, A window (then the window), or An area.", "For an area, drag to move it, drag its edges or corners to size it, or use Whole display, 16:9, 9:16 or 1:1; click Use this area (or press Return).", "Click Record; the red frame shows what is being recorded until you stop."]
    },

    // ---------- On your Mac ----------
    {
      id: "captions", title: "Captions from speech", chapter: "local", area: "library", where: ["Library", "Captions"], image: "captions", privacy: "mac",
      summary: "Turn what is said in your clips into timed captions with Whisper, on this Mac. Import and export SRT or VTT.",
      steps: ["Open the Captions tab and download a Whisper model once (Base, Small or Large v3 Turbo).", "Click Make captions; they appear on a Captions track as title clips.", "Pick a Style: Highlight, Pop, Karaoke or Word by word."]
    },
    {
      id: "transcript", title: "Edit by transcript", chapter: "local", area: "library", where: ["Library", "Transcript"], image: "transcript", privacy: "mac",
      summary: "Read what was said, and cut words, fillers and long pauses out of the clips by deleting text.",
      steps: ["Open the Transcript tab; click a word to go there.", "Shift-click to select a stretch and press Delete to cut it from the clips.", "Click Remove fillers or Shorten pauses."]
    },
    {
      id: "analyze", title: "Pauses, scenes and beats", chapter: "local", area: "inspector", where: ["Inspector", "Analyze"], image: "analyze", privacy: "mac",
      summary: "Find silent pauses, scene changes and the beat; remove pauses, split at scenes, mark beats, and lower music under speech.",
      steps: ["Select a clip and open the Analyze tab.", "Click Find under Pauses, Scene changes or Beat; results show on the timeline first.", "Click Remove them for pauses, Split there or Add markers for scenes, or mark every beat or every fourth."]
    },
    {
      id: "enhance", title: "Enhance speech", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Enhance speech"], image: "enhance", privacy: "mac",
      summary: "Take noise, hum and room echo out of voices with DeepFilterNet3, on this Mac.",
      steps: ["Click Enhance speech (the first time it downloads an 8 MB model).", "Set Lower noise by up to.", "Click Original sound to go back."]
    },
    {
      id: "people", title: "Portrait cutout, faces and auto reframe", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "People"], image: "people", privacy: "mac",
      summary: "Cut a person out of the background, cover faces that follow them, and turn landscape into 9:16, 1:1 or 4:5 keeping the subject in view.",
      steps: ["Use Portrait cutout to make the background transparent.", "Click Find faces, choose the ones to hide, and Cover faces with Mosaic, Blur or Fill.", "Under Auto reframe, pick 9:16, 1:1 or 4:5."]
    },
    {
      id: "beauty", title: "Beauty filter", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Beauty"], image: "beauty", privacy: "mac",
      summary: "Smooth and brighten the skin on every face, and whiten teeth, found afresh in each frame. Eyes, brows and lips stay sharp.",
      steps: ["Select a video clip and open the Analyze tab.", "Turn on Beauty.", "Set Smooth skin, Brighten skin and Whiten teeth."]
    },
    {
      id: "background-blur", title: "Background blur", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Cutout", "Blurred"], image: "background-blur", privacy: "mac",
      summary: "Keep the person sharp and blur everything behind them, found afresh in every frame.",
      steps: ["Select a video clip and open the Analyze tab.", "Turn on Cut out the person.", "Under What is cut away, choose Blurred and set the Blur amount."]
    },
    {
      id: "face-stickers", title: "Stickers that follow faces", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Faces", "Stick it on"], image: "face-stickers", privacy: "mac",
      summary: "Put a sticker on each face (on the head, over the eyes or the face, or beside it) that moves and grows with the face.",
      steps: ["Select a video clip and click Find faces in the Analyze tab.", "Choose the faces, a sticker and where it goes: Head, Eyes, Face or Beside.", "Click Stick it on. Make them tracks lets titles follow the faces too."]
    },
    {
      id: "night-view", title: "Night view", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Night view"], image: "night-view", privacy: "mac",
      summary: "Lift a dark shot like a phone's night mode: the shadows come up, the highlights stay, and the grain is smoothed first.",
      steps: ["Select a video clip and open the Analyze tab.", "Turn on Night view.", "Set Lift for how much brighter, and Smooth grain for how much grain to take away."]
    },
    {
      id: "depth-of-field", title: "Depth of field", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Depth of field"], image: "depth-of-field", privacy: "mac",
      summary: "Blur what is in front of or behind your subject, like a lens wide open, using the depth a model finds in every frame.",
      steps: ["Select a video clip and open the Analyze tab.", "Click Download the depth model the first time (about 50 MB, once), then turn on Depth of field.", "Set Blur and In focus; focus on a face (or the nearest thing), or choose A point."],
      note: "The depth model runs on your Mac; nothing is sent anywhere."
    },
    {
      id: "smart-search", title: "Smart search", chapter: "local", area: "library", where: ["Library", "Media", "Search"], image: "smart-search", privacy: "mac",
      summary: "Find the moments in your media by what is in the picture, what is said and the text on screen, not just by file name.",
      steps: ["In the Media tab, start typing in the search field and click Look at media once; your Mac labels every picture and video and reads the text on screen.", "Type in the search field: each moment shows whether it was Seen, Said or On screen.", "Click a moment to open it in the source monitor with In and Out around it, or double-click to put it on the timeline."],
      note: "What is said is searched in media you have transcribed. Labels are in English whatever the app's language."
    },
    {
      id: "motion-analysis", title: "Stabilize, flicker, denoise and tracking", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Steady and clean"], image: "motion", privacy: "mac",
      summary: "Smooth shaky footage, remove flicker and grain, and track an object or a surface so titles and stickers can follow it.",
      steps: ["Click Stabilize (or Lock the camera) under Steady and clean.", "Use Remove flicker and Denoise over time as needed.", "Under Track motion, drag a box around something, then Track object or Track surface; other clips can Follow it."]
    },
    {
      id: "vision", title: "Upscale, slow motion and object removal", chapter: "local", area: "inspector", where: ["Inspector", "Analyze", "Picture"], image: "vision", privacy: "mac",
      summary: "Upscale 2× or 4×, make frames for smooth slow motion, line clips up by sound, cut out any subject, or remove an object.",
      steps: ["Under Picture, click Upscale, Make frames, or Remove an object and draw around it.", "Under Cutout, Draw around a subject, then Cut it out.", "Select two or more clips and click Line up by sound to sync them."]
    },

    // ---------- Your keys ----------
    {
      id: "ai-settings", title: "AI settings and your keys", chapter: "cloud", area: "topbar", where: ["Top bar", "Speech bubble"], image: "ai-settings", privacy: "key",
      summary: "Paste your own Claude, ElevenLabs, OpenAI and Google AI keys. They are kept in your Mac's Keychain, never in projects.",
      steps: ["Click the speech bubble in the top bar.", "Paste each key you want to use.", "The dialog shows this session's tokens, an estimated cost and the characters spoken."],
      note: "What you use a key on is sent to that provider and billed to your account."
    },
    {
      id: "ai-write", title: "Write with AI and translate", chapter: "cloud", area: "library", where: ["Library", "Transcript", "Write with AI…"], image: "ai-write", privacy: "key",
      summary: "Claude writes five titles, a description or a clean script from what is said, and translates captions keeping their timing.",
      steps: ["In the Transcript tab, click Write with AI… and pick Titles, Description or Script.", "Click Write; the draft streams in. Add a title to the timeline from the list.", "In the Captions tab, click Translate and pick a language."]
    },
    {
      id: "ai-voice", title: "Voices", chapter: "cloud", area: "library", where: ["Library", "Audio", "Voice"], image: "ai-voice", privacy: "key",
      summary: "Speak a text with one of your Mac's voices (offline) or an ElevenLabs voice, with captions timed to the words.",
      steps: ["Open Audio › Voice and type the text.", "Pick a voice and click Listen.", "Click Add at the playhead."]
    },
    {
      id: "voice-clone", title: "Copy a voice, with consent", chapter: "cloud", area: "library", where: ["Library", "Audio", "Voice", "Copy a voice, with consent…"], image: "voice-clone", privacy: "key",
      summary: "Make a copy of a voice for narration, only of the person reading, 18 or older, who agrees.",
      steps: ["Click Copy a voice, with consent….", "The person reads the consent statement written for them into the microphone.", "Only a recording checked on this Mac to say the statement is sent to ElevenLabs."]
    },
    {
      id: "copilot", title: "Copilot", chapter: "cloud", area: "library", where: ["Library", "Copilot"], image: "copilot", privacy: "key",
      summary: "Edit by asking: \"cut it to the best 30 seconds\", \"cross-fade between every shot\". Claude lists what it would change; nothing happens until you apply it.",
      steps: ["Open the Copilot tab and type what you want.", "Read the list of changes.", "Click Apply; it is one step you can undo."],
      note: "Each request sends a description of the project (clip names, timing, titles, what is said), not the pictures or sound."
    },
    {
      id: "generate", title: "Generate pictures, video, music and sounds", chapter: "cloud", area: "library", where: ["Library", "Generate"], image: "generate", privacy: "key",
      summary: "Make a picture or sticker, a video, music or a sound effect from a description, with OpenAI, Google Gemini or ElevenLabs.",
      steps: ["Pick Picture, Sticker, Video, Music or Sound effect.", "Describe it and choose shape, quality or length.", "Click Generate; results go into the media bin or at the playhead, marked AI."],
      note: "What you may do with the results is set by each provider's terms, summarised under the Generate button."
    },
    {
      id: "ai-transition", title: "AI transitions", chapter: "cloud", area: "library", where: ["Library", "Generate", "Video", "Between two clips"], image: "ai-transition", privacy: "key",
      summary: "Google makes a shot that turns one clip into the next, from the last frame before a cut to the first after it.",
      steps: ["Move the playhead near a cut between two picture clips.", "In the Generate tab, pick Video, then Between two clips; say how it should move if you like.", "Click Make the transition. It goes between the clips and what comes later moves along."],
      note: "Only those two frames are sent to Google."
    },
    {
      id: "idea-video", title: "A video from an idea", chapter: "cloud", area: "library", where: ["Library", "Generate", "From an idea"], image: "idea-video", privacy: "key",
      summary: "Describe an idea: Claude plans a few shots with captions, you change what you like, and Google makes each shot.",
      steps: ["In the Generate tab, pick From an idea, describe it and choose how many shots.", "Click Plan it, then read and change each shot's description and caption.", "Click Make the video; the shots go after your last clip with their captions."],
      note: "Needs both a Claude key and a Google key. If a shot fails, the ones already made stay in the media bin."
    },
    {
      id: "image-prompt", title: "A prompt from a picture", chapter: "cloud", area: "library", where: ["Library", "Generate", "Describe the frame"], image: "image-prompt", privacy: "key",
      summary: "Claude looks at the frame at the playhead and writes a prompt to make a picture, a sticker or a video like it.",
      steps: ["Move the playhead to the frame you like.", "In the Generate tab, pick Picture, Sticker or Video.", "Click Describe the frame; the prompt appears in the box, ready to change or generate."],
      note: "Only that one frame is sent to Anthropic."
    },
    {
      id: "proofread", title: "Check spelling and grammar", chapter: "cloud", area: "library", where: ["Library", "Captions", "Check the text"], image: "proofread", privacy: "key",
      summary: "Claude proofreads every title and caption in its own language and lists what it would fix. Nothing changes until you apply it.",
      steps: ["Open the Captions tab and click Check the text.", "Read each fix: the text before is struck through, the corrected text below it.", "Untick any you don't want and click Apply; it is one step you can undo."]
    },

    // ---------- Templates and automatic edits ----------
    {
      id: "templates", title: "Templates", chapter: "auto", area: "library", where: ["Library", "Templates"], image: "templates",
      summary: "Eight original templates (Postcard, Birthday, Launch, Story drop, Memories, Recipe, Game day, Slideshow) with cuts on the beat.",
      steps: ["Open the Templates tab; previews play when you point at them.", "Choose one and pick its shape: 16:9, 9:16, 1:1, 4:3, 3:4 or 21:9.", "Click Use template to start a new project from it."]
    },
    {
      id: "storyboard", title: "Fill a template's storyboard", chapter: "auto", area: "library", where: ["Library", "Templates", "Storyboard"], image: "template-mode",
      summary: "Choose what goes in each slot, change the words and the music, and save your own projects as templates.",
      steps: ["Click Choose on a slot and pick media from the bin, or import a file.", "Move where a video starts, change the words, pick other music.", "Save as template… turns your own project into one."]
    },
    {
      id: "auto-edit", title: "Instant video and highlights", chapter: "auto", area: "library", where: ["Library", "Templates", "Make it for me"], image: "auto-edit",
      summary: "Give it pictures and videos and a style, and it makes an edit cut to the beat, of about the length you choose.",
      steps: ["Click Instant video (or Highlights for videos only).", "Pick media, a style (Cinematic, Upbeat, Calm, Energetic, Vintage or Clean), a shape and a length.", "Click Make it; the result is a new project you can keep editing."]
    },
    {
      id: "shorts", title: "Shorts from a long video", chapter: "auto", area: "library", where: ["Library", "Templates", "Shorts"], image: "shorts",
      summary: "Make up to five tall shorts of a long video's best moments, each following the subject.",
      steps: ["Click Shorts and pick one long video.", "Choose how many, and turn on captions if it has a transcript.", "Click Make it, then Export each short…."]
    },
    {
      id: "store", title: "Store", chapter: "auto", area: "library", where: ["Library", "Store"], image: "store",
      summary: "Install packs of templates, music and sounds, stickers, effects and transitions from a signed catalogue.",
      steps: ["Open the Store tab.", "Click Get, Update or Remove on a pack."],
      note: "No catalogue is published yet, so the list stays empty until one is."
    },

    // ---------- Projects and your setup ----------
    {
      id: "home", title: "Start a project", chapter: "setup", area: "home", where: ["Home"], image: "home",
      summary: "Pick a canvas shape, open a recent project, or start straight into recording or an instant video.",
      steps: ["Click a canvas shape: 16:9, 9:16, 1:1, 4:3 or 21:9.", "Or use Record screen or Instant mode to open a new project with that tool ready.", "⌘S saves a .tve project file."],
      keys: ["⌘S"]
    },
    {
      id: "autosave", title: "Autosave and recovery", chapter: "setup", area: "home", where: ["Home", "Unsaved work"], image: "home-recovery",
      summary: "Unsaved changes are written a few seconds after you stop editing; after a crash, Home offers to recover them.",
      steps: ["Watch for Autosaved hh:mm in the top bar.", "After a crash, find the project on Home under Unsaved work.", "Click Recover to open it, or Discard to delete the copy."]
    },
    {
      id: "languages", title: "English and Tamil", chapter: "setup", area: "home", where: ["Home", "Language"], image: "tamil-home",
      summary: "The app speaks English and Tamil (தமிழ்); it follows your Mac's language or the one you choose.",
      steps: ["Open the Language menu on the Home screen.", "Choose English or தமிழ்; the change shows at once, everywhere."]
    },
    {
      id: "shortcuts", title: "Keyboard shortcuts and presets", chapter: "setup", area: "topbar", where: ["Top bar", "Keyboard"], image: "shortcuts",
      summary: "See and change every shortcut, or switch the whole set to Premiere Pro-style or Final Cut Pro-style keys; export and import them as a file.",
      steps: ["Click the keyboard button in the top bar, or press ⌘/.", "Click a key and press the new one; Esc cancels.", "Pick a set under Keys: this app's own, Like Premiere Pro or Like Final Cut Pro.", "Export… saves every key to a file; Import… reads one back."],
      keys: ["⌘/"]
    },
    {
      id: "keyboard-map", title: "Keyboard map", chapter: "setup", area: "topbar", where: ["Top bar", "Keyboard", "Keyboard view"], image: "keyboard",
      summary: "A drawn keyboard showing which keys do something with the modifiers you choose, and what.",
      steps: ["Open the shortcuts and click Keyboard.", "Choose ⌘, ⇧, ⌥ or ⌃; the keys used with them are filled and named.", "Click a key to give its action another key."]
    },
    {
      id: "favourites", title: "Favourites and tags", chapter: "setup", area: "library", where: ["Library", "Effects · Transitions · Titles · Stickers · Filters"], image: "favourites",
      summary: "Star the effects, transitions, titles, stickers and looks you use most, and tag them.",
      steps: ["Point at a tile and click ☆ to keep it as a favourite.", "Click # to give it tags.", "Click the ★ chip to show only favourites; a tag chip narrows them."]
    },
    {
      id: "workspaces", title: "Workspaces and a second display", chapter: "setup", area: "topbar", focus: [0, 0, 100, 100], where: ["Top bar", "Workspace"], image: "editor",
      summary: "Lay the panels out for the work at hand, and show the picture full screen on another display.",
      steps: ["Click the workspace button: Editing, Organising, Colour, Audio or Big preview.", "Save this layout… keeps your own; the one in use comes back next time.", "Click the second-display button under the player; Space plays, ←/→ step, Esc closes."]
    },
    {
      id: "proxies", title: "Proxies for heavy media", chapter: "setup", area: "library", where: ["Library", "Media", "Make proxy"], image: "editor-proxies",
      summary: "Light 720p copies make 4K edit smoothly; exports always use the originals.",
      steps: ["Videos larger than 1080p get a proxy automatically.", "For any other video, point at its tile and click Make proxy.", "The proxy button in the player switches between proxies and originals."]
    },
    {
      id: "render-files", title: "Smooth playback with render files", chapter: "setup", area: "timeline", where: ["Timeline", "Toolbar", "Render"], image: "render-files",
      summary: "Stretches too heavy to play in real time show an orange bar; render them in the background and they play smoothly.",
      steps: ["Look for the orange bar along the ruler.", "Click Render in the timeline toolbar (⌃R); the bar turns blue.", "Or turn on half resolution in the player."],
      keys: ["⌃R"]
    },
    {
      id: "relink", title: "Missing media and relink", chapter: "setup", area: "topbar", where: ["Bar under the top bar", "Relink…"], image: "relink",
      summary: "When a project's files have moved, find them again by name or by content.",
      steps: ["Click Relink… in the bar naming the missing files.", "Click Search a folder… to find them all, or Locate… for one."]
    },
    {
      id: "interchange", title: "Work with other editors", chapter: "setup", area: "topbar", where: ["Top bar", "Export timeline"], image: "interchange",
      summary: "Send the cut to DaVinci Resolve, Premiere Pro, Final Cut Pro or Avid as OpenTimelineIO, Final Cut Pro XML or an EDL, and open their timelines here.",
      steps: ["Click Export timeline in the top bar and pick a format.", "To bring one in, use Open project… with an OpenTimelineIO or FCPXML file.", "A list says what didn't carry over."]
    },

    // ---------- Export ----------
    {
      id: "export", title: "Export a video", chapter: "export", area: "topbar", where: ["Top bar", "Export"], image: "export-dialog",
      summary: "Presets for the web, social apps, ProRes masters, GIFs and sound alone, or every setting by hand, with a size estimate.",
      steps: ["Click Export (⌘E).", "Pick a preset, or set format, size, frame rate, quality, sound and range.", "Click Export to watch the progress, or Add to queue to keep editing."],
      keys: ["⌘E"]
    },
    {
      id: "render-queue", title: "Render queue", chapter: "export", area: "topbar", focus: [55, 0, 45, 55], where: ["Top bar", "Render queue"], image: "render-queue",
      summary: "Exports run one after another in the background while you keep editing.",
      steps: ["Click the render-queue button in the top bar.", "Pause, resume, cancel, or Show in Finder."]
    },
    {
      id: "export-size", title: "Export to a file size, in several formats", chapter: "export", area: "topbar", where: ["Top bar", "Export", "Quality: File size"], image: "export-size",
      summary: "Aim an H.264 or HEVC file under a number of megabytes, and export more formats in one go.",
      steps: ["Set Quality to File size and type the megabytes.", "Click format chips under Also as (HEVC, ProRes, GIF, WAV, MP3…).", "Each becomes a job in the render queue."]
    },
    {
      id: "live-photo", title: "Live Photos", chapter: "export", area: "topbar", where: ["Top bar", "Export", "Live Photo"], image: "export-live-photo",
      summary: "A key photo and a 3-second movie, paired the way Photos pairs an iPhone's.",
      steps: ["Put the playhead on the key frame.", "Choose the Live Photo format in Export.", "Turn on Add to Photos to put it in your library."]
    },
    {
      id: "export-audio", title: "Sound only: WAV, FLAC and MP3", chapter: "export", area: "topbar", where: ["Top bar", "Export", "Format"], image: "export-mp3",
      summary: "Export the mix as AAC, 16-, 24- or 32-bit float WAV, FLAC or 320 kbit/s MP3, with chapters.",
      steps: ["Open Export and pick a sound format or preset.", "Choose the range and loudness normalization.", "Click Export."]
    }
  ],

  shortcuts: [
    { group: "Playback", rows: [["Space", "Play / pause"], ["← / →", "Previous / next frame"], ["Home / End", "Go to start / end"], ["J · K · L", "Shuttle backwards, stop, forwards (press again for 2×, 4×, 8×)"]] },
    { group: "Editing", rows: [["S", "Split the selected clip at the playhead"], ["⌫ / Delete", "Delete the selected clip"], ["⇧⌫", "Ripple delete"], ["B / V", "Blade / select"], ["I / O", "Mark In / Out"], ["⌥X", "Clear In and Out"], ["⌘Z / ⇧⌘Z", "Undo / redo"]] },
    { group: "Clipboard", rows: [["⌘X · ⌘C · ⌘V", "Cut, copy, paste at the playhead"], ["⌘D", "Duplicate"], ["⌘A", "Select all"], ["⇧⌘V", "Paste attributes"]] },
    { group: "Structure", rows: [["M", "Add a marker"], ["⌥← / ⌥→", "Previous / next marker"], ["⌘G / ⇧⌘G", "Group / ungroup"], ["⌥⌘G", "Make or break a compound clip"], ["1–9", "Cut to a multicam angle"], ["⇧-click / ⌘-click", "Add a clip to the selection, or take it out"]] },
    { group: "Project", rows: [["⌘S", "Save project"], ["⌘I", "Import media"], ["⌘E", "Export (or add to the render queue)"], ["⌃R", "Render heavy stretches"], ["⌘/", "Keyboard shortcuts"]] }
  ]
};
