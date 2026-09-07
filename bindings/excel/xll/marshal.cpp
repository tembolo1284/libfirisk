#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "marshal.hpp"

/* ------------------------------------------------------------------ */
/* the Excel callback                                                  */
/* ------------------------------------------------------------------ */

extern "C" EXCEL12PROC pExcel12v = nullptr;

extern "C" int firisk_xll_bind_excel(void)
{
    if (pExcel12v)
        return 1;

    /* The export lives in the running Excel executable, not in a
       library we link, so it is resolved out of the process image. */
    HMODULE excel = GetModuleHandleW(nullptr);
    if (!excel)
        return 0;

    pExcel12v = reinterpret_cast<EXCEL12PROC>(
        GetProcAddress(excel, "MdCallBack12"));

    return pExcel12v != nullptr;
}

extern "C" int Excel12(int xlfn, XLOPER12 *operRes, int count, ...)
{
    if (!pExcel12v && !firisk_xll_bind_excel())
        return xlretFailed;

    if (count > 30)
        return xlretInvCount;

    XLOPER12 *opers[30];

    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; ++i)
        opers[i] = va_arg(ap, XLOPER12 *);
    va_end(ap);

    return pExcel12v(xlfn, count, opers, operRes);
}

namespace firisk_xll {

namespace {

std::string to_lower(const std::string &text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return out;
}

/* Strips spaces so "modified following" and "modfollowing" both work. */
std::string squash(const std::string &text)
{
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '_' && c != '-')
            out.push_back(c);
    }
    return to_lower(out);
}

/* Excel serial to YYYYMMDD. Serial 1 is 1900-01-01, and Excel treats
   1900 as a leap year, so serials at or past 61 (1900-03-01) are one
   greater than the true day count. Anchoring on 1970 avoids the
   quirk entirely for any date after 1900-03-01. */
