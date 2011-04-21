Plugins
=======

GIMP
----

### Installation

If you are using Ubuntu then install the libgimp2.0-dev and build-essential packages from the repositories (other distributions
should have similar packages available).

Then it is simply a case of running

	gimptool-2.0 --install <plugin>

where `<plugin>` is either the binary (if available) or the C source file.

### Adaptive Threshold Edge Detect

File: adaptive-edge.c

An alternative edge detector based on the implementation in the OpenIllusionist project.

The original algorithm requires scaling down of the entire image area to be processed and
it would have to be completely re-written to work with GIMP tiles. Since this plugin is a
simple adaptation of the original algorithm it does not make use of tiles and therefore it
needs to allocate a lot of memory to work. So please be aware that it may have problems if
you attempt to use it on an extremely large image!