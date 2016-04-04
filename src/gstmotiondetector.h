/*
 * gstmotiondetector.h
 * Copyright Matthew Brush <mbrush@codebrainz.ca>
 * See COPYING file for license information.
 */
#ifndef PLUGIN_H__
#define PLUGIN_H__ 1

#include <gst/gst.h>
#include <cv.h>

G_BEGIN_DECLS

#define GST_TYPE_MOTION_DETECTOR (gst_motion_detector_get_type())
#define GST_MOTION_DETECTOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MOTION_DETECTOR,GstMotionDetector))
#define GST_MOTION_DETECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MOTION_DETECTOR,GstMotionDetectorClass))
#define GST_IS_MOTION_DETECTOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MOTION_DETECTOR))
#define GST_IS_MOTION_DETECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MOTION_DETECTOR))

typedef struct _GstMotionDetector      GstMotionDetector;
typedef struct _GstMotionDetectorClass GstMotionDetectorClass;

struct _GstMotionDetector
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean motion_detected;
  gboolean post_messages;
  gboolean draw_motion;

  gdouble avg_weight;
  guint threshold;
  guint dilate_iterations;
  guint erode_iterations;
  guint min_blob_size;
  guint max_blob_size;
  guint num_blobs;
  guint rate_limit;
  gboolean rate_inhibit;
  guint width;
  guint height;

  IplImage *run_avg;
  IplImage *currentImage;
};

struct _GstMotionDetectorClass
{
  GstElementClass parent_class;
};

GType gst_motion_detector_get_type (void);


G_END_DECLS

#endif /* PLUGIN_H__ */
