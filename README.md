# Idmapped mounts

This is a tiny tool to allow the creation of idmapped mounts. In order for this
to work you need to be on a kernel with support for the `mount_setattr()`
syscall, i.e. at least Linux 5.12.

Note that this tool is not really meant to be production software.
It was mainly written to allow users to test the patchset during the review
process and in general to experiment with idmapped mounts.