fir_date_t serial_to_yyyymmdd(double serial)
{
    if (serial < 61.0 || serial > 2958465.0)
        return 0;

    /* Serial 25569 is 1970-01-01. */
    const long long days = static_cast<long long>(serial) - 25569;

    /* Hinnant's civil_from_days, the same algorithm the core uses. */
    long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long long doe =
        static_cast<unsigned long long>(z - era * 146097);
    const unsigned long long yoe =
        (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long long mp = (5 * doy + 2) / 153;
    const unsigned long long d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned long long m = mp + (mp < 10 ? 3 : -9);

    const long long year = y + (m <= 2 ? 1 : 0);

    return static_cast<fir_date_t>(year * 10000 + m * 100 + d);
}

fir_date_t parse_iso_date(const std::string &text)
{
    int y = 0, m = 0, d = 0;

    /* Accepts 2024-02-15 and 2024/02/15. */
    if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3 &&
        std::sscanf(text.c_str(), "%d/%d/%d", &y, &m, &d) != 3)
        return 0;

    if (y < 1000 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
        return 0;

    return static_cast<fir_date_t>(y * 10000 + m * 100 + d);
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* reading arguments                                                   */
/* ------------------------------------------------------------------ */

bool is_missing(const XLOPER12 *x)
{
    if (!x)
        return true;

    const DWORD type = x->xltype & ~(xlbitXLFree | xlbitDLLFree);
    return type == xltypeMissing || type == xltypeNil;
}

bool to_double(const XLOPER12 *x, double *out)
{
    if (!x || !out)
        return false;

    const DWORD type = x->xltype & ~(xlbitXLFree | xlbitDLLFree);

    switch (type) {
    case xltypeNum:
        *out = x->val.num;
        return true;
    case xltypeInt:
        *out = static_cast<double>(x->val.w);
        return true;
    case xltypeBool:
        *out = x->val.xbool ? 1.0 : 0.0;
        return true;
    case xltypeMulti:
        /* A one-cell range reads as its single value. */
        if (x->val.array.rows == 1 && x->val.array.columns == 1 &&
            x->val.array.lparray)
            return to_double(&x->val.array.lparray[0], out);
        return false;
    default:
        return false;
    }
}

double to_double_or(const XLOPER12 *x, double fallback)
{
    double value = 0.0;
    if (is_missing(x) || !to_double(x, &value))
        return fallback;
    return value;
}

int to_int_or(const XLOPER12 *x, int fallback)
{
    double value = 0.0;
    if (is_missing(x) || !to_double(x, &value))
        return fallback;
    return static_cast<int>(value);
}

bool to_string(const XLOPER12 *x, std::string *out)
{
    if (!x || !out)
        return false;

    const DWORD type = x->xltype & ~(xlbitXLFree | xlbitDLLFree);

    if (type == xltypeMulti) {
        if (x->val.array.rows == 1 && x->val.array.columns == 1 &&
            x->val.array.lparray)
            return to_string(&x->val.array.lparray[0], out);
        return false;
    }

    if (type != xltypeStr || !x->val.str)
        return false;

    /* Length lives in the first character, not a null terminator. */
    const int length = static_cast<int>(x->val.str[0]);
    if (length <= 0) {
        out->clear();
        return true;
    }

    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, x->val.str + 1, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return false;

    out->assign(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, x->val.str + 1, length,
                        &(*out)[0], bytes, nullptr, nullptr);

    return true;
}

bool to_doubles(const XLOPER12 *x, std::vector<double> *out)
{
    if (!x || !out)
        return false;

    out->clear();

    const DWORD type = x->xltype & ~(xlbitXLFree | xlbitDLLFree);

    if (type == xltypeMulti) {
        if (!x->val.array.lparray)
            return false;

        const int count = x->val.array.rows * x->val.array.columns;
        out->reserve(static_cast<size_t>(count));

        for (int i = 0; i < count; ++i) {
            const XLOPER12 *cell = &x->val.array.lparray[i];
            const DWORD cell_type =
                cell->xltype & ~(xlbitXLFree | xlbitDLLFree);

            /* Trailing blanks in a selected range are ignored rather
               than rejected: users select whole columns. */
            if (cell_type == xltypeMissing || cell_type == xltypeNil)
                continue;

            double value = 0.0;
            if (!to_double(cell, &value))
                return false;

            out->push_back(value);
        }

        return true;
    }

    double single = 0.0;
    if (!to_double(x, &single))
        return false;

    out->push_back(single);
    return true;
}

/* ------------------------------------------------------------------ */
/* dates                                                               */
/* ------------------------------------------------------------------ */

fir_date_t to_yyyymmdd(const XLOPER12 *x)
{
    if (is_missing(x))
        return 0;

    std::string text;
    if (to_string(x, &text))
        return parse_iso_date(text);

    double value = 0.0;
    if (!to_double(x, &value))
        return 0;

    /* A value large enough to be a YYYYMMDD is taken as one; anything
       smaller is an Excel serial. The two ranges do not overlap for
       any date this library handles. */
    if (value >= 10000101.0)
        return static_cast<fir_date_t>(value);

    return serial_to_yyyymmdd(value);
}

/* ------------------------------------------------------------------ */
/* conventions                                                         */
/* ------------------------------------------------------------------ */

int parse_daycount(const std::string &text)
{
    const std::string key = squash(text);

    if (key == "act/360" || key == "actual/360" || key == "a/360")
        return FIR_DC_ACT_360;
    if (key == "act/365f" || key == "act/365" || key == "actual/365")
        return FIR_DC_ACT_365F;
    if (key == "act/actisda" || key == "actualactualisda" ||
        key == "act/act(isda)")
        return FIR_DC_ACT_ACT_ISDA;
    if (key == "act/act" || key == "act/acticma" ||
        key == "actualactual" || key == "act/act(icma)")
        return FIR_DC_ACT_ACT_ICMA;
    if (key == "30/360" || key == "30/360bond" || key == "bond" ||
        key == "30/360us")
        return FIR_DC_THIRTY_360_BOND;
    if (key == "30e/360" || key == "30/360e" || key == "eurobond")
        return FIR_DC_THIRTY_E_360;

    return -1;
}

int parse_frequency(const XLOPER12 *x)
{
    if (is_missing(x))
        return FIR_FREQ_SEMIANNUAL;

    std::string text;
    if (to_string(x, &text)) {
        const std::string key = squash(text);

        if (key == "zero" || key == "none" || key == "bullet")
            return FIR_FREQ_ZERO;
        if (key == "annual" || key == "yearly" || key == "a")
            return FIR_FREQ_ANNUAL;
        if (key == "semiannual" || key == "semi" || key == "s")
            return FIR_FREQ_SEMIANNUAL;
        if (key == "quarterly" || key == "quarter" || key == "q")
            return FIR_FREQ_QUARTERLY;
        if (key == "monthly" || key == "month" || key == "m")
            return FIR_FREQ_MONTHLY;

        return -1;
    }

    double value = 0.0;
    if (!to_double(x, &value))
        return -1;

    const int n = static_cast<int>(value);
    switch (n) {
    case 0:  return FIR_FREQ_ZERO;
    case 1:  return FIR_FREQ_ANNUAL;
    case 2:  return FIR_FREQ_SEMIANNUAL;
    case 4:  return FIR_FREQ_QUARTERLY;
    case 12: return FIR_FREQ_MONTHLY;
    default: return -1;
    }
}

int parse_compounding(const XLOPER12 *x)
{
    if (is_missing(x))
        return FIR_COMP_PERIODIC;

    std::string text;
    if (to_string(x, &text)) {
        const std::string key = squash(text);

        if (key == "simple")
            return FIR_COMP_SIMPLE;
        if (key == "periodic" || key == "compound" || key == "bond")
            return FIR_COMP_PERIODIC;
        if (key == "continuous" || key == "cont" || key == "exp")
            return FIR_COMP_CONTINUOUS;

        return -1;
    }

    const int n = to_int_or(x, FIR_COMP_PERIODIC);
    if (n < FIR_COMP_SIMPLE || n > FIR_COMP_CONTINUOUS)
        return -1;

    return n;
}

int parse_bdc(const XLOPER12 *x)
{
    if (is_missing(x))
        return FIR_BDC_NONE;

    std::string text;
    if (to_string(x, &text)) {
        const std::string key = squash(text);

        if (key == "none" || key == "unadjusted")
            return FIR_BDC_NONE;
        if (key == "following" || key == "f")
            return FIR_BDC_FOLLOWING;
        if (key == "modifiedfollowing" || key == "modfollowing" ||
            key == "mf")
            return FIR_BDC_MODIFIED_FOLLOWING;
        if (key == "preceding" || key == "p")
            return FIR_BDC_PRECEDING;

        return -1;
    }

    const int n = to_int_or(x, FIR_BDC_NONE);
    if (n < FIR_BDC_NONE || n > FIR_BDC_PRECEDING)
        return -1;

    return n;
}

/* ------------------------------------------------------------------ */
/* returning values                                                    */
/* ------------------------------------------------------------------ */

Result::Result() : value_(nullptr)
{
    value_ = static_cast<XLOPER12 *>(std::calloc(1, sizeof(XLOPER12)));
    if (value_)
        value_->xltype = xltypeNil | xlbitDLLFree;
}

Result::~Result()
{
    /* Only reached if release() was never called, which means the
       value never went to Excel. */
    if (value_)
        free_result(value_);
}

void Result::set_number(double value)
{
    if (!value_)
        return;

    value_->xltype = xltypeNum | xlbitDLLFree;
    value_->val.num = value;
}

void Result::set_bool(bool value)
{
    if (!value_)
        return;

    value_->xltype = xltypeBool | xlbitDLLFree;
    value_->val.xbool = value ? 1 : 0;
}

void Result::set_error(int xlerr)
{
    if (!value_)
        return;

    value_->xltype = xltypeErr | xlbitDLLFree;
    value_->val.err = xlerr;
}

void Result::set_string(const std::string &utf8)
{
    if (!value_)
        return;

    const int chars = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
        nullptr, 0);

    if (chars <= 0 || chars > 32767) {
        set_error(xlerrValue);
        return;
    }

    /* One extra character for the leading length word. */
    XCHAR *buffer = static_cast<XCHAR *>(
        std::malloc(sizeof(XCHAR) * static_cast<size_t>(chars + 1)));
    if (!buffer) {
        set_error(xlerrValue);
        return;
    }

    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()),
                        buffer + 1, chars);
    buffer[0] = static_cast<XCHAR>(chars);

    value_->xltype = xltypeStr | xlbitDLLFree;
    value_->val.str = buffer;
}

