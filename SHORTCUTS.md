# Mini-Blender Quick Shortcuts & CLI Flags

## Blender-Inspired CLI Shorthands (`./mini_blender`)
- `-h` : Show help & quick shortcuts
- `-f <num>` : Start frame (like timeline start)
- `-n <num>` : Frame count in this batch
- `-c <num>` : Cycle length for full rotation/orbit
- `-o <dir>` : Output directory (default: `renders`)
- `-s <file.xml>` : Load scene XML file
- `-g <dx,dy,dz>` : Quick Grab/Move camera/scene offset (Blender `G` feel)
- `-r <rx,ry,rz>` : Quick Rotate offset in radians (Blender `R` feel)
- `-z <fov>` : Quick Zoom/FOV adjustment (Blender `S` / lens feel)

## Build & Run Shortcuts (`make`)
- `make` / `make all` : Compile with C11 + `-O3`
- `make run` : Quick 12-frame render check
- `make stress` : 24-frame stress render using `scenes/blender_like_scene.xml`
- `make clean` : Remove compiled binary
