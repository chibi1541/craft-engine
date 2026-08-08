#pragma once

#define NAME_SPACE_BEGIN(name)		namespace name {
#define NAME_SPACE_END				} 

#define DLLEXPORT			__declspec(dllexport)
#define DLLIMPORT			__declspec(dllimport)

#if ENGINE_BUILD_DLL
#define CRAFT_API			DLLEXPORT
#else
#define CRAFT_API			DLLIMPORT
#endif


/*---------------
	  Crash
---------------*/

#define CRASH(cause)						\
{											\
	uint32* crash = nullptr;				\
	__analysis_assume(crash != nullptr);	\
	*crash = 0xDEADBEEF;					\
}

#define ASSERT_CRASH(expr)			\
{									\
	if (!(expr))					\
	{								\
		CRASH("ASSERT_CRASH");		\
		__analysis_assume(expr);	\
	}								\
}