#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "firisk.h"

namespace nb = nanobind;

namespace {

/* ------------------------------------------------------------------ */
/* errors                                                              */
/* ------------------------------------------------------------------ */

/* Every status code becomes an exception. Callers get Python
   semantics; the C ABI keeps its return codes. */
void raise_status(fir_status_t status, const char *what)
{
    if (status == FIR_OK)
        return;

    std::string message = std::string(what) + ": " + fir_strerror(status);

    switch (status) {
    case FIR_E_NULL_ARG:
    case FIR_E_BAD_ARG:
    case FIR_E_BAD_DATE:
    case FIR_E_BAD_SCHEDULE:
    case FIR_E_UNSUPPORTED:
    case FIR_E_NO_BUMP_SUPPORT:
        throw nb::value_error(message.c_str());
    case FIR_E_BUFFER_TOO_SMALL:
    case FIR_E_BAD_STRUCT_SIZE:
        throw std::runtime_error(message);
    case FIR_E_NO_CONVERGENCE:
        throw std::runtime_error(message);
    case FIR_E_ALLOC:
        throw std::bad_alloc();
    default:
        throw std::runtime_error(message);
    }
}

/* Same, but pulls the richer diagnostic off the context when there is
   one. The pointer is only valid until the next call, so it is copied
   immediately. */
void raise_context(fir_context_t *ctx, fir_status_t status, const char *what)
{
    if (status == FIR_OK)
        return;

    std::string detail = fir_context_last_message(ctx);
    std::string message = std::string(what) + ": " + detail;

    if (status == FIR_E_NO_BUMP_SUPPORT || status == FIR_E_BAD_ARG)
        throw nb::value_error(message.c_str());

    throw std::runtime_error(message);
}

/* ------------------------------------------------------------------ */
/* calendar                                                            */
/* ------------------------------------------------------------------ */

class Calendar {
public:
    Calendar() : handle_(fir_calendar_new())
    {
        if (!handle_)
            throw std::bad_alloc();
    }

    ~Calendar()
    {
        fir_calendar_free(handle_);
    }

    Calendar(const Calendar &) = delete;
    Calendar &operator=(const Calendar &) = delete;

    void set_holidays(const std::vector<int> &dates)
    {
        std::vector<fir_date_t> converted(dates.begin(), dates.end());
        raise_status(
            fir_calendar_set_holidays(handle_, converted.data(),
                                      converted.size()),
            "set_holidays");
    }

    void set_weekend(int mask)
    {
        raise_status(fir_calendar_set_weekend(handle_, mask), "set_weekend");
    }

    bool is_business_day(int date) const
    {
        return fir_calendar_is_business_day(handle_, date) != 0;
    }

    fir_calendar_t *handle() const { return handle_; }

private:
    fir_calendar_t *handle_;
};

/* ------------------------------------------------------------------ */
/* curves                                                              */
/* ------------------------------------------------------------------ */

/* Base for anything that can fill a fir_curve_t. Native subclasses do
   all their work in C++; PyCurve trampolines back into Python. */
class Curve {
public:
    virtual ~Curve() = default;
    virtual const fir_curve_t *handle() = 0;
};

/* Flat continuously compounded curve. No Python involved once
   constructed. */
class FlatCurve : public Curve {
public:
    explicit FlatCurve(double rate) : base_(rate), rate_(rate)
    {
        std::memset(&curve_, 0, sizeof curve_);
        curve_.struct_size   = sizeof(fir_curve_t);
        curve_.user_data     = this;
        curve_.discount      = &FlatCurve::discount_cb;
        curve_.bump          = &FlatCurve::bump_cb;
        curve_.bump_parallel = &FlatCurve::bump_parallel_cb;
        curve_.reset         = &FlatCurve::reset_cb;
        curve_.pillars       = nullptr;
        curve_.pillar_count  = 0;
    }

    const fir_curve_t *handle() override { return &curve_; }

    double rate() const { return rate_; }

    double discount(double t) const { return std::exp(-rate_ * t); }

private:
    static double discount_cb(void *ud, double t)
    {
        return static_cast<FlatCurve *>(ud)->discount(t);
    }

