#ifndef SWIGMACRO_H
#define SWIGMACRO_H

#ifndef SWIG
# define STINLINE static inline
# define STINLINE_END
# define SWIG_immutable %immutable;
# define SWIG_mutable %mutable;

#else  /* SWIG */

# define STINLINE %inline %{
# define STINLINE_END %}
# define SWIG_immutable
# define SWIG_mutable

#endif /* SWIG */

#endif
