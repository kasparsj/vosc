# vosc

2D/3D OSC VJing software built on openFrameworks. **This is work in progress!**

# Installation

**For MacOS:** download pre-compiled [version Alpha binary](https://github.com/kasparsj/vosc/releases/tag/alpha).

**For all other OS:** clone the repository into your openFrameworks apps folder, then use project generator to generate project files, then build.

# Keyboard shortcuts

- `f` toggle fullscreen

- `c` toggle console

- `TAB` toggle debug view

# Command overview

## Setup
- `/mic` [Setup microphone input](Setup-commands#setup-microphone)
- `/sound/data` [Update sound data from SC](Setup-commands#sound-data)
- `/layers` [Setup layers](Setup-commands#setup-layers)
- `/layers/layout` [Change layout](Setup-commands#change-layout)

## Camera
- `/cam` [Use camera](Camera-commands#use-camera)
- `/cam/pos` [Camera position](Camera-commands#camera-position)
- `/cam/look` [Camera look at](Camera-commands#camera-look)
- `/cam/orbit` [Orbit camera](Camera-commands#camera-orbit)
- `/cam/set` [Set camera property](Camera-commands#camera-set)

## Textures
- `/tex` [Load texture](Texture-commands#texture-load)
- `/tex/reload` [Reload texture](Texture-commands#texture-reload)
- `/tex/unload` [Unload texture](Texture-commands#texture-unload)
- `/tex/choose` [Choose texture at random](Texture-commands#texture-choose)
- `/tex/clear` [Clear texture](Texture-commands#texture-clear)
- `/tex/var` [Set texture variable](Texture-commands#texture-variable)
- `/tex/set` [Set texture property](Texture-commands#texture-set)
- `/tex/size` [Set texture size](Texture-commands#texture-size)
- `/tex/color` [Set texture color](Texture-commands#texture-color)
- `/tex/tint` [Set texture tint](Texture-commands#texture-tint)
- `/tex/speed` [Set texture playback speed](Texture-commands#texture-speed)
- `/tex/seek` [Seek texture playback position](Texture-commands#texture-seek)
- `/tex/fbo` [Configure texture FBO](Texture-commands#texture-fbo)
- `/tex/looper` [Enable texture looper](Texture-commands#texture-looper)

## Geometry
- `/geom` [Load geometry](Geometry-commands#geometry-load)
- `/geom/set` [Set geometry property](Geometry-commands#geometry-set)

## Shaders
- `/shader` [Load layer shader](Shader-commands#shader-load)
- `/shader/var` [Set layer shader variable](Shader-commands#shader-var)
- `/shader/texture` [Set layer shader texture](Shader-commands#shader-texture)
- `/shader/set` [Set layer shader property](Shader-commands#shader-set)

## Layers
- `/layer/visible` [Show/hide layer](Layer-commands#layer-visible)
- `/layer/solo` [Hide all other layers](Layer-commands#layer-solo)
- `/layer/pos` [Set layer position](Layer-commands#layer-position)
- `/layer/size` [Set layer size](Layer-commands#layer-size)
- `/layer/scale` [Set layer scale](Layer-commands#layer-scale)
- `/layer/rot` [Set layer rotation](Layer-commands#layer-rotation)
- `/layer/pivot` [Set layer pivot point](Layer-commands#layer-pivot-point)
- `/layer/reset` [Reset layer](Layer-commands#layer-reset)
- `/layer/color` [Set layer color](Layer-commands#layer-color)
- `/layer/tint` [Set layer tint](Layer-commands#layer-tint)
- `/layer/alpha` [Set layer alpha](Layer-commands#layer-alpha)
- `/layer/bri` [Set layer brightness](Layer-commands#layer-brightness)
- `/layer/delay` [Set layer delay](Layer-commands#layer-delay)

## Variables

- `/var` [Set variable](Variable-commands#variable-set)

## Lights

- `/light` [Set light](Light-commands#light-set)
- `/light/remove` [Remove a light](Light-commands#light-remove)

## Materials
- `/mat/diffuse` [Set material diffuse color](Material-commands#diffuse)
- `/mat/ambient` [Set material ambient color](Material-commands#ambient)
- `/mat/specular` [Set material specular color](Material-commands#specular)
- `/mat/emissive` [Set material emissive color](Material-commands#emissive)
- `/mat/shininess` [Set material shininess](Material-commands#shininess)

# Constants

- [Layouts](Constants#layouts)
- [Source types](Constants#source-types)
- [3D primitives](Constants#3d-primitives)