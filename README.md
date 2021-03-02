# Idmapped mounts

This is a tiny tool to allow the creation of idmapped mounts. In order for this
to work you need to be on a kernel with support for the `mount_setattr()`
syscall, i.e. at least Linux 5.12.

Note that this tool is not really meant to be production software.
It was mainly written to allow users to test the patchset during the review
process and in general to experiment with idmapped mounts.

```
mount-idmapped --map-mount=<idmap> <source> <target>

Create an idmapped mount of <source> at <target>
Options:
  --map-mount=<idmap>
        Specify an idmap for the <target> mount in the format
        <idmap-type>:<id-from>:<id-to>:<id-range>
        The <idmap-type> can be:
        "b" or "both"   -> map both uids and gids
        "u" or "uid"    -> map uids
        "g" or "gid"    -> map gids
        For example, specifying:
        both:1000:1001:1        -> map uid and gid 1000 to uid and gid 1001 in <target> and no other ids
        uid:20000:100000:1000   -> map uid 20000 to uid 100000, uid 20001 to uid 100001 [...] in <target>
        Currently up to 340 separate idmappings may be specified.

  --map-mount=/proc/<pid>/ns/user
        Specify a path to a user namespace whose idmap is to be used.

  --map-caller=<idmap>
        Specify an idmap to be used for the caller, i.e. move the caller into a new user namespace
        with the requested mapping.

  --recursive
        Copy the whole mount tree from <source> and apply the idmap to everyone at <target>.

Examples:
  - Create an idmapped mount of /source on /target with both ('b') uids and gids mapped:
        mount-idmapped --map-mount b:0:10000:10000 /source /target

  - Create an idmapped mount of /source on /target with uids ('u') and gids ('g') mapped separately:
        mount-idmapped --map-mount u:0:10000:10000 g:0:20000:20000 /source /target

  - Create an idmapped mount of /source on /target with both ('b') uids and gids mapped and a user namespace
    with both ('b') uids and gids mapped:
        mount-idmapped --map-caller b:0:10000:10000 --map-mount b:0:10000:1000 /source /target

  - Create an idmapped mount of /source on /target with uids ('u') gids ('g') mapped separately
    and a user namespace with both ('b') uids and gids mapped:
        mount-idmapped --map-caller u:0:10000:10000 g:0:20000:20000 --map-mount b:0:10000:1000 /source /target
```

The tool is based on the `mount_setattr()` syscall. A man page is currently up
for review but it will likely take a while for it to show up in distros. So for
the curious here it is:

NAME
====

mount\_setattr - change mount options of a mount or mount tree

SYNOPSIS
========

```c
int mount_setattr(int dfd, const char *path, unsigned int flags,
                  struct mount_attr *attr, size_t size);
```

*Note*: There is no glibc wrapper for this system call; see NOTES.

DESCRIPTION
===========

The **mount\_setattr**(2) system call changes the mount properties of a
mount or whole mount tree. If *path* is a relative pathname, then it is
interpreted relative to the directory referred to by the file descriptor
*dirfd* (or the current working directory of the calling process, if
*dirfd* is the special value **AT\_FDCWD**). If **AT\_EMPTY\_PATH** is
specified in *flags* then the mount properties of the mount identified
by *dirfd* are changed.

The **mount\_setattr**(2) syscall uses an extensible structure (*struct
mount\_attr*) to allow for future extensions. Any future extensions to
**mount\_setattr**(2) will be implemented as new fields appended to the
above structure, with a zero value in a new field resulting in the
kernel behaving as though that extension field was not present.
Therefore, the caller *must* zero-fill this structure on initialization.
(See the \"Extensibility\" section of the **NOTES** for more detail on
why this is necessary.)

The *size* argument should usually be specified as *sizeof(struct
mount\_attr)*. However, if the caller does not intend to make use of
features that got introduced after the initial version of *struct
mount\_attr* they are free to pass the size of the initial struct
together with the larger struct. This allows the kernel to not copy
later parts of the struct that aren\'t used anyway. With each extension
that changes the size of *struct mount\_attr* the kernel will expose a
define of the form **MOUNT\_ATTR\_SIZE\_VER\<number\> .** For example
the macro for the size of the initial version of *struct* mount\_attr is
**MOUNT\_ATTR\_SIZE\_VER0**