    /* A flat curve has no pillars, so any tenor moves the whole
       level. That makes key rate durations degenerate but keeps the
       class usable for effective risk. */
    static fir_status_t bump_cb(void *ud, double, double bump_bp)
    {
        static_cast<FlatCurve *>(ud)->rate_ += bump_bp * 1e-4;
        return FIR_OK;
    }

    static fir_status_t bump_parallel_cb(void *ud, double bump_bp)
    {
        static_cast<FlatCurve *>(ud)->rate_ += bump_bp * 1e-4;
        return FIR_OK;
    }

    static fir_status_t reset_cb(void *ud)
    {
        FlatCurve *self = static_cast<FlatCurve *>(ud);
        self->rate_ = self->base_;
        return FIR_OK;
    }

    fir_curve_t curve_;
    double      base_;
    double      rate_;
};

/* Piecewise-linear zero curve over pillar tenors, with per-pillar
   bumps that decay linearly to the neighbouring pillars. This is the
   one that makes key rate durations meaningful: a bump at 5y leaves
   the 2y and 10y points untouched. */
class ZeroCurve : public Curve {
public:
    ZeroCurve(std::vector<double> tenors, std::vector<double> zeros)
        : tenors_(std::move(tenors))
        , zeros_(std::move(zeros))
        , bumps_()
    {
        if (tenors_.size() != zeros_.size())
            throw nb::value_error("tenors and zeros must be the same length");
        if (tenors_.empty())
            throw nb::value_error("a curve needs at least one pillar");

        for (size_t i = 1; i < tenors_.size(); ++i) {
            if (!(tenors_[i] > tenors_[i - 1]))
                throw nb::value_error("tenors must be strictly increasing");
        }
        if (!(tenors_.front() > 0.0))
            throw nb::value_error("tenors must be positive");

        bumps_.assign(tenors_.size(), 0.0);

        std::memset(&curve_, 0, sizeof curve_);
        curve_.struct_size   = sizeof(fir_curve_t);
        curve_.user_data     = this;
        curve_.discount      = &ZeroCurve::discount_cb;
        curve_.bump          = &ZeroCurve::bump_cb;
        curve_.bump_parallel = &ZeroCurve::bump_parallel_cb;
        curve_.reset         = &ZeroCurve::reset_cb;
        curve_.pillars       = tenors_.data();
        curve_.pillar_count  = tenors_.size();
    }

    const fir_curve_t *handle() override { return &curve_; }

    const std::vector<double> &tenors() const { return tenors_; }

    /* Linear interpolation on zero rates, flat extrapolation at both
       ends. Bumps are interpolated the same way, so a pillar bump
       decays to zero at its neighbours. */
    double zero_rate(double t) const
    {
        const size_t n = tenors_.size();

        if (t <= tenors_.front())
            return zeros_.front() + bumps_.front();
        if (t >= tenors_.back())
            return zeros_.back() + bumps_.back();

        size_t hi = 1;
        while (hi < n && tenors_[hi] < t)
            ++hi;

        const size_t lo = hi - 1;
        const double w =
            (t - tenors_[lo]) / (tenors_[hi] - tenors_[lo]);

        const double a = zeros_[lo] + bumps_[lo];
        const double b = zeros_[hi] + bumps_[hi];

        return a + w * (b - a);
    }

    double discount(double t) const
    {
        return std::exp(-zero_rate(t) * t);
    }

private:
    static double discount_cb(void *ud, double t)
    {
        return static_cast<ZeroCurve *>(ud)->discount(t);
    }

    static fir_status_t bump_cb(void *ud, double tenor, double bump_bp)
    {
        ZeroCurve *self = static_cast<ZeroCurve *>(ud);

        for (size_t i = 0; i < self->tenors_.size(); ++i) {
            if (std::fabs(self->tenors_[i] - tenor) < 1e-9) {
                self->bumps_[i] += bump_bp * 1e-4;
                return FIR_OK;
            }
        }
        return FIR_E_BAD_ARG;
    }

    static fir_status_t bump_parallel_cb(void *ud, double bump_bp)
    {
        ZeroCurve *self = static_cast<ZeroCurve *>(ud);
        for (double &b : self->bumps_)
            b += bump_bp * 1e-4;
        return FIR_OK;
    }

    static fir_status_t reset_cb(void *ud)
    {
        ZeroCurve *self = static_cast<ZeroCurve *>(ud);
        self->bumps_.assign(self->bumps_.size(), 0.0);
        return FIR_OK;
    }

