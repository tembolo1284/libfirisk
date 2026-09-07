/*
 * libfirisk Excel add-in.
 *
 * Stateless worksheet functions: every call takes the full bond
 * description and rebuilds the schedule. A fifteen-cashflow bond builds
 * in microseconds, so this costs nothing measurable and avoids a handle
 * registry with its stale-reference problems across workbook reloads.
 *
 * Excel calls the functions below by name. Registration in xlAutoOpen
 * tells it which DLL export each name maps to and what argument types
 * to marshal.
 */

#include <string>
#include <vector>

#include "marshal.hpp"
#include "xlcall.h"
#include "firisk.h"

using namespace firisk_xll;

namespace {

/* ------------------------------------------------------------------ */
/* a context and bond per call                                         */
/* ------------------------------------------------------------------ */

/* RAII over the two handles, so every early return releases them.
   Excel recalculates on multiple threads, so nothing here is shared. */
class Session {
public:
    Session() : ctx_(fir_context_new()), bond_(nullptr) {}

    ~Session()
    {
        fir_bond_free(bond_);
        fir_context_free(ctx_);
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    bool ok() const { return ctx_ != nullptr; }

    fir_context_t *ctx() const { return ctx_; }
    fir_bond_t *bond() const { return bond_; }

    /* Builds the bond. Returns the status so the caller can map it to
       an Excel error. */
    fir_status_t build(fir_date_t valuation,
                       fir_date_t issue,
                       fir_date_t maturity,
                       double coupon,
                       double face,
                       int frequency,
                       int daycount,
                       int bdc,
                       fir_date_t first_coupon)
    {
        if (!ctx_)
            return FIR_E_ALLOC;

        fir_status_t rv = fir_context_set_valuation_date(ctx_, valuation);
        if (rv != FIR_OK)
            return rv;

        fir_bond_def_t def;
        std::memset(&def, 0, sizeof def);

        def.struct_size             = sizeof def;
        def.issue_date              = issue;
        def.first_coupon_date       = first_coupon;
        def.maturity_date           = maturity;
        def.coupon_rate             = coupon;
        def.face                    = face;
        def.frequency               = frequency;
        def.daycount                = daycount;
        def.business_day_convention = bdc;
        def.end_of_month            = 0;
        def.calendar                = nullptr;

        bond_ = fir_bond_new(ctx_, &def);
        if (!bond_)
            return fir_context_last_status(ctx_);

        return FIR_OK;
    }

private:
    fir_context_t *ctx_;
    fir_bond_t    *bond_;
};

/* The common leading arguments every bond function takes. Parsed once
   here so the functions themselves stay short. */
struct BondArgs {
    fir_date_t valuation;
    fir_date_t issue;
    fir_date_t maturity;
    double     coupon;
    double     face;
    int        frequency;
    int        daycount;
    int        bdc;
    fir_date_t first_coupon;

