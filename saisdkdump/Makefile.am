AM_CXXFLAGS = $(SAIINC)

bin_PROGRAMS = saisdkdump

if SAIVS
SAILIB=-L$(top_srcdir)/vslib/.libs -lsaivs
else
SAILIB=-lsai
endif

saisdkdump_SOURCES = saisdkdump.cpp
saisdkdump_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS)
saisdkdump_CXXFLAGS = $(DBGFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS_COMMON) $(CODE_COVERAGE_CXXFLAGS)
saisdkdump_LDADD = -lhiredis -lswsscommon $(SAILIB) -lpthread -L$(top_srcdir)/meta/.libs -lsaimetadata -lsaimeta -lzmq $(CODE_COVERAGE_LIBS)

if GCOV_ENABLED
#saisdkdump_LDADD += -lgcovpreload
saisdkdump_LDADD += -L$(top_srcdir)/gcovpreload/.libs/ -lgcovpreload
saisdkdump_LDFLAGS = -Wl,--no-as-needed,-Bdynamic
endif
