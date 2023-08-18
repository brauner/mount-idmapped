# Idmapped mounts

This is a tiny tool to allow the creation of idmapped mounts. In order for this
to work you need to be on a kernel with support for the `mount_setattr()`
syscall, i.e. at least Linux 5.12.

Note that this tool is not really meant to be production software.
It was mainly written to allow users to test the patchset during the review
process and in general to experiment with idmapped mounts.

With util-linux v2.39 the functionality of `mount-idmapped` has been integrated
into the `mount` utility via the `X-mount.idmap=` option for bind mounts. This
uses the same syntax as `--map-mount=` option below.

```
mount-idmapped --map-mount=<idmap> [--map-mount=<idmap>] <abs path source> <abs path target>

Create an idmapped mount of <abs path source> at <abs path target>
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
        mount-idmapped --map-mount=b:0:10000:10000 /source /target

  - Create an idmapped mount of /source on /target with uids ('u') and gids ('g') mapped separately:
        mount-idmapped --map-mount=u:0:10000:10000 --map-mount=g:0:20000:20000 /source /target

  - Create an idmapped mount of /source on /target with both ('b') uids and gids mapped and a user namespace
    with both ('b') uids and gids mapped:
        mount-idmapped --map-caller=b:0:10000:10000 --map-mount=b:0:10000:1000 /source /target

  - Create an idmapped mount of /source on /target with uids ('u') gids ('g') mapped separately
    and a user namespace with both ('b') uids and gids mapped:
        mount-idmapped --map-caller=u:0:10000:10000 --map-mount=g:0:20000:20000 --map-mount=b:0:10000:1000 /source /target
```

The tool is based on the `mount_setattr()` syscall. A man page is currently up
for review but it will likely take a while for it to show up in distros. So for
the curious here it is:

NAME
====

mount\_setattr - change properties of a mount or mount tree

SYNOPSIS
========



    #include <linux/fcntl.h> /* Definition of AT_* constants */
    #include <linux/mount.h> /* Definition of MOUNT_ATTR_* constants */
    #include <sys/syscall.h> /* Definition of SYS_* constants */
    #include <unistd.h>

    int syscall(SYS_mount_setattr, int dirfd, const char *pathname,
     unsigned int flags, struct mount_attr *attr",size_t"size);

*Note*: glibc provides no wrapper for **mount\_setattr**(),
necessitating the use of **syscall**(2).

DESCRIPTION
===========

The **mount\_setattr**() system call changes the mount properties of a
mount or an entire mount tree. If *pathname* is a relative pathname,
then it is interpreted relative to the directory referred to by the file
descriptor *dirfd*. If *dirfd* is the special value **AT\_FDCWD**, then
*pathname* is interpreted relative to the current working directory of
the calling process. If *pathname* is the empty string and
**AT\_EMPTY\_PATH** is specified in *flags*, then the mount properties
of the mount identified by *dirfd* are changed. (See **openat**(2) for
an explanation of why the *dirfd* argument is useful.)

The **mount\_setattr**() system call uses an extensible structure
(*struct mount\_attr*) to allow for future extensions. Any non-flag
extensions to **mount\_setattr**() will be implemented as new fields
appended to the this structure, with a zero value in a new field
resulting in the kernel behaving as though that extension field was not
present. Therefore, the caller *must* zero-fill this structure on
initialization. See the \"Extensibility\" subsection under **NOTES** for
more details.

The *size* argument should usually be specified as *sizeof(struct
mount\_attr)*. However, if the caller is using a kernel that supports an
extended *struct mount\_attr*, but the caller does not intend to make
use of these features, it is possible to pass the size of an earlier
version of the structure together with the extended structure. This
allows the kernel to not copy later parts of the structure that aren\'t
used anyway. With each extension that changes the size of *struct
mount\_attr*, the kernel will expose a definition of the form
**MOUNT\_ATTR\_SIZE\_VER***number* . For example, the macro for the size
of the initial version of *struct mount\_attr* is
**MOUNT\_ATTR\_SIZE\_VER0**.

The *flags* argument can be used to alter the pathname resolution
behavior. The supported values are:

**AT\_EMPTY\_PATH**

-   If *pathname* is the empty string, change the mount properties on
    *dirfd* itself.

**AT\_RECURSIVE**

