#ifndef FIRISK_H_INCLUDED
#define FIRISK_H_INCLUDED

#ifndef FIR_NOINCLUDE
#  include <stddef.h>
#  include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* export markers                                                      */
/* ------------------------------------------------------------------ */

#ifndef FIR_API
#  ifdef _WIN32
#    if defined(FIR_BUILD_SHARED)
#      define FIR_API __declspec(dllexport)
#    elif !defined(FIR_BUILD_STATIC)
#      define FIR_API __declspec(dllimport)
#    else
#      define FIR_API
#    endif
#  else
#    if defined(__GNUC__) && __GNUC__ >= 4
#      define FIR_API __attribute__((visibility("default")))
#    else
#      define FIR_API
#    endif
#  endif
#endif

/* ------------------------------------------------------------------ */
/* version                                                             */
/* ------------------------------------------------------------------ */

#define FIR_VERSION_MAJOR 0
#define FIR_VERSION_MINOR 1
#define FIR_VERSION_PATCH 0
#define FIR_VERSION ((FIR_VERSION_MAJOR << 16) | \
                     (FIR_VERSION_MINOR <<  8) | \
                      FIR_VERSION_PATCH)

FIR_API unsigned int fir_get_version(void);
FIR_API int          fir_is_compatible_dll(void);
FIR_API const char  *fir_get_version_string(void);

/* ------------------------------------------------------------------ */
/* allocators and panic handling                                       */
/* ------------------------------------------------------------------ */

/* Set before the first fir_context_new; these are process-global. */
FIR_API void  fir_set_allocators(void *(*f_malloc)(size_t),
                                 void *(*f_realloc)(void *, size_t),
                                 void  (*f_free)(void *));
FIR_API void *fir_malloc(size_t size);
FIR_API void *fir_realloc(void *ptr, size_t size);
FIR_API void  fir_free(void *ptr);

/* Called when the library cannot continue: allocation failure, or an
   invariant violation. Must not return. If unset, the library writes to
   stderr and calls abort(). Set this before the first fir_context_new. */
typedef void (*fir_panic_fn)(const char *message);

FIR_API void fir_set_panic_handler(fir_panic_fn handler);

/* ------------------------------------------------------------------ */
/* status codes                                                        */
/* ------------------------------------------------------------------ */

enum fir_status_e {
    FIR_OK                  =  0,
    FIR_E_NULL_ARG          = -1,
    FIR_E_BAD_STRUCT_SIZE   = -2,
    FIR_E_BAD_ARG           = -3,
    FIR_E_BAD_DATE          = -4,
    FIR_E_BAD_SCHEDULE      = -5,
    FIR_E_NO_CONVERGENCE    = -6,
    FIR_E_BUFFER_TOO_SMALL  = -7,
    FIR_E_UNSUPPORTED       = -8,
    FIR_E_NO_BUMP_SUPPORT   = -9,
    FIR_E_ALLOC             = -10
};
typedef int fir_status_t;

FIR_API const char *fir_strerror(fir_status_t status);

/* ------------------------------------------------------------------ */
/* conventions                                                         */
/* ------------------------------------------------------------------ */

enum fir_daycount_e {
    FIR_DC_ACT_360 = 0,
    FIR_DC_ACT_365F,
    FIR_DC_ACT_ACT_ISDA,
    FIR_DC_ACT_ACT_ICMA,
    FIR_DC_THIRTY_360_BOND,
    FIR_DC_THIRTY_E_360
};
typedef int fir_daycount_t;

enum fir_frequency_e {
    FIR_FREQ_ZERO        = 0,
    FIR_FREQ_ANNUAL      = 1,
    FIR_FREQ_SEMIANNUAL  = 2,
    FIR_FREQ_QUARTERLY   = 4,
    FIR_FREQ_MONTHLY     = 12
};
typedef int fir_frequency_t;

/* Compounding used to interpret a quoted yield. FIR_COMP_PERIODIC uses
   the bond's own coupon frequency as k; a zero-coupon bond uses k = 1. */
enum fir_compounding_e {
    FIR_COMP_SIMPLE = 0,
    FIR_COMP_PERIODIC,
    FIR_COMP_CONTINUOUS
};
typedef int fir_compounding_t;

enum fir_bdc_e {
    FIR_BDC_NONE = 0,
    FIR_BDC_FOLLOWING,
    FIR_BDC_MODIFIED_FOLLOWING,
    FIR_BDC_PRECEDING
};
typedef int fir_bdc_t;