The *flags* argument can be used to alter the path resolution behavior.
The supported values are:

**AT\_EMPTY\_PATH**

The mount properties of the mount identified by *dfd* are changed.

**AT\_RECURSIVE**

Change the mount properties of the whole mount tree.

**AT\_SYMLINK\_NOFOLLOW**

Don\'t follow trailing symlinks.

**AT\_NO\_AUTOMOUNT**

Don\'t trigger automounts.

The *attr* argument of **mount\_setattr**(2) is a structure of the
following form:

```c
struct mount_attr {
    u64 attr_set;    /* Mount properties to set. */
    u64 attr_clr;    /* Mount properties to clear. */
    u64 propagation; /* Mount propagation type. */
    u64 userns_fd;   /* User namespace file descriptor. */
};
```

The *attr\_set* and *attr\_clr* members are used to specify the mount
options that are supposed to be set or cleared for a given mount or
mount tree.

When changing mount properties the kernel will first lower the flags
specified in the *attr\_clr* field and then raise the flags specified in
the *attr\_set* field:

```c
struct mount_attr attr = {
    .attr_clr |= MOUNT_ATTR_NOEXEC | MOUNT_ATTR_NODEV,
    .attr_set |= MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID,
};
unsigned int current_mnt_flags = mnt->mnt_flags;

/*
 * Clear all flags raised in .attr_clr, i.e
 * clear MOUNT_ATTR_NOEXEC and MOUNT_ATTR_NODEV.
 */
current_mnt_flags &= ~attr->attr_clr;

/*
 * Now raise all flags raised in .attr_set, i.e.
 * set MOUNT_ATTR_RDONLY and MOUNT_ATTR_NOSUID.
 */
current_mnt_flags |= attr->attr_set;

mnt->mnt_flags = current_mnt_flags;
```

The effect of this change will be a mount or mount tree that is
read-only, blocks the execution of setuid binaries but does allow
interactions with executables and devices nodes. Multiple changes with
the same set of flags requested in *attr\_clr* and *attr\_set* are
guaranteed to be idempotent after the changes have been applied.

The following mount attributes can be specified in the *attr\_set* or
*attr\_clr* fields:

**MOUNT\_ATTR\_RDONLY**

If set in *attr\_set* makes the mount read only and if set in
*attr\_clr* removes the read only setting if set on the mount.

**MOUNT\_ATTR\_NOSUID**

If set in *attr\_set* makes the mount not honor setuid, setgid binaries,
and file capabilities when executing programs. If set in *attr\_clr*
clears the setuid, setgid, and file capability restriction if set on
this mount.

**MOUNT\_ATTR\_NODEV**

If set in *attr\_set* prevents access to devices on this mount and if
set in *attr\_clr* removes the device access restriction if set on this
mount.

**MOUNT\_ATTR\_NOEXEC**

If set in *attr\_set* prevents executing programs on this mount and if
set in *attr\_clr* removes the restriction to execute programs on this
mount.

**MOUNT\_ATTR\_NODIRATIME**

If set in *attr\_set* prevents updating access time for directories on
this mount and if set in *attr\_clr* removes access time restriction for
directories. Note that **MOUNT\_ATTR\_NODIRATIME** can be combined with
other access time settings and is implied by the noatime setting. All
other access time settings are mutually exclusive.

**MOUNT\_ATTR\_\_ATIME - Changing access time settings**

In the new mount api the access time values are an enum starting from 0.
Even though they are an enum in contrast to the other mount flags such
as **MOUNT\_ATTR\_NOEXEC** they are nonetheless passed in *attr\_set*
and *attr\_clr* to keep the uapi consistent since **fsmount**(2) has the
same behavior.

