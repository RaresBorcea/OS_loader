/*
 * Loader Implementation
 *
 * 2020, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "exec_parser.h"
#include "utils.h"

#define DLL_EXPORTS

static so_exec_t *exec;
static int fd;
static struct sigaction old_sa;
static unsigned int page_size;

/* Page fault handler. Based on the page being already mapped or not,
 * the handler will either give the control back to the default
 * handler, in case of segmentation fault or for an already mapped
 * page - the page fault signals an attempt to access memory without
 * required permissions - or will map the page in memory at the
 * address indicated by the so_segment with its permissions and
 * copying the data from the executable to the now allocated memory.
 */
static void pagefault_handler(int signo, siginfo_t *info, void *context)
{
	uintptr_t addr = (uintptr_t)info->si_addr;
	so_seg_t *segments = exec->segments;
	unsigned int segments_no = exec->segments_no;
	int i, pos;
	so_seg_t *curr_segment = NULL;
	void *res = NULL;
	int *data = NULL;
	unsigned int data_size = 0;
	unsigned int perm = 0;
	long bytes_read = 0;
	long bytes_read_now = 0;
	long bytes_to_read = 0;
	int ret = 0;
	int restore_nonwrite = 0;

	if (signo != SIGSEGV) {
		old_sa.sa_sigaction(signo, info, context);
		return;
	}

	/* Find segment which contains this address */
	for (i = 0; i < segments_no; i++)
		if (addr >= segments[i].vaddr && addr < segments[i].vaddr
			+ segments[i].mem_size) {
			curr_segment = &(segments[i]);
			data_size = (segments[i].mem_size + page_size - 1)
				/ page_size;
			break;
		}

	if (curr_segment == NULL) {
		old_sa.sa_sigaction(signo, info, context);
		return;
	}

	/* Allocate custom data to mark mapped pages */
	pos = (addr - curr_segment->vaddr) / page_size;
	if (curr_segment->data == NULL) {
		data = calloc(data_size, sizeof(int));
		DIE(data == NULL, "HANDLER: Unable to allocate custom data");
		curr_segment->data = data;
	} else if (((int *)curr_segment->data)[pos] == 1) {
		old_sa.sa_sigaction(signo, info, context);
		return;
	}

	/* Get permissions of current segmnent */
	if ((curr_segment->perm & PERM_R) == PERM_R)
		perm |= PROT_READ;
	if ((curr_segment->perm & PERM_W) == PERM_W)
		perm |= PROT_WRITE;
	else
		restore_nonwrite = 1;
	if ((curr_segment->perm & PERM_X) == PERM_X)
		perm |= PROT_EXEC;

	addr = curr_segment->vaddr + pos * page_size;

	/* Map page in memory */
	res = mmap((void *)(addr), page_size, perm | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	DIE(res == MAP_FAILED, "HANDLER: Unable to map memory");

	if (curr_segment->offset + page_size * (pos + 1) <=
			curr_segment->offset + curr_segment->file_size)
		bytes_to_read = page_size;
	else
		bytes_to_read = curr_segment->offset + curr_segment->file_size
			- (curr_segment->offset + page_size * pos);

	/* Read from executable to memory page */
	ret = lseek(fd, curr_segment->offset + page_size * pos, SEEK_SET);
	DIE(ret == -1, "HANDLER: Lseek error");
	while (bytes_read < bytes_to_read) {
		bytes_read_now = read(fd, res, bytes_to_read - bytes_read);

		DIE(bytes_read_now <= 0, "HANDLER: Unable to read from file");

		bytes_read += bytes_read_now;
	}

	/* Mark page as mapped and correct permissions if required */
	((int *)curr_segment->data)[pos] = 1;

	if (restore_nonwrite == 1) {
		ret = mprotect(res, page_size, perm);
		DIE(ret == -1, "HANDLER: Mprotect error");
	}
}

/* Initialize on-demand loader for SIGSEGV signal */
int so_init_loader(void)
{
	struct sigaction sa;
	int rc;

	sa.sa_sigaction = pagefault_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGSEGV);
	sa.sa_flags = SA_SIGINFO;

	rc = sigaction(SIGSEGV, &sa, &old_sa);

	return rc;
}

/* Runs an executable specified in the path and frees
 * allocated memory
 */
int so_execute(char *path, char *argv[])
{
	int ret = 0;
	int i, j;
	so_seg_t *curr_segment = NULL;
	void *segm_start = NULL;
	int *data;
	unsigned int data_size;

	page_size = getpagesize();

	fd = open(path, O_RDONLY);
	DIE(fd < 0, "SO_EXECUTE: Couldn't open executable");

	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	so_start_exec(exec, argv);

	for (i = 0; i < exec->segments_no; i++) {
		curr_segment = &(exec->segments[i]);
		data = (int *)(curr_segment->data);

		if (data != NULL) {
			data_size = (curr_segment->mem_size + page_size - 1)
				/ page_size;
			segm_start = (void *)curr_segment->vaddr;
			for (j = 0; j < data_size; j++)
				if (data[j] == 1)
					ret |= munmap(segm_start + page_size
						* j, page_size);

			free(data);
		}
	}

	ret |= close(fd);

	return ret;
}
