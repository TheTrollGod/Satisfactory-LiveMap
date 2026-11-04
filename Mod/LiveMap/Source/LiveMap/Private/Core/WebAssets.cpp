#include "WebAssets.h"

#if __has_include("WebAssets.inc")
// Defines LiveMapWebFiles[] and LiveMapWebFileCount.
#include "WebAssets.inc"
#define LIVEMAP_HAS_WEB 1
#else
#define LIVEMAP_HAS_WEB 0
#endif

namespace livemap
{
	const EmbeddedFile* GetWebFiles(size_t& Count)
	{
#if LIVEMAP_HAS_WEB
		Count = LiveMapWebFileCount;
		return LiveMapWebFiles;
#else
		Count = 0;
		return nullptr;
#endif
	}
}

#undef LIVEMAP_HAS_WEB
