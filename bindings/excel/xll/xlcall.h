/*
 * Minimal Excel XLL ABI declarations.
 *
 * This replaces Microsoft's xlcall.h, which is no longer reliably
 * distributed. Only what libfirisk's add-in needs is declared here: the
 * XLOPER12 union, the handful of callback opcodes used for registration,
 * and the two entry points Excel exports.
 *
 * The layout of XLOPER12 is fixed by Excel and must not be changed. The
 * struct is 32 bytes on x64: a 24-byte union followed by a 4-byte type
 * tag and 4 bytes of padding.
 */

#ifndef FIRISK_XLCALL_H_INCLUDED
#define FIRISK_XLCALL_H_INCLUDED

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

typedef INT32  XLOPER_INT;
typedef WORD   XLOPER_BOOL;
typedef INT32  XLOPER_ERR;

/* Excel counts strings with a leading length word, not a null
   terminator, and uses UTF-16. A string is at most 32767 characters,
   so the length always fits in the first wchar_t. */
typedef wchar_t XCHAR;

struct xloper12;

/* Excel's own reference type. Declared for completeness because it
   appears in the union; this add-in never constructs one. */
typedef struct xlref12 {
    INT32 rwFirst;
    INT32 rwLast;
    INT32 colFirst;
    INT32 colLast;
} XLREF12;

typedef struct xlmref12 {
    WORD    count;
    XLREF12 reftbl[1];
} XLMREF12;

typedef struct xloper12 {
    union {
        double num;                 /* xltypeNum   */
        XCHAR *str;                 /* xltypeStr   */
        XLOPER_BOOL xbool;          /* xltypeBool  */
        XLOPER_ERR err;             /* xltypeErr   */
        INT32 w;                    /* xltypeInt   */

        struct {
            WORD count;
            XLMREF12 *lpmref;
            DWORD_PTR idSheet;
        } mref;                     /* xltypeRef   */

        struct {
            struct xloper12 *lparray;
            INT32 rows;
            INT32 columns;
        } array;                    /* xltypeMulti */

        struct {
            union {
                short level;
                short tbctrl;
                DWORD_PTR idSheet;
            } valflow;
            WORD rw;
            WORD col;
            BYTE xlflow;
        } flow;                     /* xltypeFlow  */

        struct {
            union {
                BYTE *lpbData;
                HANDLE hdata;
            } h;
            long cbData;
        } bigdata;                  /* xltypeBigData */

        struct {
            XLREF12 ref;
            DWORD_PTR idSheet;
        } sref;                     /* xltypeSRef  */
    } val;

    DWORD xltype;
} XLOPER12;

/* ------------------------------------------------------------------ */
/* type tags                                                           */
/* ------------------------------------------------------------------ */

#define xltypeNum        0x0001
#define xltypeStr        0x0002
#define xltypeBool       0x0004
#define xltypeRef        0x0008
#define xltypeErr        0x0010
#define xltypeFlow       0x0020
#define xltypeMulti      0x0040
#define xltypeMissing    0x0080
#define xltypeNil        0x0100
#define xltypeSRef       0x0400
#define xltypeInt        0x0800

#define xlbitXLFree      0x1000   /* Excel frees this */
#define xlbitDLLFree     0x2000   /* we free this, via xlAutoFree12 */

#define xltypeBigData    (xltypeStr | xltypeInt)

/* ------------------------------------------------------------------ */
/* error values                                                        */
/* ------------------------------------------------------------------ */

#define xlerrNull        0
#define xlerrDiv0        7
#define xlerrValue       15
#define xlerrRef         23
#define xlerrName        29
#define xlerrNum         36
#define xlerrNA          42
#define xlerrGettingData 43

/* ------------------------------------------------------------------ */
/* return codes from Excel12                                           */
/* ------------------------------------------------------------------ */

#define xlretSuccess          0
#define xlretAbort            1
#define xlretInvXlfn          2
#define xlretInvCount         4
#define xlretInvXloper        8
#define xlretStackOvfl       16
#define xlretFailed          32
#define xlretUncalced        64
#define xlretNotThreadSafe  128
#define xlretInvAsynchronousContext 256
#define xlretNotClusterSafe 512

/* ------------------------------------------------------------------ */
/* the function numbers this add-in uses                               */
/* ------------------------------------------------------------------ */

#define xlfRegister      149
#define xlfUnregister    201
#define xlFree        (0 | 0x1000)
#define xlGetName    (33 | 0x1000)
#define xlfCaller     89

/* ------------------------------------------------------------------ */
/* the two functions Excel exports for us to call                      */
/*                                                                     */
/* These live in the Excel process, not in a library we link, so they   */
/* are resolved with GetProcAddress at load time rather than at link.   */
/* ------------------------------------------------------------------ */

typedef int (__cdecl *EXCEL12PROC)(int xlfn, int coper,
                                   XLOPER12 **rgpxloper12,
                                   XLOPER12 *xloper12Res);

extern EXCEL12PROC pExcel12v;

/* Resolves pExcel12v out of the running Excel process. Returns zero if
   the add-in is not loaded inside Excel. */
int firisk_xll_bind_excel(void);

/* Varargs wrapper matching the classic Excel12 signature. */
int Excel12(int xlfn, XLOPER12 *operRes, int count, ...);

#ifdef __cplusplus
}
#endif

#endif /* FIRISK_XLCALL_H_INCLUDED */
