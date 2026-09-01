#ifndef FIRISK_CORE_COMPAT_HPP_INCLUDED
#define FIRISK_CORE_COMPAT_HPP_INCLUDED

#if defined(__GNUC__) || defined(__clang__)
#  define FIR_PRINTF_FORMAT(fmt_index, first_arg) \
       __attribute__((format(printf, fmt_index, first_arg)))
#else
#  define FIR_PRINTF_FORMAT(fmt_index, first_arg)
#endif

#endif /* FIRISK_CORE_COMPAT_HPP_INCLUDED */