void Result::set_matrix(const std::vector<double> &values,
                        int rows, int columns)
{
    if (!value_)
        return;

    if (rows <= 0 || columns <= 0 ||
        values.size() != static_cast<size_t>(rows) *
                         static_cast<size_t>(columns)) {
        set_error(xlerrValue);
        return;
    }

    const size_t count = values.size();

    XLOPER12 *cells = static_cast<XLOPER12 *>(
        std::calloc(count, sizeof(XLOPER12)));
    if (!cells) {
        set_error(xlerrValue);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        cells[i].xltype = xltypeNum;
        cells[i].val.num = values[i];
    }

    value_->xltype = xltypeMulti | xlbitDLLFree;
    value_->val.array.lparray = cells;
    value_->val.array.rows = rows;
    value_->val.array.columns = columns;
}

void Result::set_column(const std::vector<double> &values)
{
    set_matrix(values, static_cast<int>(values.size()), 1);
}

void Result::set_two_columns(const std::vector<double> &left,
                             const std::vector<double> &right)
{
    if (left.size() != right.size()) {
        set_error(xlerrValue);
        return;
    }

    std::vector<double> interleaved;
    interleaved.reserve(left.size() * 2);

    /* Row major: Excel reads left to right, then down. */
    for (size_t i = 0; i < left.size(); ++i) {
        interleaved.push_back(left[i]);
        interleaved.push_back(right[i]);
    }

    set_matrix(interleaved, static_cast<int>(left.size()), 2);
}

