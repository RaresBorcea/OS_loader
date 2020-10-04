/*
 * Loader Implementation
 *
 * 2020, Operating Systems
 */

#define DLL_EXPORTS

#include <stdio.h>
#include <Windows.h>
#include <string.h>
#include <stdlib.h>

#include "exec_parser.h"
#include "utils.h"
#include "loader.h"

static so_exec_t *exec;
static HANDLE hFile;
static LPVOID access_violation_handler;
static int page_size = 0x10000;

/* Page fault handler. Based on the page being already mapped or not,
 * the handler will either give the control back to the default
 * handler, in case of segmentation fault or for an already mapped
 * page - the page fault signals an attempt to access memory without
 * required permissions - or will map the page in memory at the
 * address indicated by the so_segment with its permissions and
 * copying the data from the executable to the now allocated memory.
 */
static LONG CALLBACK access_violation(PEXCEPTION_POINTERS ExceptionInfo)
{
	so_seg_t *segments = exec->segments;
	unsigned int segments_no = exec->segments_no;
	unsigned int i, pos;
	so_seg_t *curr_segment = NULL;
	int *data = NULL;
	unsigned int data_size = 0;
	unsigned int map_offset = 0;
	long bytes_read = 0;
	long bytes_read_now = 0;
	long bytes_to_read = 0;
	DWORD ret = 0;
	DWORD old;
	int perm = 0;
	LPBYTE addr;
	LPVOID res;
	BOOL isRead = FALSE, isWrite = FALSE, isExec = FALSE;
	BOOL bRet;

	addr = (LPBYTE)ExceptionInfo->ExceptionRecord->ExceptionInformation[1];

	/* Find segment which contains this address */
	for (i = 0; i < segments_no; i++)
		if ((uintptr_t)addr >= segments[i].vaddr
			&& (uintptr_t)addr < segments[i].vaddr
			+ segments[i].mem_size) {
			curr_segment = &(segments[i]);
			data_size = (segments[i].mem_size + page_size - 1)
				/ page_size;
			break;
		}

	if (curr_segment == NULL)
		return EXCEPTION_CONTINUE_SEARCH;

	/* Allocate custom data to mark mapped pages */
	pos = ((uintptr_t)addr - curr_segment->vaddr) / page_size;
	if (curr_segment->data == NULL) {
		data = calloc(data_size, sizeof(int));
		DIE(data == NULL, "HANDLER: Unable to allocate custom data");
		curr_segment->data = data;
	} else if (((int *)curr_segment->data)[pos] == 1) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	/* Get permissions of current segmnent */
	if ((curr_segment->perm & PERM_R) == PERM_R)
		isRead = TRUE;
	if ((curr_segment->perm & PERM_W) == PERM_W)
		isWrite = TRUE;
	if ((curr_segment->perm & PERM_X) == PERM_X)
		isExec = TRUE;
	if (isRead && isWrite && isExec)
		perm = PAGE_EXECUTE_READWRITE;
	else if (isRead && isExec)
		perm = PAGE_EXECUTE_READ;
	else if (isRead && isWrite)
		perm = PAGE_READWRITE;
	else if (isRead)
		perm = PAGE_READONLY;
	else if (isExec)
		perm = PAGE_EXECUTE;

	addr = (LPBYTE)curr_segment->vaddr + pos * page_size;

	/* Map page in memory */
	res = VirtualAlloc(addr, page_size, MEM_COMMIT|MEM_RESERVE,
		PAGE_EXECUTE_READWRITE);
	DIE(res == NULL, "HANDLER: VirtualAlloc");

	if (curr_segment->offset + page_size * (pos + 1) <=
			curr_segment->offset + curr_segment->file_size)
		bytes_to_read = page_size;
	else
		bytes_to_read = curr_segment->offset + curr_segment->file_size
			- (curr_segment->offset + page_size * pos);

	/* Read from executable to memory page */
	ret = SetFilePointer(hFile,
			curr_segment->offset + page_size * pos,
			NULL,
			FILE_BEGIN);
	DIE(ret == INVALID_SET_FILE_POINTER, "HANDLER: SetFilePointer");

	while (bytes_read < bytes_to_read) {
		bRet = ReadFile(hFile,
				res,
				bytes_to_read - bytes_read,
				&bytes_read_now,
				NULL);

		DIE(bRet == FALSE, "HANDLER: ReadFile");

		bytes_read += bytes_read_now;
	}

	/* Mark page as mapped and correct permissions if required */
	((int *)curr_segment->data)[pos] = 1;

	bRet = VirtualProtect(res, page_size, perm, &old);
	DIE(bRet == FALSE, "HANDLER: VirtualProtect");

	return EXCEPTION_CONTINUE_EXECUTION;
}

/* Initialize on-demand loader for SIGSEGV signal */
int so_init_loader(void)
{
	access_violation_handler =
		AddVectoredExceptionHandler(1, access_violation);
	DIE(access_violation_handler == NULL, "AddVctoredExceptionHandler");

	return 0;
}

/* Runs an executable specified in the path and frees
 * allocated memory
 */
int so_execute(char *path, char *argv[])
{
	BOOL bRet;
	unsigned int j;
	int i;
	so_seg_t *curr_segment = NULL;
	LPBYTE segm_start;
	int *data;
	unsigned int data_size;

	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	hFile = CreateFile(path,
			GENERIC_READ|GENERIC_EXECUTE|GENERIC_WRITE,
			FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
	DIE(hFile == INVALID_HANDLE_VALUE,
		"SO_EXECUTE: Couldn't open executable");

	so_start_exec(exec, argv);

	for (i = 0; i < exec->segments_no; i++) {
		curr_segment = &(exec->segments[i]);
		data = (int *)(curr_segment->data);

		if (data != NULL) {
			data_size = (curr_segment->mem_size + page_size - 1)
				/ page_size;
			segm_start = (LPBYTE)curr_segment->vaddr;
			for (j = 0; j < data_size; j++)
				if (data[j] == 1) {
					bRet = VirtualFree(segm_start +
						page_size * j, 0, MEM_RELEASE);
					DIE(bRet == FALSE,
						"SO_EXECUTE: VirtualFree");
				}

			free(data);
		}
	}

	bRet = CloseHandle(hFile);

	return (bRet == FALSE ? -1 : 0);
}
