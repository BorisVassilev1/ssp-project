# Project for Seminar for Systems Programming at SU FMI

Implements a multithreaded task system with priorities.

## Compilation

Clone submodules, then just:
```bash
./configure.sh
./build.sh
```

## Usage

An example can be found in TaskSystem/main.cpp . To test:
```bash
./run.sh # regular test
./run.sh # with visualization of task states
```

## Implemented tasks

I made a Task that computes a BRDF Integration map for realtime rendering.