    fir_curve_t         curve_;
    std::vector<double> tenors_;
    std::vector<double> zeros_;
    std::vector<double> bumps_;
};

/* Escape hatch: wraps any Python object exposing discount(t), and
   optionally bump/bump_parallel/reset. Each discount call reacquires
   the GIL, so this costs one Python round trip per cashflow per bump.
   Prefer ZeroCurve where it fits. */
class PyCurve : public Curve {
public:
    explicit PyCurve(nb::object obj) : obj_(std::move(obj))
    {
        if (!nb::hasattr(obj_, "discount"))
            throw nb::value_error("a curve object must have a discount(t) method");

        const bool bumpable =
            nb::hasattr(obj_, "bump") && nb::hasattr(obj_, "reset");
        const bool parallel =
            nb::hasattr(obj_, "bump_parallel") && nb::hasattr(obj_, "reset");

        std::memset(&curve_, 0, sizeof curve_);
        curve_.struct_size   = sizeof(fir_curve_t);
        curve_.user_data     = this;
        curve_.discount      = &PyCurve::discount_cb;
        curve_.bump          = bumpable ? &PyCurve::bump_cb : nullptr;
        curve_.bump_parallel = parallel ? &PyCurve::bump_parallel_cb : nullptr;
        curve_.reset         = (bumpable || parallel)
                                   ? &PyCurve::reset_cb : nullptr;
        curve_.pillars       = nullptr;
        curve_.pillar_count  = 0;
    }

    const fir_curve_t *handle() override { return &curve_; }

private:
    /* Exceptions must not cross back into C. A raising callback
       returns a sentinel the library rejects as a bad discount
       factor, or an error status for the bump hooks. */
    static double discount_cb(void *ud, double t)
    {
        PyCurve *self = static_cast<PyCurve *>(ud);
        nb::gil_scoped_acquire gil;

        try {
            return nb::cast<double>(self->obj_.attr("discount")(t));
        } catch (...) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    static fir_status_t bump_cb(void *ud, double tenor, double bump_bp)
    {
        PyCurve *self = static_cast<PyCurve *>(ud);
        nb::gil_scoped_acquire gil;

        try {
            self->obj_.attr("bump")(tenor, bump_bp);
            return FIR_OK;
        } catch (...) {
            return FIR_E_BAD_ARG;
        }
    }

    static fir_status_t bump_parallel_cb(void *ud, double bump_bp)
    {
        PyCurve *self = static_cast<PyCurve *>(ud);
        nb::gil_scoped_acquire gil;

        try {
            self->obj_.attr("bump_parallel")(bump_bp);
            return FIR_OK;
        } catch (...) {
            return FIR_E_BAD_ARG;
        }
    }

    static fir_status_t reset_cb(void *ud)
    {
        PyCurve *self = static_cast<PyCurve *>(ud);
        nb::gil_scoped_acquire gil;

        try {
            self->obj_.attr("reset")();
            return FIR_OK;
        } catch (...) {
            return FIR_E_BAD_ARG;
        }
    }

    fir_curve_t curve_;
    nb::object  obj_;
};

/* ------------------------------------------------------------------ */
/* context and bond                                                    */
/* ------------------------------------------------------------------ */

class Bond;

class Context {
public:
    Context() : handle_(fir_context_new())
    {
        if (!handle_)
            throw std::bad_alloc();
    }