/* Dates are YYYYMMDD, e.g. 20260831. */
typedef int32_t fir_date_t;

/* ------------------------------------------------------------------ */
/* calendar                                                            */
/* ------------------------------------------------------------------ */

/* Bit per weekday, 0 = Sunday .. 6 = Saturday. */
enum fir_weekend_e {
    FIR_WEEKEND_NONE     = 0,
    FIR_WEEKEND_SAT_SUN  = (1 << 0) | (1 << 6),
    FIR_WEEKEND_FRI_SAT  = (1 << 5) | (1 << 6)
};

struct fir_calendar_s;
typedef struct fir_calendar_s fir_calendar_t;

/* Defaults to Saturday/Sunday weekends and no holidays. The library
   ships no holiday tables; supply them here. */
FIR_API fir_calendar_t *fir_calendar_new(void);
FIR_API void            fir_calendar_free(fir_calendar_t *cal);

/* Copies `dates`; the caller's array need not outlive the call. */
FIR_API fir_status_t fir_calendar_set_holidays(fir_calendar_t   *cal,
                                               const fir_date_t *dates,
                                               size_t            n);
FIR_API fir_status_t fir_calendar_set_weekend(fir_calendar_t *cal,
                                              int             weekend_mask);
FIR_API int          fir_calendar_is_business_day(const fir_calendar_t *cal,
                                                  fir_date_t            date);

/* ------------------------------------------------------------------ */
/* opaque handles                                                      */
/* ------------------------------------------------------------------ */

struct fir_context_s;
typedef struct fir_context_s fir_context_t;

struct fir_bond_s;
typedef struct fir_bond_s fir_bond_t;

/* ------------------------------------------------------------------ */
/* context                                                             */
/* ------------------------------------------------------------------ */

FIR_API fir_context_t *fir_context_new(void);
FIR_API void           fir_context_free(fir_context_t *ctx);

FIR_API fir_status_t fir_context_set_valuation_date(fir_context_t *ctx,
                                                    fir_date_t     date);
FIR_API fir_status_t fir_context_set_solver_tolerance(fir_context_t *ctx,
                                                      double         tol);
FIR_API fir_status_t fir_context_set_solver_max_iter(fir_context_t *ctx,
                                                     int            max_iter);
/* Bump size used by all effective/bump-revalue risk, in basis points.
   Must be in [0.01, 1000]; defaults to 1.0. */
FIR_API fir_status_t fir_context_set_bump_size(fir_context_t *ctx,
                                               double         bp);

FIR_API fir_status_t fir_context_last_status(const fir_context_t *ctx);

/* Points into the context's internal buffer. Valid only until the next
   call taking this context. Copy it if you need to keep it. */
FIR_API const char *fir_context_last_message(const fir_context_t *ctx);

/* ------------------------------------------------------------------ */
/* instrument definition (caller-allocated, versioned POD)             */
/* ------------------------------------------------------------------ */

typedef struct fir_bond_def_s {
    size_t          struct_size;       /* = sizeof(fir_bond_def_t)      */
    fir_date_t      issue_date;
    fir_date_t      first_coupon_date; /* 0 = derive from maturity      */
    fir_date_t      maturity_date;
    double          coupon_rate;       /* annual, decimal: 0.045        */
    double          face;              /* redemption amount, e.g. 100   */
    fir_frequency_t frequency;
    fir_daycount_t  daycount;
    fir_bdc_t       business_day_convention;
    int             end_of_month;      /* 0 or 1                        */

    /* NULL = weekends only. Borrowed for the duration of the
       fir_bond_new call only; the bond retains adjusted dates, not the
       calendar, so it need not outlive that call. */
    const fir_calendar_t *calendar;
} fir_bond_def_t;

FIR_API fir_bond_t *fir_bond_new(fir_context_t        *ctx,
                                 const fir_bond_def_t *def);
FIR_API void        fir_bond_free(fir_bond_t *bond);

FIR_API fir_status_t fir_bond_cashflow_count(const fir_bond_t *bond,
                                             size_t           *out_count);

/* Fills up to n entries; either output pointer may be NULL. Times are
   year fractions from the valuation date under ACT/365F. */
FIR_API fir_status_t fir_bond_cashflows(const fir_bond_t *bond,
                                        double           *out_times,
                                        double           *out_amounts,
                                        size_t            n);
FIR_API fir_status_t fir_bond_accrued(const fir_bond_t *bond,
                                      double           *out_accrued);