Note, since access times are an enum, not a bitmap, users wanting to
transition to a different access time setting cannot simply specify the
access time in *attr\_set* but must also set **MOUNT\_ATTR\_\_ATIME** in
the *attr\_clr* field. The kernel will verify that
**MOUNT\_ATTR\_\_ATIME** isn\'t partially set in *attr\_clr* and that
*attr\_set* doesn\'t have any access time bits set if
**MOUNT\_ATTR\_\_ATIME** isn\'t set in *attr\_clr.*

  - **MOUNT\_ATTR\_RELATIME**

    When a file is accessed via this mount, update the file\'s last access
    time (atime) only if the current value of atime is less than or equal to
    the file\'s last modification time (mtime) or last status change time
    (ctime).

    To enable this access time setting on a mount or mount tree
    **MOUNT\_ATTR\_RELATIME** must be set in *attr\_set* and
    **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

  - **MOUNT\_ATTR\_NOATIME**

    Do not update access times for (all types of) files on this mount.

    To enable this access time setting on a mount or mount tree
    **MOUNT\_ATTR\_NOATIME** must be set in *attr\_set* and
    **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

  - **MOUNT\_ATTR\_STRICTATIME**

    Always update the last access time (atime) when files are accessed on
    this mount.

    To enable this access time setting on a mount or mount tree
    **MOUNT\_ATTR\_STRICTATIME** must be set in *attr\_set* and
    **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

**MOUNT\_ATTR\_IDMAP**

If set in *attr\_set* creates an idmapped mount. The idmapping is taken
from the user namespace specified in *userns\_fd* and attached to the
mount. It is currently not supported to change the idmapping of a mount
after it has been idmapped. Therefore, it is invalid to specify
**MOUNT\_ATTR\_IDMAP** in *attr\_clr.* More details can be found in
subsequent paragraphs.

Creating an idmapped mount allows to change the ownership of all files
located under a given mount. Other mounts that expose the same files
will not be affected, i.e. the ownership will not be changed.
Consequently, a caller accessing files through an idmapped mount will
see files under an idmapped mount owned by the uid and gid as specified
in the idmapping attached to the mount.

The idmapping is also applied to the following **xattr**(7) namespaces:

- The *security.* namespace when interacting with filesystem
  capabilities through the *security.capability* key whenever
  filesystem **capabilities**(7) are stored or returned in the
  *VFS\_CAP\_REVISION\_3* format which stores a rootid alongside
  the capabilities.

- The *system.posix\_acl\_access* and *system.posix\_acl\_default*
  keys whenever uids or gids are stored in **ACL\_USER** and
  **ACL\_GROUP** entries.

The following conditions must be met in order to create an idmapped
mount:

- The caller must currently have the *CAP\_SYS\_ADMIN* capability
  in the user namespace the underlying filesystem has been mounted
  in.

- The underlying filesystem must support idmapped mounts.
  Currently **xfs**(5), **ext4**(5) and **fat** filesystems
  support idmapped mounts with more filesystems being actively
  worked on.

- The mount must not already be idmapped. This also implies that
  the idmapping of a mount cannot be altered.

- The mount must be a detached/anonymous mount, i.e. it must have
  been created by calling **open\_tree**(2) with the
  *OPEN\_TREE\_CLONE* flag and it must not already have been
  visible in the filesystem.

In the common case the user namespace passed in *userns\_fd* together
with **MOUNT\_ATTR\_IDMAP** in *attr\_set* to create an idmapped mount
will be the user namespace of a container. In other scenarios it will be
a dedicated user namespace associated with a given user\'s login session
as is the case for portable home directories in
**systemd-homed.service**(8)). Details on how to create user namespaces
and how to setup idmappings can be gathered from
**user\_namespaces**(7).

In essence, an idmapping associated with a user namespace is a 1-to-1
mapping between source and target ids for a given range. Specifically,
an idmapping always has the abstract form *\[type of id\] \[source id\]
\[target id\] \[range\].* For example, uid 1000 1001 1 would mean that
uid 1000 is mapped to uid 1001, gid 1000 1001 2 would mean that gid 1000
will be mapped to gid 1001 and gid 1001 to gid 1002. If we were to
attach the idmapping of uid 1000 1001 1 to a mount it would cause all
files owned by uid 1000 to be owned by uid 1001. It is possible to
specify up to 340 of such idmappings providing for a great deal of
flexibility. If any source ids are not mapped to a target id all files
owned by that unmapped source id will appear as being owned by the
overflow uid or overflow gid respectively (see **user\_namespaces**(7)
and **proc**(5)).

