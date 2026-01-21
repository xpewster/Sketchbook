# Sketchbook
Hardware monitor/weather program including a desktop agent and supporting a remote esp32 board with TFT display.

All code for both fronts provided.

## Features
<figure>
    <figcaption><i>"Sketchbook" -- flagship skin of the software.</i></figcaption>
</figure>

![Sketchbook skin](https://media1.giphy.com/media/v1.Y2lkPTc5MGI3NjExMTM2NGxtbGlxcjBsZzk0ZmFxeXV4bTFha243Mm1lMGt3cnB4amF4YiZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/BitQuRCKAdipU2KeSs/giphy.gif)

<figure>
    <figcaption><i>"Space"</i></figcaption>
</figure>

![Space skin](https://media0.giphy.com/media/v1.Y2lkPTc5MGI3NjExajhsYXhmbXVqazA2MGF2ZXJ5eWVkeDJhaGo5MXcycDM3NmNlYWFxdyZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/Gpf2Dx5bitiZAbgpYs/giphy.gif)

- Skins (customizable with .xml files)
- Tunable settings to achieve maximum framerate. Also utilizes dirty rects to improve throughput.
- Windows startup and tray hooks
- Advanced post-processing effects (e.g. jpegify)

## Included skins
- Sketchbook2 - An early version of the Sketchbook skin
- Space - Cool space skin

## Build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release // First time
cmake --build build --config Release
```

## License


See [attached license](LICENSE). Thank you for your time.