XLOPER12 *Result::release()
{
    XLOPER12 *out = value_;
    value_ = nullptr;
    return out;
}

void free_result(XLOPER12 *x)
{
    if (!x)
        return;

    const DWORD type = x->xltype & ~(xlbitXLFree | xlbitDLLFree);

    if (type == xltypeStr && x->val.str) {
        std::free(x->val.str);
    } else if (type == xltypeMulti && x->val.array.lparray) {
        /* The cells hold only numbers, so no per-cell free is needed;
           if that ever changes this loop has to recurse. */
        std::free(x->val.array.lparray);
    }

    std::free(x);
}

int status_to_xlerr(fir_status_t status)
{
    switch (status) {
    case FIR_OK:
        return xlerrNull;
    case FIR_E_NO_CONVERGENCE:
        return xlerrNum;
    case FIR_E_NULL_ARG:
    case FIR_E_BAD_ARG:
    case FIR_E_BAD_DATE:
    case FIR_E_BAD_SCHEDULE:
    case FIR_E_BAD_STRUCT_SIZE:
        return xlerrValue;
    case FIR_E_UNSUPPORTED:
    case FIR_E_NO_BUMP_SUPPORT:
        return xlerrNA;
    case FIR_E_BUFFER_TOO_SMALL:
    case FIR_E_ALLOC:
    default:
        return xlerrValue;
    }
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

TempString::TempString(const wchar_t *text)
{
    const size_t length = std::wcslen(text);

    buffer_.resize(length + 1);
    buffer_[0] = static_cast<XCHAR>(length);
    std::memcpy(&buffer_[1], text, length * sizeof(XCHAR));

    std::memset(&value_, 0, sizeof value_);
    value_.xltype = xltypeStr;
    value_.val.str = buffer_.data();
}

TempString::~TempString() = default;

} /* namespace firisk_xll */