Idmapped mounts can be useful in the following and a variety of other
scenarios:

- Idmapped mounts make it possible to easily share files between
  multiple users or multiple machines especially in complex
  scenarios. For example, idmapped mounts are used to implement
  portable home directories in **systemd-homed.service**(8) whre
  they allow users to move their home directory to an external
  storage device and use it on multiple computers where they are
  assigned different uids and gids. This effectively makes it
  possible to assign random uids and gids at login time.

- It is possible to share files from the host with unprivileged
  containers without having to change ownership permanently
  through **chown**(2).

- It is possible to idmap a container\'s rootfs without having to
  mangle every file.

- It is possible to share files between containers with
  non-overlapping idmappings

- Filesystem that lack a proper concept of ownership such as fat
  can use idmapped mounts to implement discretionary access (DAC)
  permission checking.

- They allow users to efficiently change ownership on a per-mount
  basis without having to (recursively) **chown**(2) all files. In
  contrast to **chown**(2) changing ownership of large sets of
  files is instantenous with idmapped mounts. This is especially
  useful when ownership of a whole root filesystem of a virtual
  machine or container is to be changed. With idmapped mounts a
  single **mount\_setattr**(2) syscall will be sufficient to
  change the ownership of all files.

- Idmapped mounts always take the current ownership into account
  as idmappings specify what a given uid or gid is supposed to be
  mapped to. This contrasts with the **chown**(2) syscall which
  cannot by itself take the current ownership of the files it
  changes into account. It simply changes the ownership to the
  specified uid and gid.

- Idmapped mounts allow to change ownership locally, restricting
  it to specific mounts, and temporarily as the ownership changes
  only apply as long as the mount exists. In contrast, changing
  ownership via the **chown**(2) syscall changes the ownership
  globally and permanently.

The *propagation* field is used to specify the propagation type of the
mount or mount tree. Only one propagation type can be specified, i.e.
the propagation values behave like an enum. The supported mount
propagation settings are:

**MS\_PRIVATE**

Turn all mounts into private mounts. Mount and umount events do not
propagate into or out of this mount point.

**MS\_SHARED**

Turn all mounts into shared mounts. Mount points share events with
members of a peer group. Mount and unmount events immediately under this
mount point will propagate to the other mount points that are members of
the peer group. Propagation here means that the same mount or unmount
will automatically occur under all of the other mount points in the peer
group. Conversely, mount and unmount events that take place under peer
mount points will propagate to this mount point.

**MS\_SLAVE**

Turn all mounts into dependent mounts. Mount and unmount events
propagate into this mount point from a shared peer group. Mount and
unmount events under this mount point do not propagate to any peer.

**MS\_UNBINDABLE**

This is like a private mount, and in addition this mount can\'t be bind
mounted. Attempts to bind mount this mount will fail. When a recursive
bind mount is performed on a directory subtree, any bind mounts within
the subtree are automatically pruned (i.e., not replicated) when
replicating that subtree to produce the target subtree.

RETURN VALUE
============

On success, **mount\_setattr**(2) zero is returned. On error, -1 is
returned and *errno* is set to indicate the cause of the error.

ERRORS
======

**EBADF**

*dfd* is not a valid file descriptor.

**EBADF**

An invalid file descriptor value was specified in *userns\_fd.*

**EBUSY**

The caller tried to change the mount to **MOUNT\_ATTR\_RDONLY** but the mount
had writers.

**EINVAL**

The path specified via the *dfd* and *path* arguments to
**mount\_setattr**(2) isn\'t a mountpoint.

**EINVAL**

Unsupported value in *flags*

**EINVAL**

Unsupported value was specified in the *attr\_set* field of *mount\_attr.*

**EINVAL**

Unsupported value was specified in the *attr\_clr* field of *mount\_attr.*

**EINVAL**

Unsupported value was specified in the *propagation* field of *mount\_attr.*

**EINVAL**

More than one of **MS\_SHARED,** **MS\_SLAVE,** **MS\_PRIVATE,** and
**MS\_UNBINDABLE** was set in *propagation* field of *mount\_attr.*

**EINVAL**

An access time setting was specified in the *attr\_set* field without
**MOUNT\_ATTR\_\_ATIME** being set in the *attr\_clr* field.

