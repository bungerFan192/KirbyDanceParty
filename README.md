# Kirby Dance Party

A 2.5D rhythm game for Nintendo 3DS, starring dancing Kirbys. The gameplay is built around **Call & Response**: watch a rival's dance phrase, then repeat it in time with the music.

## Story

In a parallel universe inspired by *Kirby Battle Royale*, King Dedede builds a machine that prints Kirby copies. One of those copies discovers music and dancing, and Dedede sees a business opportunity: a Kirby dance tournament!

## Current prototype

This repository starts with a native Nintendo 3DS homebrew foundation:

- C++ with **libctru** and **citro3d**
- Top screen: 3D stage render target
- Bottom screen: debug/gameplay console
- D-pad rhythm input with PERFECT / GOOD / MISS timing judgements
- A short demonstration call-and-response phrase
- GitHub Actions build workflow for a `.3dsx` artifact

This is an initial technical milestone, not the finished visual game. Character sprites, animation, music playback, charts and menus will be added in later milestones.

## Controls

- D-pad: enter the shown direction during the response phrase
- START: exit

## Build locally

Install devkitPro with the Nintendo 3DS development packages, then from the repository root:

```sh
make
```

The build produces `KirbyDanceParty.3dsx`.

## Project layout

- `source/main.cpp` — application loop, 3DS setup and prototype rhythm sequence
- `Makefile` — devkitPro 3DS build
- `.github/workflows/build.yml` — automated build

## Design principles

- Rhythm timing is based on system time, not frame counts.
- Rendering and rhythm logic are kept separate so animation and audio can be added without changing the timing model.
- The game will target the dual-screen layout and stereoscopic top screen of the 3DS.