-   Change the mount properties of the entire mount tree.

**AT\_SYMLINK\_NOFOLLOW**

-   Don\'t follow trailing symbolic links.

**AT\_NO\_AUTOMOUNT**

-   Don\'t trigger automounts.

The *attr* argument of **mount\_setattr**() is a structure of the
following form:

```c
struct mount_attr {
    __u64 attr_set;     /* Mount properties to set */
    __u64 attr_clr;     /* Mount properties to clear */
    __u64 propagation;  /* Mount propagation type */
    __u64 userns_fd;    /* User namespace file descriptor */
};
```

The *attr\_set* and *attr\_clr* members are used to specify the mount
properties that are supposed to be set or cleared for a mount or mount
tree. Flags set in *attr\_set* enable a property on a mount or mount
tree, and flags set in *attr\_clr* remove a property from a mount or
mount tree.

When changing mount properties, the kernel will first clear the flags
specified in the *attr\_clr* field, and then set the flags specified in
the *attr\_set* field. For example, these settings:

```c
struct mount_attr attr = {
    .attr_clr = MOUNT_ATTR_NOEXEC | MOUNT_ATTR_NODEV,
    .attr_set = MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID,
};
```

are equivalent to the following steps:

```c
unsigned int current_mnt_flags = mnt->mnt_flags;

/*
 * Clear all flags set in .attr_clr,
 * clearing MOUNT_ATTR_NOEXEC and MOUNT_ATTR_NODEV.
 */
current_mnt_flags &= ~attr->attr_clr;

/*
 * Now set all flags set in .attr_set,
 * applying MOUNT_ATTR_RDONLY and MOUNT_ATTR_NOSUID.
 */
current_mnt_flags |= attr->attr_set;

mnt->mnt_flags = current_mnt_flags;
```

As a result of this change, the mount or mount tree (a) is read-only;
(b) blocks the execution of set-user-ID and set-group-ID programs; (c)
allows execution of programs; and (d) allows access to devices.

Multiple changes with the same set of flags requested in *attr\_clr* and
*attr\_set* are guaranteed to be idempotent after the changes have been
applied.

The following mount attributes can be specified in the *attr\_set* or
*attr\_clr* fields:

**MOUNT\_ATTR\_RDONLY**

-   If set in *attr\_set*, makes the mount read-only. If set in
    *attr\_clr*, removes the read-only setting if set on the mount.

**MOUNT\_ATTR\_NOSUID**

-   If set in *attr\_set*, causes the mount not to honor the set-user-ID
    and set-group-ID mode bits and file capabilities when executing
    programs. If set in *attr\_clr*, clears the set-user-ID,
    set-group-ID, and file capability restriction if set on this mount.

**MOUNT\_ATTR\_NODEV**

-   If set in *attr\_set*, prevents access to devices on this mount. If
    set in *attr\_clr*, removes the restriction that prevented accessing
    devices on this mount.

**MOUNT\_ATTR\_NOEXEC**

-   If set in *attr\_set*, prevents executing programs on this mount. If
    set in *attr\_clr*, removes the restriction that prevented executing
    programs on this mount.

**MOUNT\_ATTR\_NOSYMFOLLOW**

-   If set in *attr\_set*, prevents following symbolic links on this
    mount. If set in *attr\_clr*, removes the restriction that prevented
    following symbolic links on this mount.

**MOUNT\_ATTR\_NODIRATIME**

-   If set in *attr\_set*, prevents updating access time for directories
    on this mount. If set in *attr\_clr*, removes the restriction that
    prevented updating access time for directories. Note that
    **MOUNT\_ATTR\_NODIRATIME** can be combined with other access-time
    settings and is implied by the noatime setting. All other
    access-time settings are mutually exclusive.

**MOUNT\_ATTR\_\_ATIME** - changing access-time settings