**EINVAL**

**MOUNT\_ATTR\_IDMAP** was specified in *attr\_clr.*

**EINVAL**

A file descriptor value was specified in *userns\_fd* which exceeds
**INT\_MAX.**

**EINVAL**

A valid file descriptor value was specified in *userns\_fd* but the file
descriptor wasn\'t a namespace file descriptor or did not refer to a user
namespace.

**EINVAL**

The underlying filesystem does not support idmapped mounts.

**EINVAL**

The mount to idmap is not a detached/anonymous mount, i.e. the mount is already
visible in the filesystem.

**EINVAL**

A partial access time setting was specified in *attr\_clr* instead of
**MOUNT\_ATTR\_\_ATIME** being set.

**EINVAL**

Caller tried to change the mount properties of a mount or mount tree in another
mount namespace.

**ENOENT**

A pathname was empty or had a nonexistent component.

**ENOMEM**

When changing mount propagation to **MS\_SHARED** a new peer group id needs to
be allocated for all mounts without a peer group id set which are
**MS\_SHARED.** Allocation of this peer group id has failed.

**ENOSPC**

When changing mount propagation to **MS\_SHARED** a new peer group id needs to
be allocated for all mounts without a peer group id set which are **MS\_SHARED.
(Though unlikely, allocation of peer group ids can fail. Note that technically
further error codes are possible that are specific to the id allocation
implementation used.)

**EPERM**

One of the mounts had at least one of **MOUNT\_ATTR\_RDONLY,**
**MOUNT\_ATTR\_NODEV,** **MOUNT\_ATTR\_NOSUID,** **MOUNT\_ATTR\_NOEXEC,**
**MOUNT\_ATTR\_NOATIME,** or **MOUNT\_ATTR\_NODIRATIME** set and the flag is
locked. Mount attributes become locked on a mount if:

- a new mount or mount tree is created causing mount propagation across user
  namespaces. The kernel will lock the aforementioned flags to protect these
  sensitive properties from being altered.

- a new mount and user namespace pair is created. This happens for example when
  specifying **CLONE\_NEWUSER**\|**CLONE\_NEWNS** in **unshare**(2),
  **clone**(2), or **clone3**(2). The aformentioned flags become locked to
  protect user namespaces from altering sensitive mount properties.

**EPERM**

A valid file descriptor value was specified in *userns\_fd* but the file
descriptor refers to the initial user namespace.

**EPERM**

An already idmapped mount was supposed to be idmapped.

**EPERM**

The caller does not have *CAP\_SYS\_ADMIN* in the user namespace the underlying
filesystem is mounted in.

VERSIONS
========

**mount\_setattr**(2) first appeared in Linux 5.12.

CONFORMING TO
=============

**mount\_setattr**(2) is Linux specific.

NOTES
=====

Currently, there is no glibc wrapper for this system call; call it using
**syscall**(2).

Extensibility
-------------

In order to allow for future extensibility, **mount\_setattr**(2)
equivalent to **openat2**(2) and **clone3**(2) requires the user-space
application to specify the size of the *mount\_attr* structure that it
is passing. By providing this information, it is possible for
**mount\_setattr**(2) to provide both forwards- and
backwards-compatibility, with *size* acting as an implicit version
number. (Because new extension fields will always be appended, the
structure size will always increase.) This extensibility design is very
similar to other system calls such as **perf\_setattr**(2),
**perf\_event\_open**(2), **clone3**(2) and **openat2**(2)

If we let *usize* be the size of the structure as specified by the
user-space application, and *ksize* be the size of the structure which
the kernel supports, then there are three cases to consider:

-   If *ksize* equals *usize*, then there is no version mismatch and
    *how* can be used verbatim.

-   If *ksize* is larger than *usize*, then there are some extension
    fields that the kernel supports which the user-space application is
    unaware of. Because a zero value in any added extension field
    signifies a no-op, the kernel treats all of the extension fields not
    provided by the user-space application as having zero values. This
    provides backwards-compatibility.