    ~Context()
    {
        fir_context_free(handle_);
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    void set_valuation_date(int date)
    {
        raise_context(handle_,
                      fir_context_set_valuation_date(handle_, date),
                      "valuation_date");
    }

    int valuation_date() const { return valuation_date_cache_; }

    void set_bump_size(double bp)
    {
        raise_context(handle_, fir_context_set_bump_size(handle_, bp),
                      "bump_size");
    }

    void set_solver_tolerance(double tol)
    {
        raise_context(handle_, fir_context_set_solver_tolerance(handle_, tol),
                      "solver_tolerance");
    }

    void set_solver_max_iter(int n)
    {
        raise_context(handle_, fir_context_set_solver_max_iter(handle_, n),
                      "solver_max_iter");
    }

    fir_context_t *handle() const { return handle_; }

    void cache_valuation_date(int date) { valuation_date_cache_ = date; }

private:
    fir_context_t *handle_;
    int            valuation_date_cache_ = 0;
};

class Bond {
public:
    Bond(Context &ctx,
         int issue_date,
         int maturity_date,
         double coupon_rate,
         double face,
         int frequency,
         int daycount,
         int business_day_convention,
         int first_coupon_date,
         bool end_of_month,
         Calendar *calendar)
        : handle_(nullptr)
    {
        fir_bond_def_t def;
        std::memset(&def, 0, sizeof def);

        def.struct_size             = sizeof(fir_bond_def_t);
        def.issue_date              = issue_date;
        def.first_coupon_date       = first_coupon_date;
        def.maturity_date           = maturity_date;
        def.coupon_rate             = coupon_rate;
        def.face                    = face;
        def.frequency               = frequency;
        def.daycount                = daycount;
        def.business_day_convention = business_day_convention;
        def.end_of_month            = end_of_month ? 1 : 0;
        def.calendar                = calendar ? calendar->handle() : nullptr;

        handle_ = fir_bond_new(ctx.handle(), &def);
        if (!handle_) {
            std::string detail = fir_context_last_message(ctx.handle());
            throw nb::value_error(("bond: " + detail).c_str());
        }
    }

    ~Bond()
    {
        fir_bond_free(handle_);
    }

    Bond(const Bond &) = delete;
    Bond &operator=(const Bond &) = delete;

    size_t cashflow_count() const
    {
        size_t n = 0;
        raise_status(fir_bond_cashflow_count(handle_, &n), "cashflow_count");
        return n;
    }

    /* Returns (times, amounts) as NumPy arrays. The buffers are owned
       by capsules so NumPy frees them, not us. */
    nb::tuple cashflows() const
    {
        const size_t n = cashflow_count();

        double *times = new double[n];
        double *amounts = new double[n];

        const fir_status_t rv =
            fir_bond_cashflows(handle_, times, amounts, n);
        if (rv != FIR_OK) {
            delete[] times;
            delete[] amounts;
            raise_status(rv, "cashflows");
        }

        nb::capsule times_owner(times, [](void *p) noexcept {
            delete[] static_cast<double *>(p);
        });
        nb::capsule amounts_owner(amounts, [](void *p) noexcept {
            delete[] static_cast<double *>(p);
        });

        return nb::make_tuple(
            nb::ndarray<nb::numpy, double>(times, {n}, times_owner),
            nb::ndarray<nb::numpy, double>(amounts, {n}, amounts_owner));
    }

    double accrued() const
    {
        double out = 0.0;
        raise_status(fir_bond_accrued(handle_, &out), "accrued");
        return out;
    }

    double price_from_yield(double y, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_price_from_yield(handle_, y, comp, &out),
                     "price_from_yield");
        return out;
    }

    double yield_from_price(double price, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_yield_from_price(handle_, price, comp, &out),
                     "yield_from_price");
        return out;
    }

    double price_from_curve(Curve &curve) const
    {
        double out = 0.0;
        raise_status(
            fir_bond_price_from_curve(handle_, curve.handle(), &out),
            "price_from_curve");
        return out;
    }

    double zspread(Curve &curve, double clean_price) const
    {
        double out = 0.0;
        raise_status(
            fir_bond_zspread(handle_, curve.handle(), clean_price, &out),
            "zspread");
        return out;
    }

    double macaulay_duration(double y, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_macaulay_duration(handle_, y, comp, &out),
                     "macaulay_duration");
        return out;
    }

