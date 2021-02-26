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