/* ------------------------------------------------------------------ */
/* discount source (caller-allocated vtable, versioned)                */
/* ------------------------------------------------------------------ */

typedef struct fir_curve_s {
    size_t  struct_size;               /* = sizeof(fir_curve_t)         */
    void   *user_data;

    /* Required: discount factor for t years from the valuation date.
       Must return a finite, strictly positive value. */
    double (*discount)(void *user_data, double t);

    /* Optional (NULL => key-rate risk returns FIR_E_NO_BUMP_SUPPORT).
       bump_bp is additive on the zero rate at pillar `tenor` years,
       with the caller's own interpolation decay. */
    fir_status_t (*bump)(void *user_data, double tenor, double bump_bp);

    /* Optional: parallel shift of the entire curve. NULL is allowed
       even when `bump` is set; parallel and effective risk then return
       FIR_E_NO_BUMP_SUPPORT rather than being faked from pillars. */
    fir_status_t (*bump_parallel)(void *user_data, double bump_bp);

    /* Required if either bump callback is set: restores the unbumped
       curve. The library calls this after every bumped revaluation,
       including on failure paths. */
    fir_status_t (*reset)(void *user_data);

    /* Optional: pillar tenors in years, for default key-rate buckets. */
    const double *pillars;
    size_t        pillar_count;
} fir_curve_t;

/* ------------------------------------------------------------------ */
/* pricing                                                             */
/* ------------------------------------------------------------------ */

FIR_API fir_status_t fir_bond_price_from_yield(const fir_bond_t *bond,
                                               double            yield,
                                               fir_compounding_t comp,
                                               double           *out_clean_price);

FIR_API fir_status_t fir_bond_yield_from_price(const fir_bond_t *bond,
                                               double            clean_price,
                                               fir_compounding_t comp,
                                               double           *out_yield);

FIR_API fir_status_t fir_bond_price_from_curve(const fir_bond_t  *bond,
                                               const fir_curve_t *curve,
                                               double            *out_clean_price);

/* Continuously compounded spread over the supplied curve, in decimal
   (1bp = 0.0001). */
FIR_API fir_status_t fir_bond_zspread(const fir_bond_t  *bond,
                                      const fir_curve_t *curve,
                                      double             clean_price,
                                      double            *out_zspread);

/* ------------------------------------------------------------------ */
/* analytic risk (yield-based)                                         */
/* ------------------------------------------------------------------ */

FIR_API fir_status_t fir_bond_macaulay_duration(const fir_bond_t *bond,
                                                double            yield,
                                                fir_compounding_t comp,
                                                double           *out_duration);

FIR_API fir_status_t fir_bond_modified_duration(const fir_bond_t *bond,
                                                double            yield,
                                                fir_compounding_t comp,
                                                double           *out_duration);

FIR_API fir_status_t fir_bond_convexity(const fir_bond_t *bond,
                                        double            yield,
                                        fir_compounding_t comp,
                                        double           *out_convexity);

/* Price change per 1bp yield move, in the same units as the price. */
FIR_API fir_status_t fir_bond_dv01(const fir_bond_t *bond,
                                   double            yield,
                                   fir_compounding_t comp,
                                   double           *out_dv01);

/* ------------------------------------------------------------------ */
/* curve risk (bump and revalue)                                       */
/* ------------------------------------------------------------------ */

/* These take a context because the bump size is read from it. The bond
   need not have been built on the same context. */

FIR_API fir_status_t fir_bond_effective_duration(fir_context_t     *ctx,
                                                 const fir_bond_t  *bond,
                                                 const fir_curve_t *curve,
                                                 double            *out_duration);

FIR_API fir_status_t fir_bond_effective_convexity(fir_context_t     *ctx,
                                                  const fir_bond_t  *bond,
                                                  const fir_curve_t *curve,
                                                  double            *out_convexity);

/* Parallel shift of the whole curve. */
FIR_API fir_status_t fir_bond_curve_dv01(fir_context_t     *ctx,
                                         const fir_bond_t  *bond,
                                         const fir_curve_t *curve,
                                         double            *out_dv01);

/* One sensitivity per tenor; writes n values into out_krd. */
FIR_API fir_status_t fir_bond_key_rate_durations(fir_context_t     *ctx,
                                                 const fir_bond_t  *bond,
                                                 const fir_curve_t *curve,
                                                 const double      *tenors,
                                                 double            *out_krd,
                                                 size_t             n);

#ifdef __cplusplus
}
#endif
#endif /* FIRISK_H_INCLUDED */