    double modified_duration(double y, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_modified_duration(handle_, y, comp, &out),
                     "modified_duration");
        return out;
    }

    double convexity(double y, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_convexity(handle_, y, comp, &out), "convexity");
        return out;
    }

    double dv01(double y, int comp) const
    {
        double out = 0.0;
        raise_status(fir_bond_dv01(handle_, y, comp, &out), "dv01");
        return out;
    }

    double effective_duration(Context &ctx, Curve &curve) const
    {
        double out = 0.0;
        raise_context(ctx.handle(),
                      fir_bond_effective_duration(ctx.handle(), handle_,
                                                  curve.handle(), &out),
                      "effective_duration");
        return out;
    }

    double effective_convexity(Context &ctx, Curve &curve) const
    {
        double out = 0.0;
        raise_context(ctx.handle(),
                      fir_bond_effective_convexity(ctx.handle(), handle_,
                                                   curve.handle(), &out),
                      "effective_convexity");
        return out;
    }

    double curve_dv01(Context &ctx, Curve &curve) const
    {
        double out = 0.0;
        raise_context(ctx.handle(),
                      fir_bond_curve_dv01(ctx.handle(), handle_,
                                          curve.handle(), &out),
                      "curve_dv01");
        return out;
    }

    nb::ndarray<nb::numpy, double> key_rate_durations(
        Context &ctx, Curve &curve, const std::vector<double> &tenors) const
    {
        const size_t n = tenors.size();
        double *out = new double[n ? n : 1];

        const fir_status_t rv = fir_bond_key_rate_durations(
            ctx.handle(), handle_, curve.handle(),
            n ? tenors.data() : nullptr, out, n);

        if (rv != FIR_OK) {
            delete[] out;
            raise_context(ctx.handle(), rv, "key_rate_durations");
        }

        nb::capsule owner(out, [](void *p) noexcept {
            delete[] static_cast<double *>(p);
        });

        return nb::ndarray<nb::numpy, double>(out, {n}, owner);
    }

private:
    fir_bond_t *handle_;
};

} /* namespace */

/* ------------------------------------------------------------------ */
/* module                                                              */
/* ------------------------------------------------------------------ */

