## Automake.am for OSPF API client

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib

sbin_PROGRAMS = ospfn ospfnstop

ospfn_SOURCES = \
	ospfn_opaque_lsa.c tables.c utility.c ccn_fib.c ospfn.c

ospfn_LDADD = ../ospfclient/libospfapiclient.la \
	../ospfd/libospf.la ../lib/libzebra.la @LIBCAP@ -lccn -lcrypto

ospfn_CFLAGS = $(AM_CFLAGS) $(PICFLAGS)
ospfn_LDFLAGS = $(AM_LDFLAGS) $(PILDFLAGS)

ospfnstop_SOURCES = \
	ospfnstop.c

ospfnstop_LDADD = @LIBCAP@ -lccn -lcrypto

ospfnstop_CFLAGS = $(AM_CFLAGS) $(PICFLAGS)
ospfnstop_LDFLAGS = $(AM_LDFLAGS) $(PILDFLAGS)

