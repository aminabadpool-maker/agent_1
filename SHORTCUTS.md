# Mini-Blender Quick Shortcuts, Shell & Graphics

## CLI Shorthands (`./mini_blender`)
- `-h` : Show help & quick shortcuts
- `-i` : Interactive shell mode (live G/R/Z/M/N/S + `render` + `view` + `quit`)
- `-p` : Print ANSI truecolor terminal graphics preview of rendered frames
- `-f <num>` : Start frame (default: 0)
- `-n <num>` : Frame count in batch (default: 16)
- `-c <num>` : Cycle length for animation/orbit
- `-o <dir>` : Output directory (default: `renders`)
- `-s <file.xml>` : Load scene XML file
- `-g <x,y,z>` : Grab/move camera offset (Blender `G` feel)
- `-r <rx,ry,rz>` : Rotate offset in radians (Blender `R` feel)
- `-z <fov>` : Camera FOV / lens zoom
- `-m <scale>` : Scene scale multiplier (Blender `S` feel)
- `-q` : Quiet per-frame log

## Interactive Shell Commands (`./mini_blender -i` or `./mb_shell.sh`)
- `g <x,y,z>` : Move/grab offset
- `r <rx,ry,rz>` : Rotate offset
- `z <fov>` : Zoom / FOV
- `m <scale>` : Scene scale
- `n <count>` : Frame count
- `f <start>` : Start frame
- `s <path.xml>` : Scene XML file
- `p` : Toggle terminal graphics preview on/off
- `render` (or `run`) : Render batch now
- `gui` : Open latest rendered frame in laptop GUI viewer (if available)
- `status` : Show current shell parameters
- `help` : Show shell commands
- `quit` (or `exit`) : Exit shell

## Build & Run Shortcuts (`make`)
- `make` / `make all` : Compile with C11 + `-O3`
- `make run` : Quick 12-frame check with terminal preview
- `make shell` : Launch interactive shell
- `make stress` : Heavy 60-frame stress test
- `make clean` : Remove binary
