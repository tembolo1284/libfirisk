/*
 * XLOPER12 marshalling.
 *
 * Everything crossing the Excel boundary is an XLOPER12, and the
 * memory rules are the whole difficulty: a returned string or array is
 * allocated by us and freed by Excel calling back into xlAutoFree12.
 * The Result type below owns that contract so no call site has to
 * remember to set xlbitDLLFree.
 */

#ifndef FIRISK_XLL_MARSHAL_HPP_INCLUDED
#define FIRISK_XLL_MARSHAL_HPP_INCLUDED

#include <string>
#include <vector>

#include "xlcall.h"
#include "firisk.h"

namespace firisk_xll {

/* ------------------------------------------------------------------ */
/* reading arguments                                                   */
/* ------------------------------------------------------------------ */

/* Excel passes a missing optional argument as xltypeMissing, and an
   empty cell as xltypeNil. Both mean "not supplied". */
bool is_missing(const XLOPER12 *x);

/* Numeric coercion. Excel may hand us xltypeNum, xltypeInt, xltypeBool,
   or a single-cell array, so each is accepted. Returns false for
   anything that is not a number, including strings and errors. */
bool to_double(const XLOPER12 *x, double *out);

/* Same, with a default when the argument is missing. */
double to_double_or(const XLOPER12 *x, double fallback);

int to_int_or(const XLOPER12 *x, int fallback);

/* Reads an Excel string as UTF-8. Excel strings are length-prefixed
   UTF-16 with no null terminator. */
bool to_string(const XLOPER12 *x, std::string *out);

/* Reads a column or row of numbers out of a range. A single cell gives
   a one-element vector. Returns false on any non-numeric entry. */
bool to_doubles(const XLOPER12 *x, std::vector<double> *out);

/* ------------------------------------------------------------------ */
/* dates                                                               */
/* ------------------------------------------------------------------ */

/* Excel dates are serial days from 1900-01-01, with the well-known
   1900 leap year bug. This accepts a serial, an ISO string, or an
   integer already in YYYYMMDD form, and returns the YYYYMMDD the C ABI
   wants. Returns 0 on failure. */
fir_date_t to_yyyymmdd(const XLOPER12 *x);

/* ------------------------------------------------------------------ */
/* conventions from strings                                            */
/* ------------------------------------------------------------------ */

/* Case-insensitive. "ACT/360", "30/360", "30E/360", "ACT/365F",
   "ACT/ACT" (ICMA), "ACT/ACT ISDA". Returns -1 if unrecognised. */
int parse_daycount(const std::string &text);

/* Accepts a number (0, 1, 2, 4, 12) or a name ("annual",
   "semiannual", "quarterly", "monthly", "zero"). Returns -1 if
   unrecognised. */
int parse_frequency(const XLOPER12 *x);

/* "simple", "periodic", "continuous". Defaults to periodic. */
int parse_compounding(const XLOPER12 *x);

/* "none", "following", "modified following"/"modfollowing",
   "preceding". Defaults to none. */
int parse_bdc(const XLOPER12 *x);

/* ------------------------------------------------------------------ */
/* returning values                                                    */
/* ------------------------------------------------------------------ */

/* A returned XLOPER12 that Excel will free via xlAutoFree12.
 *
 * Excel reads the value after the function returns, so the storage
 * cannot be a local and cannot be a shared static (that would break
 * under Excel's multithreaded recalculation). Each Result heap
 * allocates, marks itself xlbitDLLFree, and Excel calls back to
 * release it. release() hands the pointer over; after that the Result
 * no longer owns it. */
class Result {
public:
    Result();
    ~Result();

    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;

    void set_number(double value);
    void set_string(const std::string &utf8);
    void set_bool(bool value);
    void set_error(int xlerr);

    /* A column of values, one per row. */
    void set_column(const std::vector<double> &values);

    /* A rectangular block, row-major. */
    void set_matrix(const std::vector<double> &values,
                    int rows, int columns);

    /* Two columns side by side, for cashflow times and amounts. */
    void set_two_columns(const std::vector<double> &left,
                         const std::vector<double> &right);

    /* Transfers ownership to Excel. */
    XLOPER12 *release();

private:
    XLOPER12 *value_;
};

/* Frees an XLOPER12 previously handed to Excel with xlbitDLLFree set.
   Called only from xlAutoFree12. */
void free_result(XLOPER12 *x);

/* Maps a fir_status_t onto the closest Excel error. */
int status_to_xlerr(fir_status_t status);

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

/* A temporary XLOPER12 holding a string, for the registration calls.
   Registration happens once on the main thread, so a simple scoped
   holder is enough. */
class TempString {
public:
    explicit TempString(const wchar_t *text);
    ~TempString();

    TempString(const TempString &) = delete;
    TempString &operator=(const TempString &) = delete;

    XLOPER12 *get() { return &value_; }

private:
    XLOPER12 value_;
    std::vector<XCHAR> buffer_;
};

} /* namespace firisk_xll */

#endif /* FIRISK_XLL_MARSHAL_HPP_INCLUDED */
