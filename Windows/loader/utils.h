#ifndef WIN_UTILS_H_
#define WIN_UTILS_H_

#include <windows.h>
#include <stdio.h>

/** Macro for handling error codes */
#define DIE(assertion, call_description)		\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
			__FILE__, __LINE__);	\
			PrintLastError(call_description);	\
			exit(EXIT_FAILURE);			\
		}				\
	} while (0)

static VOID PrintLastError(const PCHAR message)
{
	CHAR errBuff[1024];

	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL,
		GetLastError(),
		0,
		errBuff,
		sizeof(errBuff) - 1,
		NULL);

	fprintf(stderr, "%s: %s\n", message, errBuff);
}

#endif /* WIN_UTILS_H_ */
