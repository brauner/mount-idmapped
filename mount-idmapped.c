/* SPDX-License-Identifier: LGPL-2.1+ */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <linux/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
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

#ifndef MOUNT_ATTR_NODIRATIME
#define MOUNT_ATTR_NODIRATIME 0x00000080
#endif

#ifndef MOUNT_ATTR__ATIME
#define MOUNT_ATTR__ATIME 0x00000070
#endif

#ifndef MOUNT_ATTR_RELATIME
#define MOUNT_ATTR_RELATIME 0x00000000
#endif

#ifndef MOUNT_ATTR_NOATIME
#define MOUNT_ATTR_NOATIME 0x00000010
#endif

#ifndef MOUNT_ATTR_STRICTATIME
#define MOUNT_ATTR_STRICTATIME 0x00000020
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
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_mount_setattr (442 + 4000)
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_mount_setattr (442 + 6000)
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
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
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_open_tree 4428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_open_tree 6428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_open_tree 5428
		#endif
	#elif defined __ia64__
		#define __NR_open_tree (428 + 1024)
	#else
		#define __NR_open_tree 428
	#endif
#endif

/* move_mount() */
#ifndef MOVE_MOUNT_F_SYMLINKS
#define MOVE_MOUNT_F_SYMLINKS 0x00000001 /* Follow symlinks on from path */
#endif

#ifndef MOVE_MOUNT_F_AUTOMOUNTS
#define MOVE_MOUNT_F_AUTOMOUNTS 0x00000002 /* Follow automounts on from path */
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

#ifndef MOVE_MOUNT_T_SYMLINKS
#define MOVE_MOUNT_T_SYMLINKS 0x00000010 /* Follow symlinks on to path */
#endif

#ifndef MOVE_MOUNT_T_AUTOMOUNTS
#define MOVE_MOUNT_T_AUTOMOUNTS 0x00000020 /* Follow automounts on to path */
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
#define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#endif

#ifndef MOVE_MOUNT__MASK
#define MOVE_MOUNT__MASK 0x00000077
#endif

#ifndef __NR_move_mount
	#if defined __alpha__
		#define __NR_move_mount 539
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_move_mount 4429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_move_mount 6429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_move_mount 5429
		#endif
	#elif defined __ia64__
		#define __NR_move_mount (428 + 1024)
	#else
		#define __NR_move_mount 429
	#endif
#endif

/* A few helpful macros. */
#define IDMAPLEN 4096

#define STRLITERALLEN(x) (sizeof(""x"") - 1)

#define INTTYPE_TO_STRLEN(type)             \
	(2 + (sizeof(type) <= 1             \
		  ? 3                       \
		  : sizeof(type) <= 2       \
			? 5                 \
			: sizeof(type) <= 4 \
			      ? 10          \
			      : sizeof(type) <= 8 ? 20 : sizeof(int[-2 * (sizeof(type) > 8)])))

