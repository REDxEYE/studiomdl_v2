#pragma once

// Process-wide setup and teardown around one unit of work (one model compile, one dmx conversion). A process may run
// any number of these one after another (the DLL host does); the per-compile state itself (StudioMdlState) is owned
// by the caller and passed explicitly.
//
// BeginProcessing: the filesystem has only the executable's base search paths (each compile adds its own game,
//   content and local paths), and the datamodel, element framework and serializers are initialized with no elements
//   or files. If the previous unit of work never called EndProcessing (an error unwound past it), it is ended first.
// EndProcessing: destroys every element and file, drops the element types, serializers and updaters, and removes all
//   search paths. Does nothing if no processing is active.
bool BeginProcessing();

void EndProcessing();
