if HAVE_DOXYGEN

doxyfile.stamp:
	$(DOXYGEN) Doxyfile
	echo Timestamp > doxyfile.stamp

CLEANFILES = doxyfile.stamp

all-local: doxyfile.stamp
clean-local:
	@rm -rf $(top_srcdir)/doc/html
	@rm -rf $(top_srcdir)/doc/latex
endif