#define syserror(format, ...)                           \
	({                                              \
		fprintf(stderr, format, ##__VA_ARGS__); \
		(-errno);                               \
	})

#define syserror_set(__ret__, format, ...)                    \
	({                                                    \
		typeof(__ret__) __internal_ret__ = (__ret__); \
		errno = labs(__ret__);                        \
		fprintf(stderr, format, ##__VA_ARGS__);       \
		__internal_ret__;                             \
	})

#define call_cleaner(cleaner) __attribute__((__cleanup__(cleaner##_function)))

#define free_disarm(ptr)    \
	({                  \
		free(ptr);  \
		ptr = NULL; \
	})

static inline void free_disarm_function(void *ptr)
{
	free_disarm(*(void **)ptr);
}
#define __do_free call_cleaner(free_disarm)

#define move_ptr(ptr)                                 \
	({                                            \
		typeof(ptr) __internal_ptr__ = (ptr); \
		(ptr) = NULL;                         \
		__internal_ptr__;                     \
	})

#define define_cleanup_function(type, cleaner)           \
	static inline void cleaner##_function(type *ptr) \
	{                                                \
		if (*ptr)                                \
			cleaner(*ptr);                   \
	}

#define call_cleaner(cleaner) __attribute__((__cleanup__(cleaner##_function)))

#define close_prot_errno_disarm(fd) \
	if (fd >= 0) {              \
		int _e_ = errno;    \
		close(fd);          \
		errno = _e_;        \
		fd = -EBADF;        \
	}

static inline void close_prot_errno_disarm_function(int *fd)
{
       close_prot_errno_disarm(*fd);
}
#define __do_close call_cleaner(close_prot_errno_disarm)

define_cleanup_function(FILE *, fclose);
#define __do_fclose call_cleaner(fclose)

define_cleanup_function(DIR *, closedir);
#define __do_closedir call_cleaner(closedir)

static inline int sys_mount_setattr(int dfd, const char *path, unsigned int flags,
				    struct mount_attr *attr, size_t size)
{
	return syscall(__NR_mount_setattr, dfd, path, flags, attr, size);
}

static inline int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

static inline int sys_move_mount(int from_dfd, const char *from_pathname, int to_dfd,
				 const char *to_pathname, unsigned int flags)
{
	return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd, to_pathname, flags);
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int write_file(const char *path, const void *buf, size_t count)
{
	int fd;
	ssize_t ret;

	fd = open(path, O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
	if (fd < 0)
		return -errno;

	ret = write_nointr(fd, buf, count);
	close(fd);
	if (ret < 0 || (size_t)ret != count)
		return -1;

	return 0;
}

/*
 * Let's use the "standard stack limit" (i.e. glibc thread size default) for
 * stack sizes: 8MB.
 */
#define __STACK_SIZE (8 * 1024 * 1024)
static pid_t do_clone(int (*fn)(void *), void *arg, int flags)
{
	void *stack;

	stack = malloc(__STACK_SIZE);
	if (!stack)
		return -ENOMEM;

#ifdef __ia64__
	return __clone2(fn, stack, __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#else
	return clone(fn, stack + __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#endif
}

static int clone_cb(void *data)
{
	return kill(getpid(), SIGSTOP);
}

struct list {
	void *elem;
	struct list *next;
	struct list *prev;
};

#define list_for_each(__iterator, __list) \
	for (__iterator = (__list)->next; __iterator != __list; __iterator = __iterator->next)

static inline void list_init(struct list *list)
{
	list->elem = NULL;
	list->next = list->prev = list;
}

static inline int list_empty(const struct list *list)
{
	return list == list->next;
}

static inline void __list_add(struct list *new, struct list *prev, struct list *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add_tail(struct list *head, struct list *list)
{
	__list_add(list, head->prev, head);
}

typedef enum idmap_type_t {
	ID_TYPE_UID,
	ID_TYPE_GID
} idmap_type_t;

struct id_map {
	idmap_type_t map_type;
	__u32 nsid;
	__u32 hostid;
	__u32 range;
};

static struct list active_map;

static int add_map_entry(__u32 id_host,
			 __u32 id_ns,
			 __u32 range,
			 idmap_type_t map_type)
{
	__do_free struct list *new_list = NULL;
	__do_free struct id_map *newmap = NULL;

	newmap = malloc(sizeof(*newmap));
	if (!newmap)
		return -ENOMEM;

	new_list = malloc(sizeof(struct list));
	if (!new_list)
		return -ENOMEM;

	*newmap = (struct id_map){
		.hostid		= id_host,
		.nsid		= id_ns,
		.range		= range,
		.map_type	= map_type,
	};

	new_list->elem = move_ptr(newmap);
	list_add_tail(&active_map, move_ptr(new_list));
	return 0;
}

static int parse_map(char *map)
{
	char types[2] = {'u', 'g'};
	int ret;
	__u32 id_host, id_ns, range;
	char which;

	if (!map)
		return -1;

	ret = sscanf(map, "%c:%u:%u:%u", &which, &id_ns, &id_host, &range);
	if (ret != 4)
		return -1;

	if (which != 'b' && which != 'u' && which != 'g')
		return -1;

	for (int i = 0; i < 2; i++) {
		idmap_type_t map_type;

		if (which != types[i] && which != 'b')
			continue;

		if (types[i] == 'u')
			map_type = ID_TYPE_UID;
		else
			map_type = ID_TYPE_GID;

		ret = add_map_entry(id_host, id_ns, range, map_type);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int write_id_mapping(idmap_type_t map_type, pid_t pid, const char *buf, size_t buf_size)
{
	__do_close int fd = -EBADF;
	int ret;
	char path[STRLITERALLEN("/proc") + INTTYPE_TO_STRLEN(pid_t) +
		  STRLITERALLEN("/setgroups") + 1];

	if (geteuid() != 0 && map_type == ID_TYPE_GID) {
		__do_close int setgroups_fd = -EBADF;

		ret = snprintf(path, PATH_MAX, "/proc/%d/setgroups", pid);
		if (ret < 0 || ret >= PATH_MAX)
			return -E2BIG;

		setgroups_fd = open(path, O_WRONLY | O_CLOEXEC);
		if (setgroups_fd < 0 && errno != ENOENT)
			return syserror("Failed to open \"%s\"", path);

		if (setgroups_fd >= 0) {
			ret = write_nointr(setgroups_fd, "deny\n", STRLITERALLEN("deny\n"));
			if (ret != STRLITERALLEN("deny\n"))
				return syserror("Failed to write \"deny\" to \"/proc/%d/setgroups\"", pid);
		}
	}

	ret = snprintf(path, PATH_MAX, "/proc/%d/%cid_map", pid, map_type == ID_TYPE_UID ? 'u' : 'g');
	if (ret < 0 || ret >= PATH_MAX)
		return -E2BIG;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return syserror("Failed to open \"%s\"", path);

	ret = write_nointr(fd, buf, buf_size);
	if (ret != buf_size)
		return syserror("Failed to write %cid mapping to \"%s\"",
				map_type == ID_TYPE_UID ? 'u' : 'g', path);

	return 0;
}

static int map_ids(struct list *idmap, pid_t pid)
{
	int fill, left;
	char u_or_g;
	char mapbuf[STRLITERALLEN("new@idmap") + STRLITERALLEN(" ") +
		    INTTYPE_TO_STRLEN(pid_t) + STRLITERALLEN(" ") + IDMAPLEN] = {};
	bool had_entry = false;

	for (idmap_type_t map_type = ID_TYPE_UID, u_or_g = 'u';
	     map_type <= ID_TYPE_GID; map_type++, u_or_g = 'g') {
		char *pos = mapbuf;
		int ret;
		struct list *iterator;


		list_for_each(iterator, idmap) {
			struct id_map *map = iterator->elem;
			if (map->map_type != map_type)
				continue;

			had_entry = true;

			left = IDMAPLEN - (pos - mapbuf);
			fill = snprintf(pos, left, "%u %u %u\n", map->nsid, map->hostid, map->range);
			/*
			 * The kernel only takes <= 4k for writes to
			 * /proc/<pid>/{g,u}id_map
			 */
			if (fill <= 0 || fill >= left)
				return syserror_set(-E2BIG, "Too many %cid mappings defined", u_or_g);

			pos += fill;
		}
		if (!had_entry)
			continue;

		ret = write_id_mapping(map_type, pid, mapbuf, pos - mapbuf);
		if (ret < 0)
			return syserror("Failed to write mapping: %s", mapbuf);

		memset(mapbuf, 0, sizeof(mapbuf));
	}

	return 0;
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret < 0) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (ret != pid)
		goto again;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	return 0;
}

static int get_userns_fd(struct list *idmap)
{
	int ret;
	pid_t pid;
	char path_ns[STRLITERALLEN("/proc") + INTTYPE_TO_STRLEN(pid_t) +
		  STRLITERALLEN("/ns/user") + 1];

	pid = do_clone(clone_cb, NULL, CLONE_NEWUSER | CLONE_NEWNS);
	if (pid < 0)
		return -errno;

	ret = map_ids(idmap, pid);
	if (ret < 0)
		return ret;

	ret = snprintf(path_ns, sizeof(path_ns), "/proc/%d/ns/user", pid);
	if (ret < 0 || (size_t)ret >= sizeof(path_ns))
		ret = -EIO;
	else
		ret = open(path_ns, O_RDONLY | O_CLOEXEC | O_NOCTTY);

	(void)kill(pid, SIGKILL);
	(void)wait_for_pid(pid);
	return ret;
}

static inline bool strnequal(const char *str, const char *eq, size_t len)
{
	return strncmp(str, eq, len) == 0;
}

static void usage(void)
{
	const char *text = "\
mount-idmapped --map-mount=<idmap> <source> <target>\n\
\n\
Create an idmapped mount of <source> at <target>\n\
Options:\n\
  --map-mount=<idmap>\n\
	Specify an idmap for the <target> mount in the format\n\
	<idmap-type>:<id-from>:<id-to>:<id-range>\n\
	The <idmap-type> can be:\n\
	\"b\" or \"both\"	-> map both uids and gids\n\
	\"u\" or \"uid\"	-> map uids\n\
	\"g\" or \"gid\"	-> map gids\n\
	For example, specifying:\n\
	both:1000:1001:1	-> map uid and gid 1000 to uid and gid 1001 in <target> and no other ids\n\
	uid:20000:100000:1000	-> map uid 20000 to uid 100000, uid 20001 to uid 100001 [...] in <target>\n\
	Currently up to 340 separate idmappings may be specified.\n\n\
  --map-mount=/proc/<pid>/ns/user\n\
	Specify a path to a user namespace whose idmap is to be used.\n\n\
  --map-caller=<idmap>\n\
        Specify an idmap to be used for the caller, i.e. move the caller into a new user namespace\n\
	with the requested mapping.\n\n\
  --recursive\n\
	Copy the whole mount tree from <source> and apply the idmap to everyone at <target>.\n\n\
Examples:\n\
  - Create an idmapped mount of /source on /target with both ('b') uids and gids mapped:\n\
	mount-idmapped --map-mount b:0:10000:10000 /source /target\n\n\
  - Create an idmapped mount of /source on /target with uids ('u') and gids ('g') mapped separately:\n\
	mount-idmapped --map-mount u:0:10000:10000 g:0:20000:20000 /source /target\n\n\
  - Create an idmapped mount of /source on /target with both ('b') uids and gids mapped and a user namespace\n\
    with both ('b') uids and gids mapped:\n\
	mount-idmapped --map-caller b:0:10000:10000 --map-mount b:0:10000:1000 /source /target\n\n\
  - Create an idmapped mount of /source on /target with uids ('u') gids ('g') mapped separately\n\
    and a user namespace with both ('b') uids and gids mapped:\n\
	mount-idmapped --map-caller u:0:10000:10000 g:0:20000:20000 --map-mount b:0:10000:1000 /source /target\n\
";
	fprintf(stderr, "%s", text);
	_exit(EXIT_SUCCESS);
}

#define exit_usage(format, ...)                         \
	({                                              \
		fprintf(stderr, format, ##__VA_ARGS__); \
		usage();                                \
	})

#define exit_log(format, ...)                           \
	({                                              \
		fprintf(stderr, format, ##__VA_ARGS__); \
		exit(EXIT_FAILURE);                     \
	})

static const struct option longopts[] = {
	{"map-mount",	required_argument,	0,	'a'},
	{"map-caller",	required_argument,	0,	'b'},
	{"help",	no_argument,		0,	'c'},
	{"recursive",	no_argument,		0,	'd'},
	NULL,
};

int main(int argc, char *argv[])
{
	int fd_userns = -EBADF;
	int index = 0;
	const char *caller_idmap = NULL, *source = NULL, *target = NULL;
	bool recursive = false;
	int fd_tree, new_argc, ret;
	char *const *new_argv;

	list_init(&active_map);
	while ((ret = getopt_long_only(argc, argv, "", longopts, &index)) != -1) {
		switch (ret) {
		case 'a':
			if (strnequal(optarg, "/proc", STRLITERALLEN("/proc/"))) {
				fd_userns = open(optarg, O_RDONLY | O_CLOEXEC);
				if (fd_userns < 0)
					exit_log("%m - Failed top open user namespace path %s\n", optarg);
				break;
			}

			ret = parse_map(optarg);
			if (ret < 0)
				exit_log("Failed to parse idmaps for mount\n");
			break;
		case 'b':
			caller_idmap = optarg;
			break;
		case 'd':
			recursive = true;
			break;
		case 'c':
			/* fallthrough */
		default:
			usage();
		}
	}

	new_argv = &argv[optind];
	new_argc = argc - optind;
	if (new_argc < 2)
		exit_usage("Missing source or target mountpoint\n\n");
	source = new_argv[0];
	target = new_argv[1];

	fd_tree = sys_open_tree(-EBADF, source,
				OPEN_TREE_CLONE |
				OPEN_TREE_CLOEXEC |
				AT_EMPTY_PATH |
				recursive ? AT_RECURSIVE : 0);
	if (fd_tree < 0) {
		exit_log("%m - Failed to open %s\n", source);
		exit(EXIT_FAILURE);
	}

	if (!list_empty(&active_map)) {
		struct mount_attr attr = {
			.attr_set = MOUNT_ATTR_IDMAP,
		};

		attr.userns_fd = get_userns_fd(&active_map);
		if (attr.userns_fd < 0)
			exit_log("%m - Failed to create user namespace\n");

		ret = sys_mount_setattr(fd_tree, "", AT_EMPTY_PATH | AT_RECURSIVE, &attr,
				sizeof(attr));
		if (ret < 0)
			exit_log("%m - Failed to change mount attributes\n");
		close(attr.userns_fd);
	}

	ret = sys_move_mount(fd_tree, "", -EBADF, target, MOVE_MOUNT_F_EMPTY_PATH);
	if (ret < 0)
		exit_log("%m - Failed to attach mount to %s\n", target);
	close(fd_tree);

	if (caller_idmap) {
		execlp("lxc-usernsexec", "lxc-usernsexec", "-m", caller_idmap, "bash", (char *)NULL);
		exit_log("Note that moving the caller into a new user namespace requires \"lxc-usernsexec\" to be installed\n");
	}
	exit(EXIT_SUCCESS);
}