-   If *ksize* is smaller than *usize*, then there are some extension
    fields which the user-space application is aware of but which the
    kernel does not support. Because any extension field must have its
    zero values signify a no-op, the kernel can safely ignore the
    unsupported extension fields if they are all-zero. If any
    unsupported extension fields are non-zero, then -1 is returned and
    *errno* is set to **E2BIG**. This provides forwards-compatibility.

Because the definition of *struct mount\_attr* may change in the future
(with new fields being added when system headers are updated),
user-space applications should zero-fill *struct mount\_attr* to ensure
that recompiling the program with new headers will not result in
spurious errors at runtime. The simplest way is to use a designated
initializer:

```c
struct mount_attr attr = {
    .attr_set = MOUNT_ATTR_RDONLY,
    .attr_clr = MOUNT_ATTR_NODEV
};
```

or explicitly using **memset**(3) or similar:

```c
struct mount_attr attr;
memset(&attr, 0, sizeof(attr));
attr.attr_set = MOUNT_ATTR_RDONLY;
attr.attr_clr = MOUNT_ATTR_NODEV;
```

A user-space application that wishes to determine which extensions the
running kernel supports can do so by conducting a binary search on
*size* with a structure which has every byte nonzero (to find the
largest value which doesn\'t produce an error of **E2BIG**).

EXAMPLES
========

The following program allows the caller to create a new detached mount
and set various properties on it.

Program source
--------------

```c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/mount.h>
#include <linux/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* mount_setattr() */
#ifndef MOUNT_ATTR_RDONLY
#define MOUNT_ATTR_RDONLY 0x00000001
#endif

#ifndef MOUNT_ATTR_NOSUID
#define MOUNT_ATTR_NOSUID 0x00000002
#endif

#ifndef MOUNT_ATTR_NOEXEC
#define MOUNT_ATTR_NOEXEC 0x00000008
#endif

#ifndef MOUNT_ATTR__ATIME
#define MOUNT_ATTR__ATIME 0x00000070
#endif

#ifndef MOUNT_ATTR_NOATIME
#define MOUNT_ATTR_NOATIME 0x00000010
#endif

#ifndef MOUNT_ATTR_IDMAP
#define MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef AT_RECURSIVE
#define AT_RECURSIVE 0x8000
#endif

#ifndef __NR_mount_setattr
    #if defined __alpha__
        #define __NR_mount_setattr 552
    #elif defined _MIPS_SIM
        #if _MIPS_SIM == _MIPS_SIM_ABI32    /* o32 */
            #define __NR_mount_setattr (442 + 4000)
        #endif
        #if _MIPS_SIM == _MIPS_SIM_NABI32   /* n32 */
            #define __NR_mount_setattr (442 + 6000)
        #endif
        #if _MIPS_SIM == _MIPS_SIM_ABI64    /* n64 */
            #define __NR_mount_setattr (442 + 5000)
        #endif
    #elif defined __ia64__
        #define __NR_mount_setattr (442 + 1024)
    #else
        #define __NR_mount_setattr 442
    #endif
struct mount_attr {
    __u64 attr_set;
    __u64 attr_clr;
    __u64 propagation;
    __u64 userns_fd;
};
#endif

/* open_tree() */
#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

#ifndef __NR_open_tree
    #if defined __alpha__
        #define __NR_open_tree 538
    #elif defined _MIPS_SIM
        #if _MIPS_SIM == _MIPS_SIM_ABI32    /* o32 */
            #define __NR_open_tree 4428
        #endif
        #if _MIPS_SIM == _MIPS_SIM_NABI32   /* n32 */
            #define __NR_open_tree 6428
        #endif
        #if _MIPS_SIM == _MIPS_SIM_ABI64    /* n64 */
            #define __NR_open_tree 5428
        #endif
    #elif defined __ia64__
        #define __NR_open_tree (428 + 1024)
    #else
        #define __NR_open_tree 428
    #endif
#endif

/* move_mount() */
#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004
#endif

#ifndef __NR_move_mount
    #if defined __alpha__
        #define __NR_move_mount 539
    #elif defined _MIPS_SIM
        #if _MIPS_SIM == _MIPS_SIM_ABI32    /* o32 */
            #define __NR_move_mount 4429
        #endif
        #if _MIPS_SIM == _MIPS_SIM_NABI32   /* n32 */
            #define __NR_move_mount 6429
        #endif
        #if _MIPS_SIM == _MIPS_SIM_ABI64    /* n64 */
            #define __NR_move_mount 5429
        #endif
    #elif defined __ia64__
        #define __NR_move_mount (428 + 1024)
    #else
        #define __NR_move_mount 429
    #endif
#endif

static inline int mount_setattr(int dfd, const char *path, unsigned int flags,
                                struct mount_attr *attr, size_t size)
{
    return syscall(__NR_mount_setattr, dfd, path, flags, attr, size);
}

static inline int open_tree(int dfd, const char *filename, unsigned int flags)
{
    return syscall(__NR_open_tree, dfd, filename, flags);
}

static inline int move_mount(int from_dfd, const char *from_pathname, int to_dfd,
                 const char *to_pathname, unsigned int flags)
{
    return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd,
                   to_pathname, flags);
}

static const struct option longopts[] = {
    {"map-mount",       required_argument,  0,  'a'},
    {"recursive",       no_argument,        0,  'b'},
    {"read-only",       no_argument,        0,  'c'},
    {"block-setid",     no_argument,        0,  'd'},
    {"block-devices",   no_argument,        0,  'e'},
    {"block-exec",      no_argument,        0,  'f'},
    {"no-access-time",  no_argument,        0,  'g'},
    { NULL,             0,                  0,   0 },
};

#define exit_log(format, ...)                   \
    ({                                          \
        fprintf(stderr, format, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);                     \
    })

int main(int argc, char *argv[])
{
    int fd_userns = -EBADF, index = 0;
    bool recursive = false;
    struct mount_attr *attr = &(struct mount_attr){};
    const char *source, *target;
    int fd_tree, new_argc, ret;
    char *const *new_argv;

    while ((ret = getopt_long_only(argc, argv, "", longopts, &index)) != -1) {
        switch (ret) {
        case 'a':
            fd_userns = open(optarg, O_RDONLY | O_CLOEXEC);
            if (fd_userns < 0)
                exit_log("%m - Failed top open user namespace path %s, optarg);
            break;
        case 'b':
            recursive = true;
            break;
        case 'c':
            attr->attr_set |= MOUNT_ATTR_RDONLY;
            break;
        case 'd':
            attr->attr_set |= MOUNT_ATTR_NOSUID;
            break;
        case 'e':
            attr->attr_set |= MOUNT_ATTR_NODEV;
            break;
        case 'f':
            attr->attr_set |= MOUNT_ATTR_NOEXEC;
            break;
        case 'g':
            attr->attr_set |= MOUNT_ATTR_NOATIME;
            attr->attr_clr |= MOUNT_ATTR__ATIME;
            break;
        default:
            exit_log("Invalid argument specified");
        }
    }

    new_argv = &argv[optind];
    new_argc = argc - optind;
    if (new_argc < 2)
        exit_log("Missing source or target mountpoint);
    source = new_argv[0];
    target = new_argv[1];

    fd_tree = open_tree(-EBADF, source,
                        OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH |
                        (recursive ? AT_RECURSIVE : 0));
    if (fd_tree < 0)
        exit_log("%m - Failed to open %s, source);

    if (fd_userns >= 0) {
        attr->attr_set  |= MOUNT_ATTR_IDMAP;
        attr->userns_fd = fd_userns;
    }
    ret = mount_setattr(fd_tree, "",
                        AT_EMPTY_PATH | (recursive ? AT_RECURSIVE : 0),
                        attr, sizeof(struct mount_attr));
    if (ret < 0)
        exit_log("%m - Failed to change mount attributes);
    close(fd_userns);

    ret = move_mount(fd_tree, "", -EBADF, target, MOVE_MOUNT_F_EMPTY_PATH);
    if (ret < 0)
        exit_log("%m - Failed to attach mount to %s, target);
    close(fd_tree);

    exit(EXIT_SUCCESS);
}
```

SEE ALSO
========

**capabilities**(7), **clone**(2), **clone3**(2), **ext4**(5),
**mount**(2), **mount\_namespaces**(7), **newuidmap**(1),
**newgidmap**(1), **proc**(5), **unshare**(2), **user\_namespaces**(7),
**xattr**(7), **xfs**(5)