-   The access-time values listed below are an enumeration that includes
    the value zero, expressed in the bits defined by the mask
    **MOUNT\_ATTR\_\_ATIME**. Even though these bits are an enumeration
    (in contrast to the other mount flags such as
    **MOUNT\_ATTR\_NOEXEC**), they are nonetheless passed in *attr\_set*
    and *attr\_clr* for consistency with **fsmount**(2), which
    introduced this behavior.

    Note that, since the access-time values are an enumeration rather
    than bit values, a caller wanting to transition to a different
    access-time setting cannot simply specify the access-time setting in
    *attr\_set*, but must also include **MOUNT\_ATTR\_\_ATIME** in the
    *attr\_clr* field. The kernel will verify that
    **MOUNT\_ATTR\_\_ATIME** isn\'t partially set in *attr\_clr* (i.e.,
    either all bits in the **MOUNT\_ATTR\_\_ATIME** bit field are either
    set or clear), and that *attr\_set* doesn\'t have any access-time
    bits set if **MOUNT\_ATTR\_\_ATIME** isn\'t set in *attr\_clr*.

    **MOUNT\_ATTR\_RELATIME**

    -   When a file is accessed via this mount, update the file\'s last
        access time (atime) only if the current value of atime is less
        than or equal to the file\'s last modification time (mtime) or
        last status change time (ctime).

        To enable this access-time setting on a mount or mount tree,
        **MOUNT\_ATTR\_RELATIME** must be set in *attr\_set* and
        **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

    **MOUNT\_ATTR\_NOATIME**

    -   Do not update access times for (all types of) files on this
        mount.

        To enable this access-time setting on a mount or mount tree,
        **MOUNT\_ATTR\_NOATIME** must be set in *attr\_set* and
        **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

    **MOUNT\_ATTR\_STRICTATIME**

    -   Always update the last access time (atime) when files are
        accessed on this mount.

        To enable this access-time setting on a mount or mount tree,
        **MOUNT\_ATTR\_STRICTATIME** must be set in *attr\_set* and
        **MOUNT\_ATTR\_\_ATIME** must be set in the *attr\_clr* field.

**MOUNT\_ATTR\_IDMAP**

-   If set in *attr\_set*, creates an ID-mapped mount. The ID mapping is
    taken from the user namespace specified in *userns\_fd* and attached
    to the mount.

    Since it is not supported to change the ID mapping of a mount after
    it has been ID mapped, it is invalid to specify
    **MOUNT\_ATTR\_IDMAP** in *attr\_clr*.

    For further details, see the subsection \"ID-mapped mounts\" under
    NOTES.

The *propagation* field is used to specify the propagation type of the
mount or mount tree. This field either has the value zero, meaning leave
the propagation type unchanged, or it has one of the following values:

**MS\_PRIVATE**

-   Turn all mounts into private mounts.

**MS\_SHARED**

-   Turn all mounts into shared mounts.

**MS\_SLAVE**

-   Turn all mounts into dependent mounts.

**MS\_UNBINDABLE**

-   Turn all mounts into unbindable mounts.

For further details on the above propagation types, see
**mount\_namespaces**(7).

RETURN VALUE
============

On success, **mount\_setattr**() returns zero. On error, -1 is returned
and *errno* is set to indicate the cause of the error.

ERRORS
======

**EBADF**

-   *pathname* is relative but *dirfd* is neither **AT\_FDCWD** nor a
    valid file descriptor.

**EBADF**

-   *userns\_fd* is not a valid file descriptor.

**EBUSY**

-   The caller tried to change the mount to **MOUNT\_ATTR\_RDONLY**, but
    the mount still holds files open for writing.

**EINVAL**

-   The pathname specified via the *dirfd* and *pathname* arguments to
    **mount\_setattr**() isn\'t a mount point.

**EINVAL**

-   An unsupported value was set in *flags*.

**EINVAL**

-   An unsupported value was specified in the *attr\_set* field of
    *mount\_attr*.

**EINVAL**

-   An unsupported value was specified in the *attr\_clr* field of
    *mount\_attr*.

**EINVAL**

-   An unsupported value was specified in the *propagation* field of
    *mount\_attr*.

**EINVAL**

-   More than one of **MS\_SHARED**, **MS\_SLAVE**, **MS\_PRIVATE**, or
    **MS\_UNBINDABLE** was set in the *propagation* field of
    *mount\_attr*.

**EINVAL**

-   An access-time setting was specified in the *attr\_set* field
    without **MOUNT\_ATTR\_\_ATIME** being set in the *attr\_clr* field.

**EINVAL**

-   **MOUNT\_ATTR\_IDMAP** was specified in *attr\_clr*.

**EINVAL**

