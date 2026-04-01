# Cupuacu User Manual

## Main Window

The main window contains:

- a menu bar at the top
- a tab strip below the menu bar
- one waveform per channel
- transport controls and level metering
- a status bar at the bottom

When no file is open, Cupuacu shows an empty untitled document. Actions that require an active document are disabled.

## Opening Files

Use `File -> Open` to open an audio file with the system file picker.

When a file is opened successfully:

- it opens in a tab
- it becomes the active tab
- the window title changes to the file path
- it is added to `File -> Recent`
- it is moved to the top of the recent-files list

If Cupuacu starts with only the initial blank tab, opening a file reuses that blank tab. Otherwise, opening a file creates a new tab.

On startup, Cupuacu restores the previously open file-backed tabs when those files still exist.

If the saved session data only contains the older recent-files list, Cupuacu falls back to reopening the most recent existing file.

## Recent Files

Use `File -> Recent` to reopen a recently opened file.

Behavior:

- the list is capped at 10 entries
- the newest entry is shown first
- if an entry points to a file that no longer exists, Cupuacu removes that entry from the list
- if there are no recent files, the submenu shows `No recent files`
- selecting a recent file opens it in a tab

## Creating a New File

Use `File -> New file`.

Shortcut:

- `Cmd/Ctrl + N`

The dialog lets you choose:

- sample rate: `11025`, `22050`, `44100`, `48000`, or `96000` Hz
- bit depth: `8 bit` or `16 bit`
- channels: `1` or `2`

Selecting `OK` creates a new empty untitled document with those settings.

If Cupuacu still only has the initial blank tab, that tab is reused. Otherwise, a new tab is created.

Selecting `Cancel` closes the dialog without changing the current document.

## Tabs

Each open document appears in the tab strip.

Behavior:

- clicking a tab activates it
- clicking the `x` inside a tab closes that tab
- if only one tab exists, closing the file clears it back to an empty untitled document
- Cupuacu allows only one actively playing or recording tab at a time

## Closing and Exiting

Use `File -> Close file` to close the current document and return to an empty untitled document.

Shortcut:

- `Cmd/Ctrl + W`

Use `File -> Exit` to quit Cupuacu.

## Saving

Use `File -> Overwrite` to write the current document back to the current file path.

Notes:

- there is no `Save As` yet
- `Overwrite` is only available when the current document came from a file path
- the current overwrite path is a WAV overwrite workflow, not a general-purpose exporter
- in practice, it is intended for overwriting an existing 16-bit PCM WAV file
- a brand new untitled document cannot be written through a separate save dialog

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

The `Edit` menu contains:

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

## Keyboard Shortcuts

App-wide shortcuts:

- `Cmd/Ctrl + O`: open a file
- `Cmd/Ctrl + N`: open the New File dialog
- `Cmd/Ctrl + W`: close the current file
- `Cmd/Ctrl + S`: overwrite the current file
- `Cmd/Ctrl + Z`: undo
- `Cmd/Ctrl + Shift + Z`: redo
- `Cmd/Ctrl + X`: cut the current selection
- `Cmd/Ctrl + C`: copy the current selection
- `Cmd/Ctrl + V`: paste the clipboard
- `Cmd/Ctrl + T`: trim to the current selection
- `Space`: play or stop playback
- `Esc`: reset zoom
- `Z`: zoom to the current selection
- `Q`: zoom out horizontally
- `W`: zoom in horizontally
- `E`: zoom out vertically
- `R`: zoom in vertically
- `Left Arrow`: scroll the view left
- `Right Arrow`: scroll the view right
- `Shift + .`: increase pixel scale
- `Shift + ,`: decrease pixel scale

For the arrow keys and vertical zoom keys, holding modifier keys increases the step size. The current implementation multiplies the step when `Shift`, `Alt`, or `Ctrl` are held.

## View Menu

The `View` menu contains:

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

The `Generate` menu contains one item:

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

The `Effects` menu contains:

- `Reverse`
- `Amplify/Fade`
- `Dynamics`

This menu is disabled when no document is open.

## Options Menu

The `Options` menu contains:

- `Device Properties`

### Device Properties

The `Device Properties` window lets you choose:

- device type / host API
- output device
- input device

Changes are persisted automatically when you select a different device configuration.

## Persisted Settings

Cupuacu persists three JSON settings files:

- `recently_opened_files.json`
- `session_state.json`
- `audio_device_properties.json`

These files are stored in Cupuacu's config directory. The current paths are:

- macOS:
  - `~/Library/Application Support/Cupuacu/config/recently_opened_files.json`
  - `~/Library/Application Support/Cupuacu/config/session_state.json`
  - `~/Library/Application Support/Cupuacu/config/audio_device_properties.json`
- Linux:
  - `~/.config/Cupuacu/config/recently_opened_files.json`
  - `~/.config/Cupuacu/config/session_state.json`
  - `~/.config/Cupuacu/config/audio_device_properties.json`
- Windows:
  - `%AppData%\Cupuacu\config\recently_opened_files.json`
  - `%AppData%\Cupuacu\config\session_state.json`
  - `%AppData%\Cupuacu\config\audio_device_properties.json`

### Log File

Cupuacu also writes a rotating log file for diagnostic information such as startup problems, file open failures, and save/export failures.

The current log file paths are:

- macOS:
  - `~/Library/Logs/Cupuacu/cupuacu.log`
- Linux:
  - `~/.local/state/Cupuacu/Logs/cupuacu.log`
- Windows:
  - `%LocalAppData%\Cupuacu\Logs\cupuacu.log`

Older rotated log files use the same directory and file name prefix.

### recently_opened_files.json

This file stores the recent-files list used by `File -> Recent`.

User-visible behavior:

- at most 10 entries are kept
- the newest file is first

### session_state.json

This file stores the restorable file-backed tab session.

User-visible behavior:

- file-backed tabs from the last session are reopened on startup when those paths still exist
- the active restored tab is remembered when it refers to a file-backed tab

### audio_device_properties.json

This file stores the selected audio device configuration used by the `Device Properties` window.

In practical terms, it remembers:

- the selected host API / device type
- the selected output device
- the selected input device
