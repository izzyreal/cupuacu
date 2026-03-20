# Cupuacu User Manual

## What Cupuacu Is

Cupuacu is a minimalist desktop audio editor for quick waveform-based editing.

At the moment, the core workflow is:

1. Open an existing audio file, or create a new empty file
2. Inspect the waveform
3. Select audio
4. Edit it
5. Apply simple effects
6. Overwrite the current file

This manual describes the current user-visible behavior of the app.

## Main Window

The main window contains:

- a menu bar at the top
- one waveform per channel
- transport controls and level metering
- a status bar at the bottom

When no file is open, Cupuacu shows an empty untitled document. Actions that require an active document are disabled.

## Opening Files

Use `File -> Open` to open an audio file with the system file picker.

When a file is opened successfully:

- it becomes the current document
- the window title changes to the file path
- it is added to `File -> Recent`
- it is moved to the top of the recent-files list

On startup, Cupuacu attempts to reopen the most recently opened file if it still exists.

## Recent Files

Use `File -> Recent` to reopen a recently opened file.

Behavior:

- the list is capped at 10 entries
- the newest entry is shown first
- if an entry points to a file that no longer exists, Cupuacu removes that entry from the list
- if there are no recent files, the submenu shows `No recent files`

## Creating a New File

Use `File -> New file`.

The dialog currently lets you choose:

- sample rate: `11025`, `22050`, `44100`, `48000`, or `96000` Hz
- bit depth: `8 bit` or `16 bit`
- channels: `1` or `2`

Selecting `OK` creates a new empty untitled document with those settings.

Selecting `Cancel` closes the dialog without changing the current document.

## Closing and Exiting

Use `File -> Close file` to close the current document and return to an empty untitled document.

Use `File -> Exit` to quit Cupuacu.

## Saving

Use `File -> Overwrite` to write the current document back to the current file path.

Current limitations:

- there is no `Save As` yet
- `Overwrite` is only available when the current document came from a file path
- the current overwrite path is a WAV overwrite workflow, not a general-purpose exporter
- in practice, it is currently intended for overwriting an existing 16-bit PCM WAV file
- a brand new untitled document cannot currently be written through a separate save dialog

## Selecting Audio

Selections are made directly in the waveform view.

The status bar shows:

- `Pos`: current cursor position
- `St`: selection start
- `End`: selection end
- `Len`: selection length
- `Val`: sample value under the mouse cursor
- `Rate`: sample rate
- `Depth`: bit depth

When there is no active selection:

- `St` follows the cursor
- `End` is empty
- `Len` is `0`

## Sample Value Display

The status bar shows the hovered sample value in a user-friendly form.

For integer PCM documents, Cupuacu shows the integer code that corresponds to the value that is stored or will be stored in the WAV data.

Examples:

- 8-bit PCM uses signed values from `-128` to `127`
- 16-bit PCM uses signed values from `-32768` to `32767`

For formats that are not integer PCM, Cupuacu falls back to a floating-point display.

## Edit Menu

The `Edit` menu currently contains:

- `Undo`
- `Redo`
- `Trim`
- `Cut`
- `Copy`
- `Paste`

Shortcuts shown in the menu are platform-aware:

- on macOS, Cupuacu shows `Cmd` shortcuts
- on other platforms, Cupuacu shows `Ctrl` shortcuts

Current shortcut labels in the menu include:

- `Open`
- `Overwrite`
- `Trim`
- `Cut`
- `Copy`
- `Paste`

## View Menu

The `View` menu currently contains:

- `Reset zoom`
- `Zoom out horiz.`
- `Zoom in horiz.`
- `Zoom out vert.`
- `Zoom in vert.`

The menu labels also show the current keyboard shortcuts:

- `Esc`: reset zoom
- `Q`: zoom out horizontally
- `W`: zoom in horizontally
- `E`: zoom out vertically
- `R`: zoom in vertically

## Generate Menu

The `Generate` menu currently contains one item:

- `Silence`

This menu is only meaningful when a document is open.

### Generate Silence

`Generate -> Silence` opens a dialog with:

- `Duration`
- `Units`
- `Cancel`
- `OK`

The available units are:

- `samples`
- `seconds`
- `milliseconds`

Behavior:

- if there is no selection, silence is inserted at the current cursor position
- if there is an active selection, the selection is replaced by silence of the requested duration
- `Cancel` closes the dialog without changing the document
- `OK` applies the operation

## Effects Menu

The `Effects` menu currently contains:

- `Amplify/Fade`
- `Dynamics`

This menu is disabled when no document is open.

## Options Menu

The `Options` menu currently contains:

- `Device Properties`

### Device Properties

The `Device Properties` window lets you choose:

- device type / host API
- output device
- input device

Changes are persisted automatically when you select a different device configuration.

## Persisted Settings

Cupuacu currently persists two JSON settings files:

- `recently_opened_files.json`
- `audio_device_properties.json`

These files are stored in Cupuacu's config directory:

- macOS: typically `~/Library/Application Support/Cupuacu/config/`
- Linux: typically `~/.config/Cupuacu/config/`
- Windows: typically `%AppData%\Cupuacu\config\`

So the full paths are usually:

- `.../Cupuacu/config/recently_opened_files.json`
- `.../Cupuacu/config/audio_device_properties.json`

### recently_opened_files.json

This file stores the recent-files list used by `File -> Recent`.

User-visible behavior:

- at most 10 entries are kept
- the newest file is first
- startup uses the first valid entry as the file to reopen automatically

### audio_device_properties.json

This file stores the selected audio device configuration used by the `Device Properties` window.

In practical terms, it remembers:

- the selected host API / device type
- the selected output device
- the selected input device

## Other App Files

Cupuacu also uses a per-user documents area named `Cupuacu`.

At the moment, this area is used for things like:

- `cupuacu.log`
- a `Temp` directory

The exact location depends on the platform's standard user documents folder.

## Current Behavior and Limitations

This section summarizes the current state of the app as it exists today.

- Opening files is supported through the system file picker
- Recent files are persistent and restored across launches
- The last successfully opened file is reopened on startup if it still exists
- New empty files can be created with sample rate, bit depth, and channel count
- Saving is currently an overwrite-style workflow, not a full save/save-as workflow
- Generate currently provides silence generation only
- Effects currently provide `Amplify/Fade` and `Dynamics`

As Cupuacu evolves, this manual should be updated alongside user-visible changes.