-   A file descriptor value was specified in *userns\_fd* which exceeds
    **INT\_MAX**.

**EINVAL**

-   A valid file descriptor value was specified in *userns\_fd*, but the
    file descriptor did not refer to a user namespace.

**EINVAL**

-   The underlying filesystem does not support ID-mapped mounts.

**EINVAL**

-   The mount that is to be ID mapped is not a detached mount; that is,
    the mount has not previously been visible in a mount namespace.

**EINVAL**

-   A partial access-time setting was specified in *attr\_clr* instead
    of **MOUNT\_ATTR\_\_ATIME** being set.

**EINVAL**

-   The mount is located outside the caller\'s mount namespace.

**EINVAL**

-   The underlying filesystem has been mounted in a mount namespace that
    is owned by a noninitial user namespace

**ENOENT**

-   A pathname was empty or had a nonexistent component.

**ENOMEM**

-   When changing mount propagation to **MS\_SHARED**, a new peer group
    ID needs to be allocated for all mounts without a peer group ID set.
    This allocation failed because there was not enough memory to
    allocate the relevant internal structures.

**ENOSPC**

-   When changing mount propagation to **MS\_SHARED**, a new peer group
    ID needs to be allocated for all mounts without a peer group ID set.
    This allocation failed because the kernel has run out of IDs.

**EPERM**

-   One of the mounts had at least one of **MOUNT\_ATTR\_NOATIME**,
    **MOUNT\_ATTR\_NODEV**, **MOUNT\_ATTR\_NODIRATIME**,
    **MOUNT\_ATTR\_NOEXEC**, **MOUNT\_ATTR\_NOSUID**, or
    **MOUNT\_ATTR\_RDONLY** set and the flag is locked. Mount attributes
    become locked on a mount if:

    -   A new mount or mount tree is created causing mount propagation
        across user namespaces (i.e., propagation to a mount namespace
        owned by a different user namespace). The kernel will lock the
        aforementioned flags to prevent these sensitive properties from
        being altered.

    -   A new mount and user namespace pair is created. This happens for
        example when specifying **CLONE\_NEWUSER \| CLONE\_NEWNS** in
        **unshare**(2), **clone**(2), or **clone3**(2). The
        aforementioned flags become locked in the new mount namespace to
        prevent sensitive mount properties from being altered. Since the
        newly created mount namespace will be owned by the newly created
        user namespace, a calling process that is privileged in the new
        user namespace would---in the absence of such locking---be able
        to alter sensitive mount properties (e.g., to remount a mount
        that was marked read-only as read-write in the new mount
        namespace).

**EPERM**

-   A valid file descriptor value was specified in *userns\_fd*, but the
    file descriptor refers to the initial user namespace.

**EPERM**

-   An attempt was made to add an ID mapping to a mount that is already
    ID mapped.

**EPERM**

-   The caller does not have **CAP\_SYS\_ADMIN** in the initial user
    namespace.

VERSIONS
========

**mount\_setattr**() first appeared in Linux 5.12.

CONFORMING TO
=============

**mount\_setattr**() is Linux-specific.

NOTES
=====

ID-mapped mounts
----------------

Creating an ID-mapped mount makes it possible to change the ownership of
all files located under a mount. Thus, ID-mapped mounts make it possible
to change ownership in a temporary and localized way. It is a localized
change because the ownership changes are visible only via a specific
mount. All other users and locations where the filesystem is exposed are
unaffected. It is a temporary change because the ownership changes are
tied to the lifetime of the mount.

Whenever callers interact with the filesystem through an ID-mapped
mount, the ID mapping of the mount will be applied to user and group IDs
associated with filesystem objects. This encompasses the user and group
IDs associated with inodes and also the following **xattr**(7) keys:

-   *security.capability*, whenever filesystem capabilities are stored
    or returned in the **VFS\_CAP\_REVISION\_3** format, which stores a
    root user ID alongside the capabilities (see **capabilities**(7)).

-   *system.posix\_acl\_access* and *system.posix\_acl\_default*,
    whenever user IDs or group IDs are stored in **ACL\_USER** or
    **ACL\_GROUP** entries.

The following conditions must be met in order to create an ID-mapped
mount:

-   The caller must have the **CAP\_SYS\_ADMIN** capability in the
    initial user namespace.

