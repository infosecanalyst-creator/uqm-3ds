/* Heap set-up for the 3DS (replaces libctru's __system_allocateHeaps). UQ-04.
 * libctru's default commits every free byte (32 MB linear + the rest as heap). From
 * real hardware (Nov 13-15 2026, the shared heap3ds pattern from opensgolf-3ds):
 *  - wait up to 3 s for the previous app's memory to be released before sizing;
 *  - leave 4 MB uncommitted. With every byte committed, the next program the Homebrew
 *    loader starts after this one quits fails to load (D86007F3 in "loader", or a
 *    svcBreak in the Homebrew Menu's own heap set-up).
 * Sizes otherwise match libctru's defaults for this port: 32 MB linear (framebuffers,
 * audio), the rest heap (capped at the 96 MB heap area; any excess goes to linear). */
#include <3ds.h>
#include <3ds/allocator/mappable.h>

extern char *fake_heap_start;
extern char *fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

void
__system_allocateHeaps (void)
{
	Handle reslimit = 0;
	s64 maxCommit = 0, currentCommit = 0;
	ResourceLimitType type = RESLIMIT_COMMIT;
	const u32 reserve = 4u << 20;
	const u32 heap_max = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
	const u32 linear_want = 32u << 20;
	u32 remaining, heap;
	int tries;

	if (R_FAILED (svcGetResourceLimit (&reslimit, CUR_PROCESS_HANDLE)))
		svcBreak (USERBREAK_PANIC);
	for (tries = 0; tries < 300; tries++)
	{
		svcGetResourceLimitLimitValues (&maxCommit, reslimit, &type, 1);
		svcGetResourceLimitCurrentValues (&currentCommit, reslimit, &type, 1);
		if (maxCommit - currentCommit >= (64ll << 20))
			break;
		svcSleepThread (10000000LL);
	}
	svcCloseHandle (reslimit);

	remaining = (u32) (maxCommit - currentCommit) & ~0xFFFu;
	remaining = remaining > reserve * 4 ? remaining - reserve : remaining;
	heap = remaining > linear_want ? remaining - linear_want : remaining / 2;
	if (heap > heap_max)
		heap = heap_max;
	heap &= ~0xFFFu;
	__ctru_heap_size = heap;
	__ctru_linear_heap_size = remaining - heap;

	if (R_FAILED (svcControlMemory (&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size,
			MEMOP_ALLOC, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE))))
		svcBreak (USERBREAK_PANIC);
	if (R_FAILED (svcControlMemory (&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
			MEMOP_ALLOC_LINEAR, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE))))
		svcBreak (USERBREAK_PANIC);
	mappableInit (OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	fake_heap_start = (char *) __ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;
}