    int error;   /* xlerr value, or xlerrNull when fine */
};

BondArgs parse_bond(const XLOPER12 *valuation,
                    const XLOPER12 *issue,
                    const XLOPER12 *maturity,
                    const XLOPER12 *coupon,
                    const XLOPER12 *frequency,
                    const XLOPER12 *daycount,
                    const XLOPER12 *face,
                    const XLOPER12 *bdc,
                    const XLOPER12 *first_coupon)
{
    BondArgs args;
    std::memset(&args, 0, sizeof args);
    args.error = xlerrNull;

    args.valuation = to_yyyymmdd(valuation);
    args.issue     = to_yyyymmdd(issue);
    args.maturity  = to_yyyymmdd(maturity);

    if (!args.valuation || !args.issue || !args.maturity) {
        args.error = xlerrValue;
        return args;
    }

    if (!to_double(coupon, &args.coupon)) {
        args.error = xlerrValue;
        return args;
    }

    args.face = to_double_or(face, 100.0);

    args.frequency = parse_frequency(frequency);
    if (args.frequency < 0) {
        args.error = xlerrValue;
        return args;
    }

    /* Day count defaults to 30/360 bond basis, the most common
       convention for a fixed rate corporate or Treasury. */
    if (is_missing(daycount)) {
        args.daycount = FIR_DC_THIRTY_360_BOND;
    } else {
        std::string text;
        if (!to_string(daycount, &text)) {
            args.daycount = to_int_or(daycount, -1);
        } else {
            args.daycount = parse_daycount(text);
        }

        if (args.daycount < 0) {
            args.error = xlerrValue;
            return args;
        }
    }

    args.bdc = parse_bdc(bdc);
    if (args.bdc < 0) {
        args.error = xlerrValue;
        return args;
    }

    args.first_coupon = is_missing(first_coupon)
                            ? 0
                            : to_yyyymmdd(first_coupon);

    return args;
}

/* Builds a session from parsed arguments, returning the Excel error on
   failure. */
int open_session(Session *session, const BondArgs &args)
{
    if (!session->ok())
        return xlerrValue;

    const fir_status_t rv = session->build(
        args.valuation, args.issue, args.maturity, args.coupon,
        args.face, args.frequency, args.daycount, args.bdc,
        args.first_coupon);

    if (rv != FIR_OK)
        return status_to_xlerr(rv);

    return xlerrNull;
}

/* Which analytic measure a call wants. */
enum Measure {
    kPrice = 0,
    kAccrued,
    kDirty,
    kMacaulay,
    kModified,
    kConvexity,
    kDv01,
    kYield
};

/* Every scalar analytic function funnels through here. */
XLOPER12 *analytic(Measure what,
                   const XLOPER12 *valuation,
                   const XLOPER12 *issue,
                   const XLOPER12 *maturity,
                   const XLOPER12 *coupon,
                   const XLOPER12 *rate,
                   const XLOPER12 *frequency,
                   const XLOPER12 *daycount,
                   const XLOPER12 *face,
                   const XLOPER12 *compounding,
                   const XLOPER12 *bdc,
                   const XLOPER12 *first_coupon)
{
    Result result;

    const BondArgs args = parse_bond(valuation, issue, maturity, coupon,
                                     frequency, daycount, face, bdc,
                                     first_coupon);
    if (args.error != xlerrNull) {
        result.set_error(args.error);
        return result.release();
    }

    double input = 0.0;
    if (!to_double(rate, &input)) {
        result.set_error(xlerrValue);
        return result.release();
    }

    const int comp = parse_compounding(compounding);
    if (comp < 0) {
        result.set_error(xlerrValue);
        return result.release();
    }

    Session session;
    const int open_error = open_session(&session, args);
    if (open_error != xlerrNull) {
        result.set_error(open_error);
        return result.release();
    }

    double out = 0.0;
    fir_status_t rv = FIR_OK;

    switch (what) {
    case kPrice:
        rv = fir_bond_price_from_yield(session.bond(), input, comp, &out);
        break;

    case kYield:
        /* Here the input is a price, not a yield. */
        rv = fir_bond_yield_from_price(session.bond(), input, comp, &out);
        break;

    case kAccrued:
        rv = fir_bond_accrued(session.bond(), &out);
        break;

    case kDirty: {
        double clean = 0.0, accrued = 0.0;
        rv = fir_bond_price_from_yield(session.bond(), input, comp, &clean);
        if (rv == FIR_OK)
            rv = fir_bond_accrued(session.bond(), &accrued);
        out = clean + accrued;
        break;
    }

    case kMacaulay:
        rv = fir_bond_macaulay_duration(session.bond(), input, comp, &out);
        break;

    case kModified:
        rv = fir_bond_modified_duration(session.bond(), input, comp, &out);
        break;

    case kConvexity:
        rv = fir_bond_convexity(session.bond(), input, comp, &out);
        break;

    case kDv01:
        rv = fir_bond_dv01(session.bond(), input, comp, &out);
        break;
    }

    if (rv != FIR_OK) {
        result.set_error(status_to_xlerr(rv));
        return result.release();
    }

    result.set_number(out);
    return result.release();
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* worksheet functions                                                 */
/* ------------------------------------------------------------------ */

extern "C" {

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_price(XLOPER12 *valuation, XLOPER12 *issue,
                              XLOPER12 *maturity, XLOPER12 *coupon,
                              XLOPER12 *yield, XLOPER12 *frequency,
                              XLOPER12 *daycount, XLOPER12 *face,
                              XLOPER12 *compounding, XLOPER12 *bdc,
                              XLOPER12 *first_coupon)
{
    return analytic(kPrice, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_yield(XLOPER12 *valuation, XLOPER12 *issue,
                              XLOPER12 *maturity, XLOPER12 *coupon,
                              XLOPER12 *price, XLOPER12 *frequency,
                              XLOPER12 *daycount, XLOPER12 *face,
                              XLOPER12 *compounding, XLOPER12 *bdc,
                              XLOPER12 *first_coupon)
{
    return analytic(kYield, valuation, issue, maturity, coupon, price,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_accrued(XLOPER12 *valuation, XLOPER12 *issue,
                                XLOPER12 *maturity, XLOPER12 *coupon,
                                XLOPER12 *frequency, XLOPER12 *daycount,
                                XLOPER12 *face, XLOPER12 *bdc,
                                XLOPER12 *first_coupon)
{
    XLOPER12 zero;
    std::memset(&zero, 0, sizeof zero);
    zero.xltype = xltypeNum;
    zero.val.num = 0.0;

    return analytic(kAccrued, valuation, issue, maturity, coupon, &zero,
                    frequency, daycount, face, nullptr, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_dirty(XLOPER12 *valuation, XLOPER12 *issue,
                              XLOPER12 *maturity, XLOPER12 *coupon,
                              XLOPER12 *yield, XLOPER12 *frequency,
                              XLOPER12 *daycount, XLOPER12 *face,
                              XLOPER12 *compounding, XLOPER12 *bdc,
                              XLOPER12 *first_coupon)
{
    return analytic(kDirty, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_macaulay(XLOPER12 *valuation, XLOPER12 *issue,
                                 XLOPER12 *maturity, XLOPER12 *coupon,
                                 XLOPER12 *yield, XLOPER12 *frequency,
                                 XLOPER12 *daycount, XLOPER12 *face,
                                 XLOPER12 *compounding, XLOPER12 *bdc,
                                 XLOPER12 *first_coupon)
{
    return analytic(kMacaulay, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_duration(XLOPER12 *valuation, XLOPER12 *issue,
                                 XLOPER12 *maturity, XLOPER12 *coupon,
                                 XLOPER12 *yield, XLOPER12 *frequency,
                                 XLOPER12 *daycount, XLOPER12 *face,
                                 XLOPER12 *compounding, XLOPER12 *bdc,
                                 XLOPER12 *first_coupon)
{
    return analytic(kModified, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_convexity(XLOPER12 *valuation, XLOPER12 *issue,
                                  XLOPER12 *maturity, XLOPER12 *coupon,
                                  XLOPER12 *yield, XLOPER12 *frequency,
                                  XLOPER12 *daycount, XLOPER12 *face,
                                  XLOPER12 *compounding, XLOPER12 *bdc,
                                  XLOPER12 *first_coupon)
{
    return analytic(kConvexity, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_dv01(XLOPER12 *valuation, XLOPER12 *issue,
                             XLOPER12 *maturity, XLOPER12 *coupon,
                             XLOPER12 *yield, XLOPER12 *frequency,
                             XLOPER12 *daycount, XLOPER12 *face,
                             XLOPER12 *compounding, XLOPER12 *bdc,
                             XLOPER12 *first_coupon)
{
    return analytic(kDv01, valuation, issue, maturity, coupon, yield,
                    frequency, daycount, face, compounding, bdc,
                    first_coupon);
}

/* Two columns: time in years, and amount. Spills in modern Excel;
   older versions need Ctrl+Shift+Enter over a selected block. */
__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_cashflows(XLOPER12 *valuation, XLOPER12 *issue,
                                  XLOPER12 *maturity, XLOPER12 *coupon,
                                  XLOPER12 *frequency, XLOPER12 *daycount,
                                  XLOPER12 *face, XLOPER12 *bdc,
                                  XLOPER12 *first_coupon)
{
    Result result;

    const BondArgs args = parse_bond(valuation, issue, maturity, coupon,
                                     frequency, daycount, face, bdc,
                                     first_coupon);
    if (args.error != xlerrNull) {
        result.set_error(args.error);
        return result.release();
    }

    Session session;
    const int open_error = open_session(&session, args);
    if (open_error != xlerrNull) {
        result.set_error(open_error);
        return result.release();
    }

    size_t count = 0;
    fir_status_t rv = fir_bond_cashflow_count(session.bond(), &count);
    if (rv != FIR_OK || count == 0) {
        result.set_error(status_to_xlerr(rv));
        return result.release();
    }

    std::vector<double> times(count);
    std::vector<double> amounts(count);

    rv = fir_bond_cashflows(session.bond(), times.data(), amounts.data(),
                            count);
    if (rv != FIR_OK) {
        result.set_error(status_to_xlerr(rv));
        return result.release();
    }

    result.set_two_columns(times, amounts);
    return result.release();
}

/* Every analytic measure at one yield, as a column. Saves rebuilding
   the same bond eight times when a sheet wants all of them. */
__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_risk(XLOPER12 *valuation, XLOPER12 *issue,
                             XLOPER12 *maturity, XLOPER12 *coupon,
                             XLOPER12 *yield, XLOPER12 *frequency,
                             XLOPER12 *daycount, XLOPER12 *face,
                             XLOPER12 *compounding, XLOPER12 *bdc,
                             XLOPER12 *first_coupon)
{
    Result result;

    const BondArgs args = parse_bond(valuation, issue, maturity, coupon,
                                     frequency, daycount, face, bdc,
                                     first_coupon);
    if (args.error != xlerrNull) {
        result.set_error(args.error);
        return result.release();
    }

    double y = 0.0;
    if (!to_double(yield, &y)) {
        result.set_error(xlerrValue);
        return result.release();
    }

    const int comp = parse_compounding(compounding);
    if (comp < 0) {
        result.set_error(xlerrValue);
        return result.release();
    }

    Session session;
    const int open_error = open_session(&session, args);
    if (open_error != xlerrNull) {
        result.set_error(open_error);
        return result.release();
    }

    double clean = 0.0, accrued = 0.0, dmac = 0.0;
    double dmod = 0.0, convexity = 0.0, dv01 = 0.0;

    fir_bond_t *bond = session.bond();

    fir_status_t rv = fir_bond_price_from_yield(bond, y, comp, &clean);
    if (rv == FIR_OK) rv = fir_bond_accrued(bond, &accrued);
    if (rv == FIR_OK) rv = fir_bond_macaulay_duration(bond, y, comp, &dmac);
    if (rv == FIR_OK) rv = fir_bond_modified_duration(bond, y, comp, &dmod);
    if (rv == FIR_OK) rv = fir_bond_convexity(bond, y, comp, &convexity);
    if (rv == FIR_OK) rv = fir_bond_dv01(bond, y, comp, &dv01);

    if (rv != FIR_OK) {
        result.set_error(status_to_xlerr(rv));
        return result.release();
    }

    const std::vector<double> values = {
        clean, accrued, clean + accrued, dmac, dmod, convexity, dv01
    };

    result.set_column(values);
    return result.release();
}

__declspec(dllexport)
XLOPER12 *WINAPI fir_xl_version(void)
{
    Result result;
    result.set_string(fir_get_version_string());
    return result.release();
}

} /* extern "C" */

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

namespace {

/* Argument type codes for xlfRegister.
 *
 *   Q  an XLOPER12 passed by value semantics (we take a pointer)
 *   $  return an XLOPER12 the DLL frees
 *   !  volatile, recalculate on every change
 *   #  accept missing arguments as xltypeMissing rather than erroring
 *
 * The leading character is the return type; each subsequent character
 * is one argument. Trailing '#' makes optional arguments work. */
struct Registration {
    const wchar_t *export_name;
    const wchar_t *type_text;
    const wchar_t *function_name;
    const wchar_t *arg_names;
    const wchar_t *description;
};

const Registration kFunctions[] = {
    {
        L"fir_xl_price", L"QQQQQQQQQQQQ#", L"FIR.PRICE",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Clean price of a fixed rate bond from its yield."
    },
    {
        L"fir_xl_yield", L"QQQQQQQQQQQQ#", L"FIR.YIELD",
        L"valuation, issue, maturity, coupon, price, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Yield to maturity from a clean price."
    },
    {
        L"fir_xl_accrued", L"QQQQQQQQQQ#", L"FIR.ACCRUED",
        L"valuation, issue, maturity, coupon, frequency, daycount, "
        L"face, bdc, firstCoupon",
        L"Accrued interest as of the valuation date."
    },
    {
        L"fir_xl_dirty", L"QQQQQQQQQQQQ#", L"FIR.DIRTY",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Dirty price: clean price plus accrued interest."
    },
    {
        L"fir_xl_macaulay", L"QQQQQQQQQQQQ#", L"FIR.MACAULAY",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Macaulay duration: PV-weighted average time to cashflows."
    },
    {
        L"fir_xl_duration", L"QQQQQQQQQQQQ#", L"FIR.DURATION",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Modified duration: percentage price change per unit yield."
    },
    {
        L"fir_xl_convexity", L"QQQQQQQQQQQQ#", L"FIR.CONVEXITY",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Convexity: the second order price-yield sensitivity."
    },
    {
        L"fir_xl_dv01", L"QQQQQQQQQQQQ#", L"FIR.DV01",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Price change for a one basis point yield move."
    },
    {
        L"fir_xl_cashflows", L"QQQQQQQQQQ#", L"FIR.CASHFLOWS",
        L"valuation, issue, maturity, coupon, frequency, daycount, "
        L"face, bdc, firstCoupon",
        L"Remaining cashflows as two columns: time in years, amount."
    },
    {
        L"fir_xl_risk", L"QQQQQQQQQQQQ#", L"FIR.RISK",
        L"valuation, issue, maturity, coupon, yield, frequency, "
        L"daycount, face, compounding, bdc, firstCoupon",
        L"Clean, accrued, dirty, Macaulay, modified, convexity, DV01 "
        L"as a column."
    },
    {
        L"fir_xl_version", L"Q", L"FIR.VERSION",
        L"",
        L"Version of the underlying libfirisk library."
    },
};

const int kFunctionCount =
    static_cast<int>(sizeof(kFunctions) / sizeof(kFunctions[0]));

/* Path to this DLL, which xlfRegister needs as its first argument. */
bool this_dll_path(XLOPER12 *out)
{
    std::memset(out, 0, sizeof *out);
    return Excel12(xlGetName, out, 0) == xlretSuccess;
}

} /* namespace */

extern "C" {

__declspec(dllexport)
int WINAPI xlAutoOpen(void)
{
    if (!firisk_xll_bind_excel())
        return 0;

    XLOPER12 dll_path;
    if (!this_dll_path(&dll_path))
        return 0;

    XLOPER12 macro_type;
    std::memset(&macro_type, 0, sizeof macro_type);
    macro_type.xltype = xltypeInt;
    macro_type.val.w = 1;

    for (int i = 0; i < kFunctionCount; ++i) {
        const Registration &fn = kFunctions[i];

        TempString export_name(fn.export_name);
        TempString type_text(fn.type_text);
        TempString function_name(fn.function_name);
        TempString arg_names(fn.arg_names);
        TempString category(L"libfirisk");
        TempString empty(L"");
        TempString description(fn.description);

        XLOPER12 registered;
        std::memset(&registered, 0, sizeof registered);

        Excel12(xlfRegister, &registered, 10,
                &dll_path,
                export_name.get(),
                type_text.get(),
                function_name.get(),
                arg_names.get(),
                /* macro type: 1 = a worksheet function */
                @macro_type,
                category.get(),
                empty.get(),   /* shortcut key */
                empty.get(),   /* help topic */
                description.get());

        /* Excel allocated the result, so it frees it. */
        Excel12(xlFree, nullptr, 1, &registered);
    }

    Excel12(xlFree, nullptr, 1, &dll_path);

    return 1;
}

__declspec(dllexport)
int WINAPI xlAutoClose(void)
{
    return 1;
}

/* Excel shows this in the add-in manager. */
__declspec(dllexport)
XLOPER12 *WINAPI xlAddInManagerInfo12(XLOPER12 *action)
{
    static XLOPER12 info;
    static std::vector<XCHAR> name;

    double which = 0.0;
    if (!to_double(action, &which) || static_cast<int>(which) != 1) {
        std::memset(&info, 0, sizeof info);
        info.xltype = xltypeErr;
        info.val.err = xlerrValue;
        return &info;
    }

    const wchar_t *text = L"libfirisk fixed income analytics";
    const size_t length = std::wcslen(text);

    name.resize(length + 1);
    name[0] = static_cast<XCHAR>(length);
    std::memcpy(&name[1], text, length * sizeof(XCHAR));

    std::memset(&info, 0, sizeof info);
    info.xltype = xltypeStr;
    info.val.str = name.data();

    return &info;
}

/* Excel calls this back for every XLOPER12 we returned with
   xlbitDLLFree set. This is the other half of the Result contract. */
__declspec(dllexport)
void WINAPI xlAutoFree12(XLOPER12 *x)
{
    free_result(x);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        /* Nothing to initialise: the library has no global state
           beyond its allocator table, which defaults correctly. */
    }
    return TRUE;
}

} /* extern "C" */