-   The filesystem must be mounted in a mount namespace that is owned by
    the initial user namespace.

-   The underlying filesystem must support ID-mapped mounts. Currently,
    the **xfs**(5), **ext4**(5), and **FAT** filesystems support
    ID-mapped mounts with more filesystems being actively worked on.

-   The mount must not already be ID-mapped. This also implies that the
    ID mapping of a mount cannot be altered.

-   The mount must be a detached mount; that is, it must have been
    created by calling **open\_tree**(2) with the **OPEN\_TREE\_CLONE**
    flag and it must not already have been visible in a mount namespace.
    (To put things another way: the mount must not have been attached to
    the filesystem hierarchy with a system call such as
    **move\_mount**(2).)

ID mappings can be created for user IDs, group IDs, and project IDs. An
ID mapping is essentially a mapping of a range of user or group IDs into
another or the same range of user or group IDs. ID mappings are written
to map files as three numbers separated by white space. The first two
numbers specify the starting user or group ID in each of the two user
namespaces. The third number specifies the range of the ID mapping. For
example, a mapping for user IDs such as \"1000 1001 1\" would indicate
that user ID 1000 in the caller\'s user namespace is mapped to user ID
1001 in its ancestor user namespace. Since the map range is 1, only user
ID 1000 is mapped.

It is possible to specify up to 340 ID mappings for each ID mapping
type. If any user IDs or group IDs are not mapped, all files owned by
that unmapped user or group ID will appear as being owned by the
overflow user ID or overflow group ID respectively.

Further details on setting up ID mappings can be found in
**user\_namespaces**(7).

In the common case, the user namespace passed in *userns\_fd* (together
with **MOUNT\_ATTR\_IDMAP** in *attr\_set*) to create an ID-mapped mount
will be the user namespace of a container. In other scenarios it will be
a dedicated user namespace associated with a user\'s login session as is
the case for portable home directories in **systemd-homed.service**(8)).
It is also perfectly fine to create a dedicated user namespace for the
sake of ID mapping a mount.

ID-mapped mounts can be useful in the following and a variety of other
scenarios:

-   Sharing files or filesystems between multiple users or multiple
    machines, especially in complex scenarios. For example, ID-mapped
    mounts are used to implement portable home directories in
    **systemd-homed.service**(8), where they allow users to move their
    home directory to an external storage device and use it on multiple
    computers where they are assigned different user IDs and group IDs.
    This effectively makes it possible to assign random user IDs and
    group IDs at login time.

-   Sharing files or filesystems from the host with unprivileged
    containers. This allows a user to avoid having to change ownership
    permanently through **chown**(2).

-   ID mapping a container\'s root filesystem. Users don\'t need to
    change ownership permanently through **chown**(2). Especially for
    large root filesystems, using **chown**(2) can be prohibitively
    expensive.

-   Sharing files or filesystems between containers with non-overlapping
    ID mappings.

-   Implementing discretionary access (DAC) permission checking for
    filesystems lacking a concept of ownership.

-   Efficiently changing ownership on a per-mount basis. In contrast to
    **chown**(2), changing ownership of large sets of files is
    instantaneous with ID-mapped mounts. This is especially useful when
    ownership of an entire root filesystem of a virtual machine or
    container is to be changed as mentioned above. With ID-mapped
    mounts, a single **mount\_setattr**() system call will be sufficient
    to change the ownership of all files.

-   Taking the current ownership into account. ID mappings specify
    precisely what a user or group ID is supposed to be mapped to. This
    contrasts with the **chown**(2) system call which cannot by itself
    take the current ownership of the files it changes into account. It
    simply changes the ownership to the specified user ID and group ID.

-   Locally and temporarily restricted ownership changes. ID-mapped
    mounts make it possible to change ownership locally, restricting the
    ownership changes to specific mounts, and temporarily as the
    ownership changes only apply as long as the mount exists. By
    contrast, changing ownership via the **chown**(2) system call
    changes the ownership globally and permanently.

Extensibility
-------------

In order to allow for future extensibility, **mount\_setattr**()
requires the user-space application to specify the size of the
*mount\_attr* structure that it is passing. By providing this
information, it is possible for **mount\_setattr**() to provide both
forwards- and backwards-compatibility, with *size* acting as an implicit
version number. (Because new extension fields will always be appended,
the structure size will always increase.) This extensibility design is
very similar to other system calls such as **perf\_setattr**(2),
**perf\_event\_open**(2), **clone3**(2) and **openat2**(2).

