# scs-sdk-controller

This Euro Truck Simulator 2 and American Truck Simulator plugin is meant to be used by other programs to control the game. It's built on top of the [SCS SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry) that is provided by SCS Software.

The program will first create and then listen to changes in the following shared memory file: `Local\SCSControls` and `/dev/shm/SCS/SCSControls` for Windows and Linux respectively.

Available controls are listed in [`inputs.h`](inputs.h). More controls can be added, but they need to match the semantical controls defined in `controls.sii`.

## Building

This project is using the CMake build system. You can build it like any other CMake project by running:
```bash
cmake -B build
cmake --build build
```
