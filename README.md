# Safe

Welcome to the Safe source!

Safe is an application that makes it easy to
encrypt your files. When you encrypt your files with Safe
they are rendered unreadable to anyone who doesn't have your
password.

Safe aims to be cross-platform and currently runs on Windows and
Mac OS X. It works with all applications and file types and
can store encrypted files anywhere.

Safe is licensed under the GPLv3 and is based on free software.

## Repository Layout

* `GNUmakefile`: main make file
* `Xcode/`: Xcode project
* `assets/`: non-code source (e.g. vector graphics)
* `dependencies/`: packaged third-party dependencies
* `resources/`: pre-built static binary resources for Windows build
* `src/`: all source code, organized by component
* `tools/`: build and development tools, usually scripts

## Architecture

Safe's core is composed of two projects, Davfuse and EncFS.
Davfuse is essentially used as an embeddable webdav server.
EncFS is an encryption file system based on FUSE.

Safe glues Davfuse, EncFS and the file
system together to run a loopback WebDAV server that does on-the-fly
file encryption. Safe integrates with the user's
file system by instructing the operating system to
mount the WebDAV server.

Currently, Safe runs a separate WebDAV server instance for
each encrypted volume. Additionally, each WebDAV server instance
gets its own thread. This may change in the future.

Safe's provides minimal UI so that a user can create, mount,
and otherwise manupulate their encrypted
volumes. No cross-platform UI layer is used, instead the UI
re-implemented for each supported platform using the native
system UI library.

## Source Code Conventions

Safe is a C++11 project. Certain C++11 features are adopted to
minimize the chances of programming errors using C++11 features.

* All functions signal error via the C++ exception mechanism.

* "Out" parameters are not allowed, it is better to return an
  aggregate value (the "return value optimization" is relied upon).

* Do not use naked `new` / `delete`, only use `std::unique_ptr` and
  `std::shared_ptr`. If you must pass raw pointers via C-based
  mechanisms, use `std::unique_ptr::unique_ptr()` and
  `std::unique_ptr::release()`.

* Use RAII for C-based resources via `safe::create_deferred()` and
  `safe::ManagedResource<>`.

Since Safe is a cross-platform project, it is sometimes necessary
to use alternative languages and their conventions. For instance,
typically Objective-C method do not throw C++ exceptions and
instead return `nil`. All Objective-C methods in Safe should follow
the idiomatic Objective-C conventions. When implementing an internal
cross-platform C++11 interface in Objective-C, make sure to translate
between the two conventions, e.g. turn `nil` return values into C++
exceptions.

## Source Code Layout

All directories in `src/` mirror the C++ namespace the code exists
in. For example, all the source code in the `src/w32util` directory
lives in the `w32util` C++ namespace.

* `src/mount_webdav_interpose`: code specific to the bundled
   `webdavfs_agent` override library

* `src/safe`: Safe application code

* `src/safe/mac`: Mac OS X specific Safe application code

* `src/safe/win`: Windows specific Safe application code

* `src/update_driver`: code specific to the bundled
  `update_driver.exe` executable

* `src/w32util`: our internal Win32 C++ support library

In general most code should go into the `src/safe` directory unless
it is specific to a particular platform or it is specific to an
external helper binary that Safe bundles.

## Design Philosophy

Above all else, Safe aims to be minimal. It aims to do one thing
well and consistently. That one thing is to make it easy for
people to encrypt their files, nothing more. Once this goal
is accomplished, Safe should stay out of the way.

This philosophy also bleeds into how Safe is built. Minimal effort
and minimal code are of the utmost importance. Don't architect things
super intricately upfront. First do something reasonably minimal and
refine incrementally as necessary. Asymptotically, simple things should
be simple, and complex things should be composed of simpler things,
_nothing_ should be composed of more complex things.

Here are some manifestations of Safe's design philosophy:

* Keep external dependencies low, only rely on a dependency
  if the vast majority of the functionality is required.
* No unused code.
* No third-party UI library, use native UI library and use minimal
  internal application-specific interfaces.
* No reliance on over-generalized autotools or build system,
  Safe will never build for more than a handful of platforms.
* Minimal UI, minimal options, just do whatever is best and most
  secure for the user.
* No installer, just a portable executable.

## Building

Before building make sure you have Safe's other dependencies in the
same directory as this package: davfuse, encfs,
protobuf. These dependencies are not stock and contain Safe-specific
patches, they are also versioned with Safe, so you need to make
sure you have the right ones.

If you downloaded the Safe source release package (the .tar.gz),
you should already have all the right files.

If you obtained Safe from the GitHub repository, make sure
you checked out http://github.com/safeapp/saferoot. That is a
meta-repository that contains each correctly versioned dependency
as a git submodule.

### Windows

#### Prerequisites

Before you can build Safe, you need the following stock
prerequisites. Default options are fine.

* MinGW32 (http://www.mingw32.org/): install both MSYS and MinGW32.
* CMake (http://www.cmake.org/): used for building encfs
* Python 2.7 (http://www.python.org): used for building botan

After installation make sure that both CMake and Python are accessible
from the MSYS command prompt. Try running the `cmake` command and the
`python` command. If they are not, you will have to add them to your
Windows `%PATH%` environment variable.

#### Instructions

Simply run:

```
$ make dependencies
```

Then run:

```
$ make Safe.exe
```

### Mac OS X

#### Prerequisites

* Xcode (https://developer.apple.com/xcode/downloads/): our build system
* CMake (http://www.cmake.org/): used for building encfs

#### Instructions

The Mac OS X application is built using Xcode, simply launch
Xcode and open the `Xcode/Safe.xcodeproj` located in this directory.
Once launched, select the Xcode "Run" command. The first time
Safe is run it may take a while as it first builds its
dependencies.

## Copyright

Copyright (c) 2013,2014 Rian Hunter

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