Let *usize* be the size of the structure as specified by the user-space
application, and let *ksize* be the size of the structure which the
kernel supports, then there are three cases to consider:

-   If *ksize* equals *usize*, then there is no version mismatch and
    *attr* can be used verbatim.

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
    unsupported extension fields if they are all zero. If any
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

Alternatively, the structure can be zero-filled using **memset**(3) or
similar functions:

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

```c
/*
 * This program allows the caller to create a new detached mount
 * and set various properties on it.
 */
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

static inline int
mount_setattr(int dirfd, const char *pathname, unsigned int flags,
              struct mount_attr *attr, size_t size)
{
    return syscall(SYS_mount_setattr, dirfd, pathname, flags,
                   attr, size);
}

static inline int
open_tree(int dirfd, const char *filename, unsigned int flags)
{
    return syscall(SYS_open_tree, dirfd, filename, flags);
}

static inline int
move_mount(int from_dirfd, const char *from_pathname,
           int to_dirfd, const char *to_pathname, unsigned int flags)
{
    return syscall(SYS_move_mount, from_dirfd, from_pathname,
                   to_dirfd, to_pathname, flags);
}

static const struct option longopts[] = {
    {"map-mount",       required_argument,  NULL,  'a'},
    {"recursive",       no_argument,        NULL,  'b'},
    {"read-only",       no_argument,        NULL,  'c'},
    {"block-setid",     no_argument,        NULL,  'd'},
    {"block-devices",   no_argument,        NULL,  'e'},
    {"block-exec",      no_argument,        NULL,  'f'},
    {"no-access-time",  no_argument,        NULL,  'g'},
    { NULL,             0,                  NULL,   0 },
};

#define exit_log(format, ...)  do           \
{                                           \
    fprintf(stderr, format, ##__VA_ARGS__); \
    exit(EXIT_FAILURE);                     \
} while (0)

int
main(int argc, char *argv[])
{
    struct mount_attr *attr = &(struct mount_attr){};
    int fd_userns = -1;
    bool recursive = false;
    int index = 0;
    int ret;

    while ((ret = getopt_long_only(argc, argv, "",
                                   longopts, &index)) != -1) {
        switch (ret) {
        case 'a':
            fd_userns = open(optarg, O_RDONLY | O_CLOEXEC);
            if (fd_userns == -1)
                exit_log("%m - Failed top open %s\n", optarg);
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

    if ((argc - optind) < 2)
        exit_log("Missing source or target mount point\n");

    const char *source = argv[optind];
    const char *target = argv[optind + 1];

    /* In the following, -1 as the 'dirfd' argument ensures that
       open_tree() fails if 'source' is not an absolute pathname. */

    int fd_tree = open_tree(-1, source,
                       OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC |
                       AT_EMPTY_PATH | (recursive ? AT_RECURSIVE : 0));
    if (fd_tree == -1)
        exit_log("%m - Failed to open %s\n", source);

    if (fd_userns >= 0) {
        attr->attr_set  |= MOUNT_ATTR_IDMAP;
        attr->userns_fd = fd_userns;
    }

    ret = mount_setattr(fd_tree, "",
                        AT_EMPTY_PATH | (recursive ? AT_RECURSIVE : 0),
                        attr, sizeof(struct mount_attr));
    if (ret == -1)
        exit_log("%m - Failed to change mount attributes\n");

    close(fd_userns);

    /* In the following, -1 as the 'to_dirfd' argument ensures that
       open_tree() fails if 'target' is not an absolute pathname. */

    ret = move_mount(fd_tree, "", -1, target,
                     MOVE_MOUNT_F_EMPTY_PATH);
    if (ret == -1)
        exit_log("%m - Failed to attach mount to %s\n", target);

    close(fd_tree);

    exit(EXIT_SUCCESS);
}
```

SEE ALSO
========

**newuidmap**(1), **newgidmap**(1), **clone**(2), **mount**(2),
**unshare**(2), **proc**(5), **mount\_namespaces**(7),
**capabilities**(7), **user\_namespaces**(7), **xattr**(7)
