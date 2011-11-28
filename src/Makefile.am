plugin_LTLIBRARIES = libgstmotiondetector.la

libgstmotiondetector_la_SOURCES = gstmotiondetector.c gstmotiondetector.h
libgstmotiondetector_la_CFLAGS = $(GST_CFLAGS) @OPENCV_CFLAGS@
libgstmotiondetector_la_LIBADD = $(GST_LIBS) @OPENCV_LIBS@
libgstmotiondetector_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmotiondetector_la_LIBTOOLFLAGS = --tag=disable-static