NB_MODULE(_firisk, m)
{
    m.doc() = "Fixed income risk analytics";

    m.attr("__version__") = fir_get_version_string();

    m.def("version", &fir_get_version_string,
          "Library version as a string.");

    /* -- enums -- */

    nb::enum_<fir_daycount_e>(m, "DayCount")
        .value("ACT_360", FIR_DC_ACT_360)
        .value("ACT_365F", FIR_DC_ACT_365F)
        .value("ACT_ACT_ISDA", FIR_DC_ACT_ACT_ISDA)
        .value("ACT_ACT_ICMA", FIR_DC_ACT_ACT_ICMA)
        .value("THIRTY_360_BOND", FIR_DC_THIRTY_360_BOND)
        .value("THIRTY_E_360", FIR_DC_THIRTY_E_360);

    nb::enum_<fir_frequency_e>(m, "Frequency")
        .value("ZERO", FIR_FREQ_ZERO)
        .value("ANNUAL", FIR_FREQ_ANNUAL)
        .value("SEMIANNUAL", FIR_FREQ_SEMIANNUAL)
        .value("QUARTERLY", FIR_FREQ_QUARTERLY)
        .value("MONTHLY", FIR_FREQ_MONTHLY);

    nb::enum_<fir_compounding_e>(m, "Compounding")
        .value("SIMPLE", FIR_COMP_SIMPLE)
        .value("PERIODIC", FIR_COMP_PERIODIC)
        .value("CONTINUOUS", FIR_COMP_CONTINUOUS);

    nb::enum_<fir_bdc_e>(m, "BusinessDayConvention")
        .value("NONE", FIR_BDC_NONE)
        .value("FOLLOWING", FIR_BDC_FOLLOWING)
        .value("MODIFIED_FOLLOWING", FIR_BDC_MODIFIED_FOLLOWING)
        .value("PRECEDING", FIR_BDC_PRECEDING);

    nb::enum_<fir_weekend_e>(m, "Weekend")
        .value("NONE", FIR_WEEKEND_NONE)
        .value("SAT_SUN", FIR_WEEKEND_SAT_SUN)
        .value("FRI_SAT", FIR_WEEKEND_FRI_SAT);

    /* -- calendar -- */

    nb::class_<Calendar>(m, "Calendar")
        .def(nb::init<>())
        .def("set_holidays", &Calendar::set_holidays, nb::arg("dates"),
             "Set holiday dates as YYYYMMDD integers. Copied, not borrowed.")
        .def("set_weekend", &Calendar::set_weekend, nb::arg("mask"))
        .def("is_business_day", &Calendar::is_business_day, nb::arg("date"));

    /* -- curves -- */

    nb::class_<Curve>(m, "Curve");

    nb::class_<FlatCurve, Curve>(m, "FlatCurve")
        .def(nb::init<double>(), nb::arg("rate"),
             "Flat continuously compounded curve at `rate`.")
        .def_prop_ro("rate", &FlatCurve::rate)
        .def("discount", &FlatCurve::discount, nb::arg("t"));

    nb::class_<ZeroCurve, Curve>(m, "ZeroCurve")
        .def(nb::init<std::vector<double>, std::vector<double>>(),
             nb::arg("tenors"), nb::arg("zeros"),
             "Piecewise-linear zero curve. Pillar bumps decay linearly to "
             "the neighbouring pillars, so key rate durations localise.")
        .def_prop_ro("tenors", &ZeroCurve::tenors)
        .def("zero_rate", &ZeroCurve::zero_rate, nb::arg("t"))
        .def("discount", &ZeroCurve::discount, nb::arg("t"));

    nb::class_<PyCurve, Curve>(m, "PyCurve")
        .def(nb::init<nb::object>(), nb::arg("obj"),
             "Wrap any object with a discount(t) method, and optionally "
             "bump(tenor, bp), bump_parallel(bp) and reset(). One Python "
             "call per cashflow per bump, so prefer ZeroCurve where it fits.");

    /* -- context -- */

    nb::class_<Context>(m, "Context")
        .def(nb::init<>())
        .def("set_valuation_date", &Context::set_valuation_date,
             nb::arg("date"), "Valuation date as a YYYYMMDD integer.")
        .def("set_bump_size", &Context::set_bump_size, nb::arg("bp"),
             "Bump size in basis points for all bump-and-revalue risk.")
        .def("set_solver_tolerance", &Context::set_solver_tolerance,
             nb::arg("tol"))
        .def("set_solver_max_iter", &Context::set_solver_max_iter,
             nb::arg("n"));

    /* -- bond -- */

    nb::class_<Bond>(m, "Bond")
        .def(nb::init<Context &, int, int, double, double, int, int, int,
                      int, bool, Calendar *>(),
             nb::arg("ctx"),
             nb::arg("issue_date"),
             nb::arg("maturity_date"),
             nb::arg("coupon_rate"),
             nb::arg("face") = 100.0,
             nb::arg("frequency") = FIR_FREQ_SEMIANNUAL,
             nb::arg("daycount") = FIR_DC_THIRTY_360_BOND,
             nb::arg("business_day_convention") = FIR_BDC_NONE,
             nb::arg("first_coupon_date") = 0,
             nb::arg("end_of_month") = false,
             nb::arg("calendar").none() = nb::none(),
             nb::keep_alive<1, 2>())

        .def_prop_ro("cashflow_count", &Bond::cashflow_count)
        .def_prop_ro("accrued", &Bond::accrued)
        .def("cashflows", &Bond::cashflows,
             "Returns (times, amounts) as NumPy arrays.")

        .def("price_from_yield", &Bond::price_from_yield,
             nb::arg("yield_"), nb::arg("compounding") = FIR_COMP_PERIODIC)
        .def("yield_from_price", &Bond::yield_from_price,
             nb::arg("clean_price"),
             nb::arg("compounding") = FIR_COMP_PERIODIC)
        .def("price_from_curve", &Bond::price_from_curve, nb::arg("curve"))
        .def("zspread", &Bond::zspread,
             nb::arg("curve"), nb::arg("clean_price"))

        .def("macaulay_duration", &Bond::macaulay_duration,
             nb::arg("yield_"), nb::arg("compounding") = FIR_COMP_PERIODIC)
        .def("modified_duration", &Bond::modified_duration,
             nb::arg("yield_"), nb::arg("compounding") = FIR_COMP_PERIODIC)
        .def("convexity", &Bond::convexity,
             nb::arg("yield_"), nb::arg("compounding") = FIR_COMP_PERIODIC)
        .def("dv01", &Bond::dv01,
             nb::arg("yield_"), nb::arg("compounding") = FIR_COMP_PERIODIC)

        .def("effective_duration", &Bond::effective_duration,
             nb::arg("ctx"), nb::arg("curve"))
        .def("effective_convexity", &Bond::effective_convexity,
             nb::arg("ctx"), nb::arg("curve"))
        .def("curve_dv01", &Bond::curve_dv01,
             nb::arg("ctx"), nb::arg("curve"))
        .def("key_rate_durations", &Bond::key_rate_durations,
             nb::arg("ctx"), nb::arg("curve"), nb::arg("tenors"));
}
